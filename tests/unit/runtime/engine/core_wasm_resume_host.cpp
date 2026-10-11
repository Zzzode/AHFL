// RFC 0026 KR6.5 E4-B2-D2a (F4) permanent regression for the engine-agnostic
// production resume-host driver. Hand-rolled check()/main(); NO gtest. Links
// PRIVATE only against ahfl_runtime_engine.
//
// The engine behind the core_wasm_resume_engine port is a SYNCHRONOUS IN-TEST
// DOUBLE (FakeResumeEngine), not a self-built VM: it owns one fixed 64 KiB
// page, an instance-lifetime bump allocator, and a scripted schedule that
// publishes node-event records (through the SHARED resume_test_support.hpp
// synthesizer) and invokes the host's import callback synchronously on the
// SAME call stack nested inside invoke_run2. This is the same evidence class
// as D1b's hand-built memory fixtures -- it proves the host DRIVER (admission
// order, L0/L3/L4 transfers, the bind/CAS/ACK handshake, fail-closed arms,
// terminal consume), not Wasm execution. The real-engine port is F5 (Node
// embedded engine).
//
// The durable evidence drives a REAL IntegrityPayloadStore (Linux durable FS;
// SKIP 77 elsewhere), round-tripping Suspended -> Injected -> Consumed.

#include "resume_test_support.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ahfl/runtime/ahfl_host.h"
#include "runtime/engine/core_wasm_idempotency_token.hpp"
#include "runtime/engine/core_wasm_node_events.hpp"
#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_resume_host.hpp"
#include "runtime/engine/durable_effect_authority.hpp"
#include "runtime/engine/durable_effect_intent.hpp"
#include "runtime/engine/durable_effect_live_mint.hpp"

namespace {

using namespace ahfl::runtime::resume_test_support;

namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace host = ahfl::runtime::core_wasm_resume_host;
namespace nev = ahfl::runtime::core_wasm_node_events;
namespace ps = ahfl::runtime::payload_store;
namespace rc = ahfl::runtime::core_wasm_resume_controller;
namespace dea = ahfl::runtime::durable_effect_authority;
namespace dei = ahfl::runtime::durable_effect_intent;
namespace delm = ahfl::runtime::durable_effect_live_mint;
namespace tok = ahfl::runtime::core_wasm_idempotency_token;

using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::core_wasm_resume::ResumeState;
using ahfl::runtime::payload_store::ResumeCheckpointId;

namespace fs = std::filesystem;

int g_failures = 0;
int g_total = 0;

void check(bool ok, std::string_view name) {
    ++g_total;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// ---- the synchronous in-test engine double ---------------------------------

// One scripted capability call site: the L2 Param frame the module presents
// (a view into already-existing instance storage), the ahfl_cap import
// ordinal, and what run2 does AFTER the host replies.
struct FakeCapStep {
    std::uint32_t import_ordinal{0};
    std::vector<std::uint8_t> param_frame;
    bool trap_after_reply{false}; // raise a Wasm trap once this import returned
};

// The full scripted run: the manifest node order the fake schedules, one
// entry per cap node (in call order), and optional fault injections.
struct FakeScript {
    std::vector<ManifestNodeSpec> nodes;
    std::vector<FakeCapStep> caps;
    // Invoked immediately before the frontier callback (index = its call-site
    // index); used to race a competing generation for the CAS test.
    std::function<void(std::size_t)> before_callback;
    // Make the FIRST alloc_then_write fail with this arm (L0 transfer test).
    std::optional<eng::EngineError> fail_first_alloc{};
    // Make fresh_instance fail.
    std::optional<eng::EngineError> fail_instantiation{};
    // Make read_whole_memory report a short page after a successful run2,
    // simulating an engine port whose whole-page transport was truncated.
    bool truncate_whole_memory{false};
    // WH-3: script the run2 tuple's raw status (default AHFL_CAP_OK). A
    // non-OK status (e.g. AHFL_CAP_PENDING) exercises the controller's
    // finish_run classification on the RESUME entry contract (a
    // fresh-instance replay must end OK; PENDING -> TransitionInvalid).
    std::optional<std::uint32_t> scripted_run2_status{};
};

// A record of every host frame transfer: its exact bytes and guest pointer.
struct AllocationRecord {
    std::uint32_t ptr{0};
    std::vector<std::uint8_t> bytes;
};

class FakeResumeEngine final : public eng::CoreWasmResumeEngine {
  public:
    explicit FakeResumeEngine(FakeScript script) : script_(std::move(script)) {
        const auto hb = nev::event_region_heap_base(script_.nodes.size());
        heap_base_ = hb.has_value() ? static_cast<std::uint32_t>(*hb) : 1032u;
        // Module-internal scratch (scripted runner-produced frames) gets a
        // reserved window; host L0/L3/L4 bumps start above it.
        host_next_ = heap_base_ + kScratchWindow;
    }

    [[nodiscard]] std::expected<void, eng::EngineError>
    fresh_instance(std::span<const std::uint8_t> module_bytes,
                   eng::ImportCallback callback) override {
        if (instantiated_) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        if (script_.fail_instantiation.has_value()) {
            return std::unexpected(*script_.fail_instantiation);
        }
        module_bytes_.assign(module_bytes.begin(), module_bytes.end());
        memory_.assign(kPageBytes, 0);
        callback_ = std::move(callback);
        host_next_ = heap_base_ + kScratchWindow;
        scratch_next_ = heap_base_;
        allocations_.clear();
        instantiated_ = true;
        return {};
    }

    [[nodiscard]] std::expected<std::span<const std::uint8_t>, eng::EngineError>
    read_whole_memory() override {
        if (!instantiated_) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        if (script_.truncate_whole_memory) {
            // Simulate an engine port (e.g. a pipe-framed subprocess under a
            // partial read) handing back a short, undersized page.
            return std::span<const std::uint8_t>(memory_.data(), memory_.size() - 1);
        }
        return std::span<const std::uint8_t>(memory_);
    }

    [[nodiscard]] std::expected<std::span<std::uint8_t>, eng::EngineError>
    mutable_whole_memory() override {
        if (!instantiated_) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        return std::span<std::uint8_t>(memory_);
    }

    [[nodiscard]] std::expected<eng::GuestPointer, eng::EngineError>
    alloc_then_write(std::span<const std::uint8_t> bytes) override {
        if (!instantiated_) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        if (allocations_.empty() && script_.fail_first_alloc.has_value()) {
            return std::unexpected(*script_.fail_first_alloc);
        }
        const std::uint64_t end =
            static_cast<std::uint64_t>(host_next_) + static_cast<std::uint64_t>(bytes.size());
        if (end > memory_.size()) {
            return std::unexpected(eng::EngineError::MemoryCapacityExceeded);
        }
        const auto ptr = host_next_;
        std::copy(bytes.begin(), bytes.end(), memory_.begin() + ptr);
        host_next_ = static_cast<std::uint32_t>(end);
        allocations_.push_back(
            AllocationRecord{ptr, std::vector<std::uint8_t>(bytes.begin(), bytes.end())});
        return eng::GuestPointer{ptr};
    }

    [[nodiscard]] std::expected<eng::Run2Outcome, eng::EngineError>
    invoke_run2(eng::GuestPointer entry_ptr, std::uint32_t entry_len) override {
        if (!instantiated_ || run_started_) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }
        run_started_ = true;
        if (entry_ptr.value == 0 ||
            static_cast<std::uint64_t>(entry_ptr.value) + entry_len > memory_.size()) {
            return std::unexpected(eng::EngineError::InvalidSequence);
        }

        std::uint32_t completed = 0;
        std::size_t cap_index = 0;
        std::uint32_t last_output_ptr = entry_ptr.value;
        std::uint32_t last_output_len = entry_len;

        for (const ManifestNodeSpec &node : script_.nodes) {
            if (node.cap_call_count == 0) {
                // Identity: forwards its input, publishes its event record.
                write_event_records(memory_, script_.nodes, completed + 1);
                ++completed;
                continue;
            }

            const FakeCapStep &step = script_.caps[cap_index];
            if (script_.before_callback) {
                script_.before_callback(cap_index);
            }

            // Simulate the runner-produced L2 Param view in module scratch.
            const std::uint32_t param_ptr = write_scratch(step.param_frame);

            // Events published so far = exactly this cap's schedule_pos.
            check(event_count() == completed, "fake.event_prefix_before_callback");

            eng::ImportObservation observation;
            observation.import_ordinal = step.import_ordinal;
            // The Fake writes the L2 Param view at scratch `param_ptr`; for a
            // 1-param import that pointer IS the single i32 arg (the bridge
            // lane's control-block pointer), matching the Node/wasm3 ports.
            observation.scalar_arg = param_ptr;
            observation.param_frame =
                std::span<const std::uint8_t>(memory_.data() + param_ptr, step.param_frame.size());
            observation.whole_memory = std::span<const std::uint8_t>(memory_);
            auto reply = callback_(observation);
            if (std::holds_alternative<eng::ImportAbort>(reply)) {
                // The host failed closed; the engine unwinds without resuming.
                return eng::Run2Outcome{eng::Run2HostAborted{}};
            }
            const auto &frame = std::get<eng::ImportReply>(reply);
            import_raw_statuses_.push_back(frame.raw_status);
            // D2b-8: a PENDING import reply ends run2 immediately with a
            // PENDING exit: the suspended node writes NO completion event and
            // no subsequent node runs (the driver persists the new coordinate).
            if (frame.raw_status == AHFL_CAP_PENDING) {
                eng::Run2ResultTuple pending_tuple;
                pending_tuple.raw_status = AHFL_CAP_PENDING;
                pending_tuple.output_ptr = eng::GuestPointer{0};
                pending_tuple.output_len = 0;
                return eng::Run2Outcome{pending_tuple};
            }
            last_output_ptr = frame.result_ptr.value;
            last_output_len = frame.result_len;
            ++cap_index;

            if (step.trap_after_reply) {
                return eng::Run2Outcome{eng::Run2Trapped{}};
            }

            // OK: the node completes, its event record is published AFTER the
            // frame returned (body-before-count is inside write_event_records).
            write_event_records(memory_, script_.nodes, completed + 1);
            ++completed;
        }

        eng::Run2ResultTuple tuple;
        if (script_.scripted_run2_status) {
            // WH-3: a scripted non-OK run2 status (e.g. AHFL_CAP_PENDING)
            // exercises the controller's finish_run classification on the
            // RESUME entry contract. A real module returns (status,0,0) on a
            // non-OK terminal status; the Fake mirrors that ABI shape.
            tuple.raw_status = *script_.scripted_run2_status;
            tuple.output_ptr = eng::GuestPointer{0};
            tuple.output_len = 0;
        } else {
            tuple.raw_status = AHFL_CAP_OK;
            tuple.output_ptr = eng::GuestPointer{last_output_ptr};
            tuple.output_len = last_output_len;
        }
        (void)completed;
        return eng::Run2Outcome{tuple};
    }

    // Test inspection.
    [[nodiscard]] const std::vector<AllocationRecord> &host_allocations() const noexcept {
        return allocations_;
    }
    // WH-3: the raw ahfl_cap_status of every import reply the host served, in
    // call order (memo/injected replies are always AHFL_CAP_OK).
    [[nodiscard]] const std::vector<std::uint32_t> &import_reply_statuses() const noexcept {
        return import_raw_statuses_;
    }
    [[nodiscard]] std::uint32_t event_count() const noexcept {
        return kEventLogBase < memory_.size()
                   ? static_cast<std::uint32_t>(memory_[kEventLogBase]) |
                         (static_cast<std::uint32_t>(memory_[kEventLogBase + 1]) << 8) |
                         (static_cast<std::uint32_t>(memory_[kEventLogBase + 2]) << 16) |
                         (static_cast<std::uint32_t>(memory_[kEventLogBase + 3]) << 24)
                   : 0;
    }

  private:
    static constexpr std::uint32_t kScratchWindow = 4096;

    [[nodiscard]] std::uint32_t write_scratch(const std::vector<std::uint8_t> &bytes) {
        const std::uint32_t ptr = scratch_next_;
        std::copy(bytes.begin(), bytes.end(), memory_.begin() + ptr);
        scratch_next_ += static_cast<std::uint32_t>(bytes.size());
        return ptr;
    }

    FakeScript script_;
    std::vector<std::uint8_t> module_bytes_;
    std::vector<std::uint8_t> memory_;
    eng::ImportCallback callback_;
    std::uint32_t heap_base_{0};
    std::uint32_t scratch_next_{0}; // set below from heap_base at fresh_instance
    std::uint32_t host_next_{0};
    bool instantiated_{false};
    bool run_started_{false};
    std::vector<std::uint32_t> import_raw_statuses_;
    std::vector<AllocationRecord> allocations_;
};

// ---- ledger builders --------------------------------------------------------
//
// The Suspended / Injected ledger builders and slot seeding live in the SHARED
// resume_test_support.hpp (make_suspended_record / make_injected_record /
// suspended_slots); the F5 Node e2e seeds the same shape over a real emitted
// module. This TU only supplies its hand-built topologies and the scripted
// engine.

[[nodiscard]] bool is_host_failure(const host::ResumeFailure &f, host::ResumeHostReason want) {
    return std::holds_alternative<host::HostFailure>(f) &&
           std::get<host::HostFailure>(f).reason == want;
}
[[nodiscard]] bool is_step(const host::ResumeFailure &f, rc::ResumeStepReason want) {
    return std::holds_alternative<host::StepFailure>(f) &&
           std::get<host::StepFailure>(f).reason == want;
}
[[nodiscard]] bool is_prepare(const host::ResumeFailure &f, rc::ResumePrepareReason want) {
    return std::holds_alternative<host::PrepareFailure>(f) &&
           std::get<host::PrepareFailure>(f).reason == want;
}
[[nodiscard]] bool is_store(const host::ResumeFailure &f, ps::PayloadStoreError want) {
    return std::holds_alternative<host::StoreFailure>(f) &&
           std::get<host::StoreFailure>(f).error == want;
}

// Build the 4-node topology (identity + two memo caps + pending frontier).
struct Topology {
    ModuleSpec spec;
    std::vector<ManifestNodeSpec> nodes;
};
Topology topology_four_nodes() {
    Topology t;
    t.spec.entry_id = 7;
    t.spec.extra_schema_nodes = {bounded_string_node(8)};
    t.spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                   CapSpec{4, 901, CoreWireSchemaNodeId{1}},
                   CapSpec{5, 902, CoreWireSchemaNodeId{1}}};
    t.spec.nodes = {ManifestNodeSpec{40, 0, 0, 0},
                    ManifestNodeSpec{41, 1, 3, 900},
                    ManifestNodeSpec{42, 1, 4, 901},
                    ManifestNodeSpec{43, 1, 5, 902}};
    t.nodes = t.spec.nodes;
    return t;
}

