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
#include "runtime/engine/core_wasm_node_events.hpp"
#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_resume_host.hpp"

namespace {

using namespace ahfl::runtime::resume_test_support;

namespace eng = ahfl::runtime::core_wasm_resume_engine;
namespace host = ahfl::runtime::core_wasm_resume_host;
namespace nev = ahfl::runtime::core_wasm_node_events;
namespace ps = ahfl::runtime::payload_store;
namespace rc = ahfl::runtime::core_wasm_resume_controller;

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
        return std::span<const std::uint8_t>(memory_);
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
            observation.param_frame =
                std::span<const std::uint8_t>(memory_.data() + param_ptr, step.param_frame.size());
            observation.whole_memory = std::span<const std::uint8_t>(memory_);
            auto reply = callback_(observation);
            if (std::holds_alternative<eng::ImportAbort>(reply)) {
                // The host failed closed; the engine unwinds without resuming.
                return eng::Run2Outcome{eng::Run2HostAborted{}};
            }
            const auto &frame = std::get<eng::ImportReply>(reply);
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
        tuple.raw_status = AHFL_CAP_OK;
        tuple.output_ptr = eng::GuestPointer{last_output_ptr};
        tuple.output_len = last_output_len;
        (void)completed;
        return eng::Run2Outcome{tuple};
    }

    // Test inspection.
    [[nodiscard]] const std::vector<AllocationRecord> &host_allocations() const noexcept {
        return allocations_;
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
            check(done->raw_status == AHFL_CAP_OK, "S1.raw_ok");
            check(done->output == injected, "S1.forwarded_output_is_injected");
            check(done->consumed_from == 2 && done->consumed_generation == 3, "S1.consumed_3_2");
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
            check(done->output == injected, "S2.frontier_memo_forwarded");
            check(done->consumed_from == 1 && done->consumed_generation == 2, "S2.consumed_2_1");
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

    if (g_failures != 0) {
        std::cerr << "core_wasm_resume_host: " << g_failures << " of " << g_total
                  << " check(s) failed\n";
        return 1;
    }
    std::cout << "core_wasm_resume_host: all " << g_total << " checks passed\n";
    return 0;
}