Topology topology_two_nodes() {
    Topology t;
    t.spec.entry_id = 7;
    t.spec.extra_schema_nodes = {bounded_string_node(8)};
    t.spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}}};
    t.spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900}};
    t.nodes = t.spec.nodes;
    return t;
}

// Build cap script steps, each presenting the Int param "1".
std::vector<FakeCapStep> cap_steps(std::size_t count, bool trap_last = false) {
    std::vector<FakeCapStep> steps;
    for (std::size_t i = 0; i < count; ++i) {
        FakeCapStep s;
        s.import_ordinal = static_cast<std::uint32_t>(i);
        s.param_frame = bytes_of(kIntParamJson);
        s.trap_after_reply = trap_last && i == count - 1;
        steps.push_back(std::move(s));
    }
    return steps;
}

constexpr CoreWorkflowId kWf{7};
constexpr ResumeCheckpointId kCkpt{3};
const PayloadSlotId kEntrySlot{9};
const PayloadSlotId kInjectedSlot{5};

// ---- D2b-5 live-frontier fixtures ------------------------------------------

// The D2b-5 host-bound authority id for the live tests (opaque 16 bytes,
// never derived from key_id/path/hostname).
constexpr std::array<std::uint8_t, 16> kLiveAuthorityRaw = {
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
const tok::IdempotencyAuthorityId kLiveAuthorityId{kLiveAuthorityRaw};

// The after-frontier live topology: identity 40, cap 41 (the Suspended
// frontier), cap 42 (the after-frontier ReadyForLive node the live arm serves).
ModuleSpec live_module_spec() {
    ModuleSpec spec;
    spec.entry_id = 7;
    spec.extra_schema_nodes = {bounded_string_node(8)};
    spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                 CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
    spec.nodes = {ManifestNodeSpec{40, 0, 0, 0},
                  ManifestNodeSpec{41, 1, 3, 900},
                  ManifestNodeSpec{42, 1, 4, 901}};
    return spec;
}

[[nodiscard]] ahfl::support::Sha256Digest
flipped_digest(ahfl::support::Sha256Digest d) {
    d[0] = static_cast<std::uint8_t>(d[0] ^ 0xFFu);
    return d;
}
[[nodiscard]] tok::IdempotencyToken flipped_token(tok::IdempotencyToken t) {
    t.bytes[0] = static_cast<std::uint8_t>(t.bytes[0] ^ 0xFFu);
    return t;
}

// A backend wrapper that delegates every call unchanged. Subclasses override
// seal_or_load to inject a race between the controller's READ-ONLY preview
// (find_registration_at_call_site, which sees an empty site) and the driver's
// SEALING begin_effect (seal_or_load).
class DelegatingBackend : public dea::IDurableEffectBackend {
  public:
    explicit DelegatingBackend(std::shared_ptr<dea::IDurableEffectBackend> inner)
        : inner_(std::move(inner)) {}

    [[nodiscard]] std::optional<dea::SealedEffectRegistration>
    find_registration(const tok::IdempotencyToken &token) const override {
        return inner_->find_registration(token);
    }

    [[nodiscard]] std::expected<dea::SealOutcome, dea::DurableEffectBackendError>
    seal_or_load(dea::SealedEffectRegistration candidate) override {
        return inner_->seal_or_load(std::move(candidate));
    }

    [[nodiscard]] std::expected<dea::ResultHandle,
                                std::variant<dea::RegistrationConflict,
                                             dea::DurableEffectBackendError>>
    complete_pending(const tok::IdempotencyToken &token, bool succeeded,
                     std::span<const std::uint8_t> typed_payload) override {
        return inner_->complete_pending(token, succeeded, typed_payload);
    }

    [[nodiscard]] std::expected<std::vector<dea::PendingRecoveryEntry>,
                                dea::DurableEffectBackendError>
    list_pending(const dei::CheckpointNamespace &checkpoint_namespace) const override {
        return inner_->list_pending(checkpoint_namespace);
    }

    [[nodiscard]] std::expected<dea::CallSiteLookup, dea::DurableEffectBackendError>
    find_registration_at_call_site(const tok::IdempotencyAuthorityId &authority,
                                   const dei::CheckpointNamespace &checkpoint_namespace,
                                   CoreWorkflowNodeId node, InvocationOrdinal ordinal,
                                   CoreCapabilityId capability,
                                   std::uint64_t source_symbol) const override {
        return inner_->find_registration_at_call_site(authority, checkpoint_namespace, node,
                                                      ordinal, capability, source_symbol);
    }

    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                std::variant<dea::PayloadReadConflict,
                                             dea::DurableEffectBackendError>>
    read_payload(dea::ResultHandle handle) const override {
        return inner_->read_payload(handle);
    }

  protected:
    std::shared_ptr<dea::IDurableEffectBackend> inner_;
};

// On the FIRST seal_or_load, first seal a competing SUCCEEDED terminal for the
// SAME token (a concurrent caller won between the controller's preview and the
// driver's begin), so the driver's begin_effect returns EffectReplaySucceeded.
class ReplaySucceededRacingBackend final : public DelegatingBackend {
  public:
    ReplaySucceededRacingBackend(std::shared_ptr<dea::IDurableEffectBackend> inner,
                                 std::vector<std::uint8_t> competing_result)
        : DelegatingBackend(std::move(inner)),
          competing_result_(std::move(competing_result)) {}

    [[nodiscard]] std::expected<dea::SealOutcome, dea::DurableEffectBackendError>
    seal_or_load(dea::SealedEffectRegistration candidate) override {
        if (!primed_) {
            primed_ = true;
            static_cast<void>(inner_->seal_or_load(candidate));
            static_cast<void>(
                inner_->complete_pending(candidate.token, true, competing_result_));
        }
        return inner_->seal_or_load(std::move(candidate));
    }

  private:
    std::vector<std::uint8_t> competing_result_;
    bool primed_{false};
};

// On the FIRST seal_or_load, first seal a competing PENDING row (no terminal)
// for the SAME token (a concurrent caller sealed between the controller's
// preview and the driver's begin), so the driver's begin_effect returns
// EffectReplayPending. D2b-9: the driver re-drives instead of aborting.
class ReplayPendingRacingBackend final : public DelegatingBackend {
  public:
    using DelegatingBackend::DelegatingBackend;

    [[nodiscard]] std::expected<dea::SealOutcome, dea::DurableEffectBackendError>
    seal_or_load(dea::SealedEffectRegistration candidate) override {
        if (!primed_) {
            primed_ = true;
            static_cast<void>(inner_->seal_or_load(candidate));
        }
        return inner_->seal_or_load(std::move(candidate));
    }

  private:
    bool primed_{false};
};

// On the FIRST seal_or_load, first seal a DIVERGENT row (same call site, same
// authority, DIFFERENT param digest + its own token), so the driver's
// begin_effect returns EffectDiverged.
class DivergedRacingBackend final : public DelegatingBackend {
  public:
    using DelegatingBackend::DelegatingBackend;

    [[nodiscard]] std::expected<dea::SealOutcome, dea::DurableEffectBackendError>
    seal_or_load(dea::SealedEffectRegistration candidate) override {
        if (!primed_) {
            primed_ = true;
            auto diverging = candidate;
            diverging.coordinate.param_digest = flipped_digest(diverging.coordinate.param_digest);
            diverging.token = flipped_token(diverging.token);
            static_cast<void>(inner_->seal_or_load(std::move(diverging)));
        }
        return inner_->seal_or_load(std::move(candidate));
    }

  private:
    bool primed_{false};
};

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: core_wasm_resume_host_tests <work-dir>\n";
        return 2;
    }
    const fs::path base = fs::path(argv[1]);
    const auto key = test_key();
    const auto key_id = test_key_id();
    const std::span<const std::uint8_t, 16> id_span(key_id);
    const std::vector<std::uint8_t> injected = bytes_of(kStringResultJson);

    // Platform / filesystem gate: SKIP (77) off a durable Linux FS.
    {
        const fs::path probe = base / "probe";
        if (!open_store(probe).has_value()) {
            std::cerr << "SKIP: resume-host test needs a Linux durable FS\n";
            nuke(probe);
            return 77;
        }
        nuke(probe);
    }

    // ============ D2b-5 shared live-frontier setup ==========================
    // Seed the 2-node Suspended ledger (identity 40 + cap 41 frontier) for the
    // live topology at gen 1; cap 42 is the after-frontier ReadyForLive node.
    const auto seed_live_store = [&](const fs::path &work,
                                     const csm::VerifiedCoreWasmSchemaModule &mod,
                                     const ModuleSpec &spec)
        -> std::optional<ps::IntegrityPayloadStore> {
        auto store = open_store(work);
        if (!store.has_value()) {
            return std::nullopt;
        }
        const std::vector<ManifestNodeSpec> ledger_nodes{spec.nodes[0], spec.nodes[1]};
        const auto record = make_suspended_record(
            mod, kWf, ledger_nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        if (!pub0.has_value() || *pub0 != 1) {
            return std::nullopt;
        }
        return store;
    };

    // Fill the common ResumeRequest fields for a D2b-5 live resume.
    const auto fill_request = [&](host::ResumeRequest &request,
                                  const csm::VerifiedCoreWasmSchemaModule &mod,
                                  const std::vector<std::uint8_t> &module_bytes,
                                  eng::CoreWasmResumeEngine &engine,
                                  ps::IntegrityPayloadStore &store,
                                  const dea::DurableEffectAuthority *dedup,
                                  host::LiveCapabilityInvoker invoker) {
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;
        request.gated_options.authority_id = kLiveAuthorityId;
        request.gated_options.dedup_authority = dedup;
        request.live_invoker = std::move(invoker);
    };

    // Mint the effect intent for the after-frontier cap 42 call site under the
    // live authority, using the PRODUCTION helper (the same one the driver
    // uses), so a test can assert the authority's terminal state for the exact
    // token the driver sealed under.
    const auto live_intent = [&](const csm::VerifiedCoreWasmSchemaModule &mod)
        -> std::optional<dei::DurableEffectIntent> {
        auto cs = mod.resolve(csm::ManifestCallSiteIndex{1});
        if (!cs.ok() || !cs.call_site.has_value()) {
            return std::nullopt;
        }
        auto dom = ahfl::json::parse_json(kIntParamJson);
        if (!dom.has_value()) {
            return std::nullopt;
        }
        auto decoded =
            ahfl::runtime::wire_codec::decode_json(**dom, cs.call_site->param_binding());
        if (!decoded.ok()) {
            return std::nullopt;
        }
        return delm::mint_live_effect_intent(
            kLiveAuthorityId, dei::CheckpointNamespace{kWf, kCkpt}, *cs.call_site,
            *decoded.value);
    };

    // A scripted invoker that counts its calls and returns a fixed outcome.
    const auto counting_invoker = [](int &calls, host::LiveCapabilityOutcome outcome) {
        return [&calls, outcome = std::move(outcome)](
                   const rc::ReadyForLive &, const eng::ImportObservation &)
                   -> std::expected<host::LiveCapabilityOutcome, host::LiveCapabilityFault> {
            ++calls;
            return outcome;
        };
    };

    // ============ S1: full Suspended replay, memo x2 + inject, Consumed =====
    {
        const fs::path work = base / "s1-happy";
        Topology topo = topology_four_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S1.module.admits");
        if (!mod_res.ok()) {
            std::cerr << "core_wasm_resume_host: FATAL module build\n";
            return 1;
        }
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);
        check(mod.node_count() == 4 && mod.call_site_count() == 3, "S1.topology");

        auto store = open_store(work);
        check(store.has_value(), "S1.store");
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S1.gen1_published");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(3);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(done.has_value(), "S1.resume_ok");
        if (done.has_value()) {
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.raw_status == AHFL_CAP_OK, "S1.raw_ok");
            check(completed.output == injected, "S1.forwarded_output_is_injected");
            check(completed.consumed_from == 2 && completed.consumed_generation == 3,
                  "S1.consumed_3_2");
        }

        // The host performed exactly the L0 + two memo + one injected frame
        // transfers (4), and the injected transfer carried the exact bytes.
        check(engine.host_allocations().size() == 4, "S1.four_host_transfers");
        if (engine.host_allocations().size() == 4) {
            check(engine.host_allocations()[0].bytes ==
                      std::vector<std::uint8_t>(kEntryBytes.begin(), kEntryBytes.end()),
                  "S1.l0_entry_verbatim");
            check(engine.host_allocations()[1].bytes == injected, "S1.l3_memo_1");
            check(engine.host_allocations()[2].bytes == injected, "S1.l3_memo_2");
            check(engine.host_allocations()[3].bytes == injected, "S1.l4_injected");
            // Bump, no overlap, monotonic instance-lifetime pointers.
            for (std::size_t i = 1; i < engine.host_allocations().size(); ++i) {
                const auto &prev = engine.host_allocations()[i - 1];
                const auto &cur = engine.host_allocations()[i];
                check(cur.ptr >= prev.ptr + prev.bytes.size(), "S1.bump_no_overlap");
            }
        }

        // Real store round-trip ends in ResolvedConsumed{M,N}.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
              "S1.tombstone");
        if (after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after)) {
            const auto &c = std::get<ps::ResolvedConsumed>(*after);
            check(c.generation == 3 && c.consumed_generation == 2, "S1.resolved_consumed");
        }
        nuke(work);
    }

    // ============ S2: reopen of an already-Injected generation ================
    // The frontier is a ReturnMemo (its committed injected memo): no CAS, no
    // chosen slot, run2 OK -> Consumed.
    {
        const fs::path work = base / "s2-injected-restart";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S2.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S2.store");
        auto suspended = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto injected_record = make_injected_record(suspended, kInjectedSlot);
        std::vector<ps::Slot> slots = {
            ps::Slot{kEntrySlot, std::span<const std::uint8_t>(kEntryBytes)},
            ps::Slot{kInjectedSlot, std::span<const std::uint8_t>(injected)}};
        auto pub0 = store->publish_available(kWf, kCkpt, 0, injected_record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S2.gen1_injected");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.chosen_injected_slot = PayloadSlotId{PayloadSlotId::kInvalid};

        auto done = host::run_resume(request);
        check(done.has_value(), "S2.resume_ok");
        if (done.has_value()) {
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.output == injected, "S2.frontier_memo_forwarded");
            check(completed.consumed_from == 1 && completed.consumed_generation == 2,
                  "S2.consumed_2_1");
        }
        // L0 entry + the single frontier memo transfer: there was NO new CAS.
        check(engine.host_allocations().size() == 2, "S2.two_transfers_no_cas");
        nuke(work);
    }

    // ============ S3: after-frontier ReadyForLive fails closed ================
    // 3-node module (identity, cap41 frontier, cap42 after) with a 2-node
    // Suspended ledger. The injection CAS succeeds; the next import is
    // ReadyForLive, which this replay-only driver cannot serve -> abort, no
    // consume, gen 2 Injected remains Available.
    {
        const fs::path work = base / "s3-ready-live";
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0},
                      ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        const std::vector<ManifestNodeSpec> all_nodes = spec.nodes;
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S3.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto store = open_store(work);
        check(store.has_value(), "S3.store");
        const std::vector<ManifestNodeSpec> ledger_nodes{all_nodes[0], all_nodes[1]};
        const auto record = make_suspended_record(
            mod, kWf, ledger_nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S3.gen1");

        FakeScript script;
        script.nodes = all_nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S3.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::ReadyForLiveBlocked),
                  "S3.ready_for_live_blocked");
            check(host::host_code(done.error()) == "resume.transition.invalid", "S3.host_code");
        }
        // L0 + the injected L4 transfer happened; no memo transfer and no
        // second cap frame (the live result was never fabricated).
        check(engine.host_allocations().size() == 2, "S3.two_transfers_no_live_frame");
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S3.still_available");
        if (after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after)) {
            check(std::get<ps::ResolvedAvailable>(*after).generation == 2 &&
                      std::get<ps::ResolvedAvailable>(*after).record.resume_state ==
                          ResumeState::Injected,
                  "S3.gen2_injected_available");
        }
        nuke(work);
    }

    // ============ S4: CAS failure at the frontier transfers nothing ===========
    {
        const fs::path work = base / "s4-cas-failure";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S4.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S4.store");
        const auto suspended = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(suspended, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, suspended, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S4.gen1");

        // A competing winner publishes the Injected generation at the frontier
        // boundary, so the driver's CAS (expected 1) loses.
        const auto promoted = make_injected_record(suspended, kInjectedSlot);
        std::vector<ps::Slot> promoted_slots = slots;
        promoted_slots.push_back(ps::Slot{kInjectedSlot, std::span<const std::uint8_t>(injected)});

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        script.before_callback = [&](std::size_t) {
            // A SECOND store handle over the SAME live directory (never
            // open_store: that nukes). flock serializes the two publishers.
            auto competitor = ps::IntegrityPayloadStore::open(work, big_opts());
            if (competitor.has_value()) {
                auto raced = competitor->publish_available(
                    kWf, kCkpt, 1, promoted, promoted_slots, id_span, key);
                check(raced.has_value(), "S4.competitor_wins_gen2");
            }
        };
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S4.fails_closed");
        if (!done.has_value()) {
            check(is_store(done.error(), ps::PayloadStoreError::GenerationMismatch),
                  "S4.generation_mismatch");
            check(host::host_code(done.error()) == "resume.store.generation_mismatch",
                  "S4.host_code");
        }
        // Only the L0 entry transfer: the failed CAS transferred NO new frame.
        check(engine.host_allocations().size() == 1, "S4.no_transfer_on_cas_loss");
        if (engine.host_allocations().size() == 1) {
            check(engine.host_allocations()[0].bytes ==
                      std::vector<std::uint8_t>(kEntryBytes.begin(), kEntryBytes.end()),
                  "S4.only_entry_transferred");
        }
        // The live generation is the competitor's Injected gen 2, not consumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S4.available");
        if (after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after)) {
            check(std::get<ps::ResolvedAvailable>(*after).generation == 2, "S4.live_gen2");
        }
        nuke(work);
    }

    // ============ S5: trap after the injection leaves gen 2 Available =========
    {
        const fs::path work = base / "s5-trap";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S5.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S5.store");
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S5.gen1");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1, /*trap_last=*/true);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S5.fails_closed");
        if (!done.has_value()) {
            check(is_step(done.error(), rc::ResumeStepReason::ModuleTrap), "S5.trap");
            check(host::host_code(done.error()) == "resume.module.trap", "S5.host_code");
        }
        // The acknowledged injection is NOT rolled back: gen 2 Injected, still
        // Available / unconsumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S5.available");
        if (after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after)) {
            const auto &av = std::get<ps::ResolvedAvailable>(*after);
            check(av.generation == 2 && av.record.resume_state == ResumeState::Injected,
                  "S5.gen2_injected_survives");
        }
        nuke(work);
    }

    // ============ S6: F3 declared-memory contract gate (missing section) =====
    {
        Topology topo = topology_two_nodes();
        topo.spec.emit_memory_section = false;
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S6.module.admits_without_memory");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        FakeResumeEngine engine(FakeScript{});
        // No store is needed: the gate fails before open_snapshot.
        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = nullptr;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);

        auto done = host::run_resume(request);
        check(!done.has_value(), "S6.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ArtifactMemoryContractMismatch),
                  "S6.memory_contract");
            check(host::host_code(done.error()) == "resume.preflight.resource_exhausted",
                  "S6.host_code");
        }
    }

    // ============ S7: engine instantiation failure ============================
    {
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S7.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        FakeScript script;
        script.fail_instantiation = eng::EngineError::InstanceUnavailable;
        FakeResumeEngine engine(std::move(script));

        const fs::path work = base / "s7-no-instance";
        auto store = open_store(work);
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        if (store.has_value()) {
            auto seeded = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
            check(seeded.has_value(), "seed.gen1");
        }
        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S7.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::EngineFailure),
                  "S7.engine_failure");
            check(host::host_code(done.error()) == "resume.module.error", "S7.host_code");
        }
        nuke(work);
    }

    // ============ S8: L0 transfer capacity failure ===========================
    {
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S8.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        FakeScript script;
        script.fail_first_alloc = eng::EngineError::MemoryCapacityExceeded;
        FakeResumeEngine engine(std::move(script));

        const fs::path work = base / "s8-alloc";
        auto store = open_store(work);
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        if (store.has_value()) {
            auto seeded = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
            check(seeded.has_value(), "seed.gen1");
        }
        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S8.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::EngineFailure),
                  "S8.engine_failure");
            check(host::host_code(done.error()) == "resume.preflight.resource_exhausted",
                  "S8.host_code");
        }
        nuke(work);
    }

    // ============ S9: below-frontier arg_hash mismatch fails closed ===========
    {
        const fs::path work = base / "s9-arg-hash";
        Topology topo = topology_four_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S9.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S9.store");
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto seeded = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(seeded.has_value(), "S9.gen1");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(3);
        script.caps[0].param_frame = bytes_of("2"); // wrong Int param -> arg_hash
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S9.fails_closed");
        if (!done.has_value()) {
            check(is_step(done.error(), rc::ResumeStepReason::CoordinateMismatch),
                  "S9.coordinate_mismatch");
            check(host::host_code(done.error()) == "resume.coordinate.mismatch", "S9.host_code");
        }
        // Only L0 transferred; the bad-arg-hash import returned no memo frame.
        check(engine.host_allocations().size() == 1, "S9.no_memo_transfer");
        nuke(work);
    }

    // ============ S10: phase-1 digest mismatch maps to host code ===============
    {
        const fs::path work = base / "s10-digest";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S10.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S10.store");
        auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        record.module_sha256 = hex_of_digest(ArtifactDigest{});
        const auto slots = suspended_slots(
            make_suspended_record(
                mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101}),
            std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value(), "S10.gen1_tampered");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S10.fails_closed");
        if (!done.has_value()) {
            check(is_prepare(done.error(), rc::ResumePrepareReason::ModuleDigestMismatch),
                  "S10.module_digest_mismatch");
            check(host::host_code(done.error()) == "resume.digest.module_mismatch",
                  "S10.host_code");
        }
        nuke(work);
    }

    // ============ S11: module bytes not bound to the verified handle ========
    // The verified artifact was admitted over bytes A, but the request hands
    // the engine a DIFFERENT byte buffer B (swapped buffer / re-read race).
    // Every other gate inspects the handle or the stored record; the
    // module-bytes digest binding must fail closed before fresh_instance.
    {
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S11.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);
        // Bytes B: a valid fixed-page artifact, but NOT the admitted one (one
        // irrelevant custom-section byte flipped so its digest differs).
        auto swapped_bytes = module_bytes;
        swapped_bytes.back() = static_cast<std::uint8_t>(swapped_bytes.back() ^ 0xFFu);

        FakeResumeEngine engine(FakeScript{});
        // No store is needed: the binding gate fails before open_snapshot and
        // before the engine is touched.
        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(swapped_bytes);
        request.engine = &engine;
        request.store = nullptr;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);

        auto done = host::run_resume(request);
        check(!done.has_value(), "S11.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ArtifactMemoryContractMismatch),
                  "S11.module_bytes_digest_mismatch");
            check(host::host_code(done.error()) == "resume.preflight.resource_exhausted",
                  "S11.host_code");
        }
        // The swapped bytes never reached the engine.
        check(engine.host_allocations().empty(), "S11.engine_never_instantiated");
    }

    // ============ S12: post-run2 short page fails closed =====================
    // run2 returns OK but the engine port yields a span shorter than the fixed
    // 64 KiB page (partial transport read). The terminal gate must reject it
    // before the event-prefix decode / L5 output copy, and no tombstone may be
    // emitted.
    {
        const fs::path work = base / "s12-short-page";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S12.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S12.store");
        // Reopen of an already-Injected generation: no new CAS, run2 reaches
        // its OK terminal read where the truncated-page fault is injected.
        const auto suspended = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto record = make_injected_record(suspended, kInjectedSlot);
        std::vector<ps::Slot> slots = {
            ps::Slot{kEntrySlot, std::span<const std::uint8_t>(kEntryBytes)},
            ps::Slot{kInjectedSlot, std::span<const std::uint8_t>(injected)}};
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S12.gen1");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        script.truncate_whole_memory = true;
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.chosen_injected_slot = PayloadSlotId{PayloadSlotId::kInvalid};

        auto done = host::run_resume(request);
        check(!done.has_value(), "S12.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ArtifactMemoryContractMismatch),
                  "S12.short_page");
            check(host::host_code(done.error()) == "resume.preflight.resource_exhausted",
                  "S12.host_code");
        }
        // The generation was never marked consumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S12.still_available");
        nuke(work);
    }

    // ============ S13: memo/injected import replies carry raw OK ============
    // WH-3: the host's write_frame sets ImportReply.raw_status = AHFL_CAP_OK
    // explicitly (designated initializer), so every memo/injected reply the
    // engine observes carries the raw success word. The Fake records every
    // import reply's raw_status in call order.
    {
        const fs::path work = base / "s13-raw-ok";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S13.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S13.store");
        auto suspended = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto injected_record = make_injected_record(suspended, kInjectedSlot);
        std::vector<ps::Slot> slots = {
            ps::Slot{kEntrySlot, std::span<const std::uint8_t>(kEntryBytes)},
            ps::Slot{kInjectedSlot, std::span<const std::uint8_t>(injected)}};
        auto pub0 = store->publish_available(kWf, kCkpt, 0, injected_record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S13.gen1_injected");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.chosen_injected_slot = PayloadSlotId{PayloadSlotId::kInvalid};

        auto done = host::run_resume(request);
        check(done.has_value(), "S13.resume_ok");
        // The single frontier memo reply carried raw AHFL_CAP_OK.
        check(engine.import_reply_statuses().size() == 1, "S13.one_import_reply");
        if (engine.import_reply_statuses().size() == 1) {
            check(engine.import_reply_statuses()[0] == AHFL_CAP_OK, "S13.reply_raw_ok");
        }
        nuke(work);
    }

    // ============ S14: finish_run raw PENDING -> TransitionInvalid ===========
    // WH-3: the engine carries run2's raw_status verbatim; the controller's
    // finish_run classifies it. A fresh-instance replay that ends PENDING (the
    // resume entry contract requires OK) fails closed as TransitionInvalid,
    // unchanged from the pre-WH-3 behaviour.
    {
        const fs::path work = base / "s14-pending-transition-invalid";
        Topology topo = topology_two_nodes();
        auto mod_res = admit_module(topo.spec);
        check(mod_res.ok(), "S14.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(topo.spec);

        auto store = open_store(work);
        check(store.has_value(), "S14.store");
        const auto record = make_suspended_record(
            mod, kWf, topo.nodes, kEntrySlot, kIntParamJson, PayloadSlotId{101});
        const auto slots = suspended_slots(record, std::span<const std::uint8_t>(injected));
        auto pub0 = store->publish_available(kWf, kCkpt, 0, record, slots, id_span, key);
        check(pub0.has_value() && *pub0 == 1, "S14.gen1");

        FakeScript script;
        script.nodes = topo.nodes;
        script.caps = cap_steps(1);
        // The memo reply is served (raw OK), then run2 returns a raw PENDING
        // tuple: the engine never classifies, finish_run does.
        script.scripted_run2_status = AHFL_CAP_PENDING;
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        request.pack_entry_frame = make_bump_entry_packer();
        request.pack_bridge_result = make_bump_result_packer();
        request.module = &mod;
        request.module_bytes = std::span<const std::uint8_t>(module_bytes);
        request.engine = &engine;
        request.store = &*store;
        request.workflow = kWf;
        request.checkpoint = kCkpt;
        request.key_id = id_span;
        request.key = std::span<const std::uint8_t>(key);
        request.injected_result = std::span<const std::uint8_t>(injected);
        request.chosen_injected_slot = kInjectedSlot;

        auto done = host::run_resume(request);
        check(!done.has_value(), "S14.fails_closed");
        if (!done.has_value()) {
            check(is_step(done.error(), rc::ResumeStepReason::TransitionInvalid),
                  "S14.transition_invalid");
            check(host::host_code(done.error()) == "resume.transition.invalid", "S14.host_code");
        }
        // The memo reply was still served with raw OK before the PENDING run2.
        check(engine.import_reply_statuses().size() == 1 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK,
              "S14.memo_reply_raw_ok");
        nuke(work);
    }

    // ============ S15: ReadyForLive + OK seals, records, packs ==============
    // D2b-5: the after-frontier cap 42 is ReadyForLive. With a live invoker +
    // dedup authority the driver seals the effect (begin_effect) BEFORE the
    // call, invokes, records the SUCCEEDED terminal, and packs the canonical
    // result. The workflow resumes past the frontier and Consumes.
    {
        const fs::path work = base / "s15-live-ok";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S15.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S15.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S15.resume_ok");
        if (done.has_value()) {
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.raw_status == AHFL_CAP_OK, "S15.raw_ok");
            check(completed.output == bytes_of(kStringResultJson), "S15.output_is_live_result");
            check(completed.consumed_from == 2 && completed.consumed_generation == 3,
                  "S15.consumed_3_2");
        }
        check(invoker_calls == 1, "S15.invoker_called_once");
        // The authority sealed + recorded a SUCCEEDED terminal for the call.
        auto intent = live_intent(mod);
        check(intent.has_value(), "S15.intent_minted");
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectSucceeded>(*resolved),
                  "S15.authority_succeeded");
            if (resolved.has_value() &&
                std::holds_alternative<dea::EffectSucceeded>(*resolved)) {
                auto bytes =
                    dedup.read_result(std::get<dea::EffectSucceeded>(*resolved).handle);
                check(bytes.has_value() && *bytes == bytes_of(kStringResultJson),
                      "S15.recorded_bytes_roundtrip");
            }
        }
        // L0 + injected L4 + the live result frame: 3 transfers, and the live
        // reply carried raw AHFL_CAP_OK.
        check(engine.host_allocations().size() == 3, "S15.three_transfers");
        if (engine.host_allocations().size() == 3) {
            check(engine.host_allocations()[2].bytes == bytes_of(kStringResultJson),
                  "S15.live_frame_bytes");
        }
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_OK,
              "S15.replies_raw_ok");
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
              "S15.tombstone");
        nuke(work);
    }

    // ============ S16: ReadyForLive + PENDING persists a new coordinate =====
    // The invoker returns PENDING: no Result frame, no terminal recorded. The
    // Pending row stays sealed and the driver persists a new Suspended
    // coordinate (D2b-8): the gen-2 Injected record plus a new frontier node
    // for the after-frontier cap 42 call site, published at gen 3. The outcome
    // is ResumeSuspended (no output frame), not a failure.
    {
        const fs::path work = base / "s16-live-pending";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S16.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S16.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{host::LivePending{}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S16.resume_suspended");
        if (done.has_value()) {
            check(std::holds_alternative<host::ResumeSuspended>(*done),
                  "S16.outcome_suspended");
            if (std::holds_alternative<host::ResumeSuspended>(*done)) {
                const auto &suspended = std::get<host::ResumeSuspended>(*done);
                check(suspended.suspended_generation == 3, "S16.suspended_gen_3");
            }
        }
        check(invoker_calls == 1, "S16.invoker_called_once");
        // The Pending row stays sealed (no terminal recorded).
        auto intent = live_intent(mod);
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectPending>(*resolved),
                  "S16.authority_pending");
            auto rows = dedup.recover(dei::CheckpointNamespace{kWf, kCkpt});
            check(rows.has_value() && rows->size() == 1, "S16.one_pending_row");
        }
        // The new coordinate is an Available gen-3 Suspended record: 3 nodes,
        // the frontier (cap 42 / sym 901) carrying the pending entry.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S16.available_gen3");
        if (after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after)) {
            const auto &available = std::get<ps::ResolvedAvailable>(*after);
            check(available.generation == 3, "S16.gen3");
            const auto &rec = available.record;
            check(rec.resume_state == ResumeState::Suspended, "S16.record_suspended");
            check(rec.nodes.size() == 3, "S16.three_nodes");
            if (rec.nodes.size() == 3) {
                const auto &frontier = rec.nodes.back();
                check(frontier.workflow_node_id == CoreWorkflowNodeId{42},
                      "S16.frontier_node_42");
                check(frontier.pending.has_value(), "S16.frontier_pending");
                if (frontier.pending.has_value()) {
                    check(frontier.pending->capability == CoreCapabilityId{4},
                          "S16.pending_cap4");
                    check(frontier.pending->source_symbol == 901, "S16.pending_sym901");
                    check(frontier.pending->arg_hash == param_arg_hash(mod, 1, kIntParamJson),
                          "S16.pending_arg_hash");
                }
                check(frontier.memo.empty(), "S16.frontier_memo_empty");
            }
            // The unchanged admitted slot set: entry slot 9 + injected slot 5.
            check(available.slots.size() == 2, "S16.two_slots");
        }
        // The import replies: memo OK for cap 41, PENDING for cap 42.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_PENDING,
              "S16.replies_ok_then_pending");
        // L0 entry + injected L4: 2 transfers (no live result frame).
        check(engine.host_allocations().size() == 2, "S16.two_transfers");
        nuke(work);
    }

    // ============ S17: ReadyForLive + ERROR records the failure ============
    // The invoker returns ERROR: the failure bytes are recorded in the
    // authority (a future resume replays the terminal failure) and the resume
    // fails closed with LiveCapabilityError.
    {
        const fs::path work = base / "s17-live-error";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S17.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S17.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveError{std::string{"boom"}}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S17.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::LiveCapabilityError),
                  "S17.error");
            check(host::host_code(done.error()) == "resume.module.error", "S17.host_code");
        }
        check(invoker_calls == 1, "S17.invoker_called_once");
        // The failure bytes are recorded: the authority has a FAILED terminal.
        auto intent = live_intent(mod);
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() && std::holds_alternative<dea::EffectFailed>(*resolved),
                  "S17.authority_failed");
            if (resolved.has_value() &&
                std::holds_alternative<dea::EffectFailed>(*resolved)) {
                auto bytes =
                    dedup.read_result(std::get<dea::EffectFailed>(*resolved).handle);
                check(bytes.has_value() && *bytes == bytes_of("boom"),
                      "S17.failure_bytes");
            }
        }
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S17.still_available");
        nuke(work);
    }

    // ============ S18: ReadyForLive + invoker fault leaves the row sealed ==
    // The invoker returns a LiveCapabilityFault: the Pending row stays sealed
    // (the invoker faulted before a terminal existed) and the resume fails
    // closed with LiveCapabilityFault.
    {
        const fs::path work = base / "s18-live-fault";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S18.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S18.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     [&](const rc::ReadyForLive &, const eng::ImportObservation &)
                         -> std::expected<host::LiveCapabilityOutcome,
                                          host::LiveCapabilityFault> {
                         ++invoker_calls;
                         return std::unexpected(
                             host::LiveCapabilityFault::InvokerFailed);
                     });

        auto done = host::run_resume(request);
        check(!done.has_value(), "S18.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::LiveCapabilityFault),
                  "S18.fault");
            check(host::host_code(done.error()) == "resume.module.error", "S18.host_code");
        }
        check(invoker_calls == 1, "S18.invoker_called_once");
        // The Pending row stays sealed (the invoker faulted before a terminal).
        auto intent = live_intent(mod);
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectPending>(*resolved),
                  "S18.authority_pending");
        }
        nuke(work);
    }

    // ============ S19: begin_effect races to ReplaySucceeded ===============
    // A concurrent caller sealed + recorded a SUCCEEDED terminal for the SAME
    // token between the controller's read-only preview and the driver's sealing
    // begin. The driver reads the recorded result and packs it; the invoker is
    // NEVER called (no second effect).
    {
        const fs::path work = base / "s19-race-replay-succeeded";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S19.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto inner = dea::make_in_memory_durable_effect_backend();
        auto racing = std::make_shared<ReplaySucceededRacingBackend>(
            inner, bytes_of("\"cached\""));
        dea::DurableEffectAuthority dedup{racing};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S19.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S19.resume_ok");
        if (done.has_value()) {
            // The packed result is the COMPETING terminal's bytes, not the
            // invoker's (which was never called).
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.output == bytes_of("\"cached\""), "S19.output_is_cached");
        }
        check(invoker_calls == 0, "S19.invoker_not_called");
        // The replayed result was packed with a raw OK reply.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_OK,
              "S19.replay_reply_raw_ok");
        if (engine.host_allocations().size() == 3) {
            check(engine.host_allocations()[2].bytes == bytes_of("\"cached\""),
                  "S19.replayed_frame_bytes");
        }
        nuke(work);
    }

    // ============ S19b: EffectReplayPending race re-drives (D2b-9) =========
    // A concurrent caller sealed a PENDING row (no terminal) for the SAME
    // token between the controller's read-only preview and the driver's
    // sealing begin. D2b-9: the driver re-drives the live capability through
    // the SAME invoker seam and records the terminal on the EXISTING row (no
    // second effect). The invoker IS called once; the resume completes.
    {
        const fs::path work = base / "s19b-race-replay-pending";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S19b.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto inner = dea::make_in_memory_durable_effect_backend();
        auto racing = std::make_shared<ReplayPendingRacingBackend>(inner);
        dea::DurableEffectAuthority dedup{racing};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S19b.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S19b.resume_ok");
        if (done.has_value()) {
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.raw_status == AHFL_CAP_OK, "S19b.raw_ok");
            check(completed.output == bytes_of(kStringResultJson),
                  "S19b.output_is_live_result");
            check(completed.consumed_from == 2 && completed.consumed_generation == 3,
                  "S19b.consumed_3_2");
        }
        // The invoker WAS called: the re-drive happened (key D2b-9 change).
        check(invoker_calls == 1, "S19b.invoker_called_once");
        // The terminal was recorded on the EXISTING (racing) Pending row.
        auto intent = live_intent(mod);
        check(intent.has_value(), "S19b.intent_minted");
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectSucceeded>(*resolved),
                  "S19b.authority_succeeded");
            if (resolved.has_value() &&
                std::holds_alternative<dea::EffectSucceeded>(*resolved)) {
                auto bytes =
                    dedup.read_result(std::get<dea::EffectSucceeded>(*resolved).handle);
                check(bytes.has_value() && *bytes == bytes_of(kStringResultJson),
                      "S19b.recorded_bytes_roundtrip");
            }
        }
        // The live reply carried raw AHFL_CAP_OK.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_OK,
              "S19b.replies_raw_ok");
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
              "S19b.tombstone");
        nuke(work);
    }

    // ============ S20: begin_effect races to Diverged ======================
    // A concurrent caller sealed the SAME call site under the SAME authority
    // with a DIFFERENT param digest. The driver fails closed CoordinateMismatch;
    // the invoker is NEVER called.
    {
        const fs::path work = base / "s20-race-diverged";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S20.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto inner = dea::make_in_memory_durable_effect_backend();
        auto racing = std::make_shared<DivergedRacingBackend>(inner);
        dea::DurableEffectAuthority dedup{racing};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S20.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S20.fails_closed");
        if (!done.has_value()) {
            check(is_step(done.error(), rc::ResumeStepReason::CoordinateMismatch),
                  "S20.diverged");
            check(host::host_code(done.error()) == "resume.coordinate.mismatch",
                  "S20.host_code");
        }
        check(invoker_calls == 0, "S20.invoker_not_called");
        nuke(work);
    }

    // ============ S21: structural gate -- authority bound, no invoker ======
    // The dedup authority is bound (the controller consults it and issues
    // ReadyForLive) but the request carries NO live_invoker: the live frontier
    // stays fail-closed ReadyForLiveBlocked and no effect is sealed. The
    // no-dedup baseline behavior is structurally unchanged.
    {
        const fs::path work = base / "s21-no-invoker";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S21.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S21.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     host::LiveCapabilityInvoker{});

        auto done = host::run_resume(request);
        check(!done.has_value(), "S21.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ReadyForLiveBlocked),
                  "S21.ready_for_live_blocked");
            check(host::host_code(done.error()) == "resume.transition.invalid",
                  "S21.host_code");
        }
        // The driver never reached begin_effect: no effect row was sealed.
        auto rows = dedup.recover(dei::CheckpointNamespace{kWf, kCkpt});
        check(rows.has_value() && rows->empty(), "S21.no_effect_sealed");
        nuke(work);
    }

    // ============ S22: DedupReplay -- SUCCEEDED terminal replay ============
    // D2b-6: the authority already has a SUCCEEDED terminal for the
    // after-frontier cap 42 token. The controller's read-only preview issues
    // DedupReplay; the driver reads the recorded result and packs it. NO live
    // call, NO second effect; the workflow resumes past the frontier and
    // Consumes.
    {
        const fs::path work = base / "s22-dedup-replay";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S22.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S22.store");

        // Pre-seal a SUCCEEDED terminal for the EXACT token the controller's
        // preview will mint for cap 42 (same authority/namespace/call-site/param).
        auto intent = live_intent(mod);
        check(intent.has_value(), "S22.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S22.pre_sealed_new");
            auto recorded = dedup.record_result(intent->token(), bytes_of("\"cached\""));
            check(recorded.has_value(), "S22.terminal_recorded");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S22.resume_ok");
        if (done.has_value()) {
            // The packed result is the RECORDED terminal's bytes, not the
            // invoker's (which was never called).
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.raw_status == AHFL_CAP_OK, "S22.raw_ok");
            check(completed.output == bytes_of("\"cached\""), "S22.output_is_cached");
            check(completed.consumed_from == 2 && completed.consumed_generation == 3,
                  "S22.consumed_3_2");
        }
        check(invoker_calls == 0, "S22.invoker_not_called");
        // The replayed result was packed with a raw OK reply.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_OK,
              "S22.replies_raw_ok");
        // L0 + injected L4 + the replayed result frame: 3 transfers.
        check(engine.host_allocations().size() == 3, "S22.three_transfers");
        if (engine.host_allocations().size() == 3) {
            check(engine.host_allocations()[2].bytes == bytes_of("\"cached\""),
                  "S22.replayed_frame_bytes");
        }
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
              "S22.tombstone");
        nuke(work);
    }

    // ============ S23: DedupReplayFailure -- FAILED terminal replay ========
    // D2b-6: the authority already has a FAILED terminal for the after-frontier
    // cap 42 token. The controller's preview issues DedupReplayFailure; the
    // driver re-surfaces LiveCapabilityReplayedFailure. NO live call, NO
    // second effect.
    {
        const fs::path work = base / "s23-dedup-replay-failure";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S23.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S23.store");

        // Pre-seal a FAILED terminal for the exact token the controller's
        // preview will mint for cap 42.
        auto intent = live_intent(mod);
        check(intent.has_value(), "S23.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S23.pre_sealed_new");
            auto recorded = dedup.record_failure(intent->token(), bytes_of("boom"));
            check(recorded.has_value(), "S23.failure_terminal_recorded");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S23.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::LiveCapabilityReplayedFailure),
                  "S23.replayed_failure");
            check(host::host_code(done.error()) == "resume.module.error", "S23.host_code");
        }
        check(invoker_calls == 0, "S23.invoker_not_called");
        // The generation stays Available / unconsumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S23.still_available");
        nuke(work);
    }

    // ============ S24: RecoverPending re-drives LiveOk (D2b-9) =============
    // The authority holds a PENDING row for the after-frontier cap 42 token
    // (a crashed prior attempt sealed the effect but never recorded a
    // terminal). The controller's preview issues RecoverPending; D2b-9 the
    // driver re-drives the live capability through the SAME invoker seam and
    // records the SUCCEEDED terminal on the EXISTING row (no redundant
    // begin_effect). The invoker IS called once; the resume completes and
    // Consumes.
    {
        const fs::path work = base / "s24-recover-pending";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S24.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S24.store");

        // Pre-seal a PENDING row (no terminal) for the exact token the
        // controller's preview will mint for cap 42.
        auto intent = live_intent(mod);
        check(intent.has_value(), "S24.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S24.pre_sealed_pending");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S24.resume_ok");
        if (done.has_value()) {
            const auto &completed = std::get<host::ResumeCompleted>(*done);
            check(completed.raw_status == AHFL_CAP_OK, "S24.raw_ok");
            check(completed.output == bytes_of(kStringResultJson),
                  "S24.output_is_live_result");
            check(completed.consumed_from == 2 && completed.consumed_generation == 3,
                  "S24.consumed_3_2");
        }
        // The invoker WAS called: the re-drive happened (key D2b-9 change).
        check(invoker_calls == 1, "S24.invoker_called_once");
        // The terminal was recorded on the EXISTING Pending row.
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectSucceeded>(*resolved),
                  "S24.authority_succeeded");
            if (resolved.has_value() &&
                std::holds_alternative<dea::EffectSucceeded>(*resolved)) {
                auto bytes =
                    dedup.read_result(std::get<dea::EffectSucceeded>(*resolved).handle);
                check(bytes.has_value() && *bytes == bytes_of(kStringResultJson),
                      "S24.recorded_bytes_roundtrip");
            }
            // The row is terminal: no Pending rows remain.
            auto rows = dedup.recover(dei::CheckpointNamespace{kWf, kCkpt});
            check(rows.has_value() && rows->empty(), "S24.no_pending_rows");
        }
        // The live reply carried raw AHFL_CAP_OK.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_OK,
              "S24.replies_raw_ok");
        // The generation is Consumed (tombstoned).
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
              "S24.tombstone");
        nuke(work);
    }

    // ============ S24b: RecoverPending + LivePending suspends (D2b-9) =======
    // The controller's preview issues RecoverPending; the re-driven invoker
    // returns PENDING. The Pending row stays sealed and the driver persists a
    // new Suspended coordinate (D2b-8): the outcome is ResumeSuspended.
    {
        const fs::path work = base / "s24b-recover-pending-pending";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S24b.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S24b.store");

        auto intent = live_intent(mod);
        check(intent.has_value(), "S24b.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S24b.pre_sealed_pending");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{host::LivePending{}}));

        auto done = host::run_resume(request);
        check(done.has_value(), "S24b.resume_suspended");
        if (done.has_value()) {
            check(std::holds_alternative<host::ResumeSuspended>(*done),
                  "S24b.outcome_suspended");
            if (std::holds_alternative<host::ResumeSuspended>(*done)) {
                const auto &suspended = std::get<host::ResumeSuspended>(*done);
                check(suspended.suspended_generation == 3, "S24b.suspended_gen_3");
            }
        }
        check(invoker_calls == 1, "S24b.invoker_called_once");
        // The Pending row stays sealed (no terminal recorded).
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectPending>(*resolved),
                  "S24b.authority_pending");
            auto rows = dedup.recover(dei::CheckpointNamespace{kWf, kCkpt});
            check(rows.has_value() && rows->size() == 1, "S24b.one_pending_row");
        }
        // The import replies: memo OK for cap 41, PENDING for cap 42.
        check(engine.import_reply_statuses().size() == 2 &&
                  engine.import_reply_statuses()[0] == AHFL_CAP_OK &&
                  engine.import_reply_statuses()[1] == AHFL_CAP_PENDING,
              "S24b.replies_ok_then_pending");
        nuke(work);
    }

    // ============ S24c: RecoverPending + LiveError records FAILED (D2b-9) ===
    // The controller's preview issues RecoverPending; the re-driven invoker
    // returns ERROR. The failure bytes are recorded on the EXISTING Pending
    // row (a future resume sees DedupReplayFailure) and the resume fails
    // closed with LiveCapabilityError.
    {
        const fs::path work = base / "s24c-recover-pending-error";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S24c.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S24c.store");

        auto intent = live_intent(mod);
        check(intent.has_value(), "S24c.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S24c.pre_sealed_pending");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveError{std::string{"boom"}}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S24c.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(), host::ResumeHostReason::LiveCapabilityError),
                  "S24c.error");
            check(host::host_code(done.error()) == "resume.module.error", "S24c.host_code");
        }
        check(invoker_calls == 1, "S24c.invoker_called_once");
        // The failure bytes are recorded on the EXISTING row: the authority
        // has a FAILED terminal.
        if (intent.has_value()) {
            auto resolved = dedup.resolve(intent->token());
            check(resolved.has_value() &&
                      std::holds_alternative<dea::EffectFailed>(*resolved),
                  "S24c.authority_failed");
            if (resolved.has_value() &&
                std::holds_alternative<dea::EffectFailed>(*resolved)) {
                auto bytes =
                    dedup.read_result(std::get<dea::EffectFailed>(*resolved).handle);
                check(bytes.has_value() && *bytes == bytes_of("boom"),
                      "S24c.failure_bytes");
            }
        }
        // The generation stays Available / unconsumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S24c.still_available");
        nuke(work);
    }

    // ============ S24d: RecoverPending + no invoker stays blocked (D2b-9) ===
    // The controller's preview issues RecoverPending but the request carries
    // NO live_invoker: the re-drive cannot happen and the frontier stays
    // fail-closed ReadyForLiveBlocked. The Pending row stays sealed.
    {
        const fs::path work = base / "s24d-recover-pending-no-invoker";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S24d.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S24d.store");

        auto intent = live_intent(mod);
        check(intent.has_value(), "S24d.intent_minted");
        if (intent.has_value()) {
            auto begun = dedup.begin_effect(*intent);
            check(begun.has_value() && std::holds_alternative<dea::EffectNew>(*begun),
                  "S24d.pre_sealed_pending");
        }

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     host::LiveCapabilityInvoker{});

        auto done = host::run_resume(request);
        check(!done.has_value(), "S24d.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ReadyForLiveBlocked),
                  "S24d.ready_for_live_blocked");
            check(host::host_code(done.error()) == "resume.transition.invalid",
                  "S24d.host_code");
        }
        // The Pending row stays sealed (no invoker, no re-drive).
        if (intent.has_value()) {
            auto rows = dedup.recover(dei::CheckpointNamespace{kWf, kCkpt});
            check(rows.has_value() && rows->size() == 1, "S24d.one_pending_row");
        }
        // The generation stays Available / unconsumed.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S24d.still_available");
        nuke(work);
    }

    // ============ S25: null authority -- no-dedup baseline =================
    // With no dedup authority bound the controller always issues ReadyForLive
    // (it can never issue a replay verdict). With an invoker but no authority
    // to seal under, the driver fails closed ReadyForLiveBlocked: the no-dedup
    // baseline is structurally unchanged.
    {
        const fs::path work = base / "s25-null-authority";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S25.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S25.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, /*dedup=*/nullptr,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{
                                          host::LiveOk{bytes_of(kStringResultJson)}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S25.fails_closed");
        if (!done.has_value()) {
            check(is_host_failure(done.error(),
                                  host::ResumeHostReason::ReadyForLiveBlocked),
                  "S25.ready_for_live_blocked");
            check(host::host_code(done.error()) == "resume.transition.invalid",
                  "S25.host_code");
        }
        check(invoker_calls == 0, "S25.invoker_not_called");
        nuke(work);
    }

    // ============ S26: D2b-8 pending publish CAS failure ===================
    // A competitor publishes gen 3 between the live PENDING call and the
    // driver's pending-coordinate publish. The driver's CAS (expected 2) fails
    // GenerationMismatch: the resume fails closed, the competitor's gen 3 stays
    // the live generation (the driver's coordinate was never published), and
    // the Pending row stays sealed.
    {
        const fs::path work = base / "s26-pending-cas";
        ModuleSpec spec = live_module_spec();
        auto mod_res = admit_module(spec);
        check(mod_res.ok(), "S26.module.admits");
        const csm::VerifiedCoreWasmSchemaModule mod = *mod_res.module;
        const auto module_bytes = build_module(spec);

        auto backend = dea::make_in_memory_durable_effect_backend();
        dea::DurableEffectAuthority dedup{backend};
        auto store = seed_live_store(work, mod, spec);
        check(store.has_value(), "S26.store");

        FakeScript script;
        script.nodes = spec.nodes;
        script.caps = cap_steps(2);
        script.before_callback = [&](std::size_t cap_index) {
            if (cap_index != 1 || !store.has_value()) {
                return;
            }
            // Race the driver's pending publish: re-publish the gen-2 Injected
            // record as gen 3 so the driver's CAS (expected 2) fails.
            auto live = store->load(kWf, kCkpt, id_span, key);
            if (!live.has_value() || !std::holds_alternative<ps::ResolvedAvailable>(*live)) {
                return;
            }
            const auto &available = std::get<ps::ResolvedAvailable>(*live);
            std::vector<ps::Slot> slots;
            slots.reserve(available.slots.size());
            for (const auto &s : available.slots) {
                slots.push_back(ps::Slot{s.slot, std::span<const std::uint8_t>(s.payload)});
            }
            auto raced = store->publish_available(kWf, kCkpt, available.generation,
                                                  available.record, slots, id_span, key);
            check(raced.has_value() && *raced == 3, "S26.competitor_published_gen3");
        };
        FakeResumeEngine engine(std::move(script));

        int invoker_calls = 0;
        host::ResumeRequest request;
        fill_request(request, mod, module_bytes, engine, *store, &dedup,
                     counting_invoker(invoker_calls,
                                      host::LiveCapabilityOutcome{host::LivePending{}}));

        auto done = host::run_resume(request);
        check(!done.has_value(), "S26.fails_closed");
        if (!done.has_value()) {
            check(is_store(done.error(), ps::PayloadStoreError::GenerationMismatch),
                  "S26.generation_mismatch");
        }
        check(invoker_calls == 1, "S26.invoker_called_once");
        // The competitor's gen 3 is the live generation; the driver's pending
        // coordinate was never published.
        auto after = store->load(kWf, kCkpt, id_span, key);
        check(after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after),
              "S26.still_available");
        if (after.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*after)) {
            check(std::get<ps::ResolvedAvailable>(*after).generation == 3, "S26.live_gen3");
        }
        nuke(work);
    }

    if (g_failures != 0) {
        std::cerr << "core_wasm_resume_host: " << g_failures << " of " << g_total
                  << " check(s) failed\n";
        return 1;
    }
    std::cout << "core_wasm_resume_host: all " << g_total << " checks passed\n";
    return 0;
}
