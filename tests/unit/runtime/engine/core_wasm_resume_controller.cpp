// RFC 0026 KR6.5 E4-B2-D1b permanent regression for the host-independent durable-resume
// replay controller. Hand-rolled check()/main(); NO gtest. Links PRIVATE only against
// ahfl_runtime_engine. Module bytes are HAND-BUILT (no emitter -- the same canonical
// two-section fixture cluster the A2 test uses), so the controller's digest / coordinate
// gates run over fully-controlled topology, and the durable evidence uses a REAL
// IntegrityPayloadStore (SKIP 77 off a Linux durable filesystem).
//
// FOUNDATION only: a DECISION-ONLY controller. It never touches a Wasm VM, allocates
// linear memory, transfers a frame, authenticates an artifact, writes the store, or does
// a CAS. The test drives publish / mark_consumed and feeds the controller the store's
// real result. NOT B2 real-Wasm durable resume; B2 / KR6.5 stay false.
//
// Evidence groups (see the per-group names below): (A) full happy Suspended -> publish ->
// ACK -> finish -> consume over a real store; (B) phase-1 digest + A2-baseline
// negatives, each proven to fail BEFORE phase-2 slot admission; (C) phase-2 eligibility
// priority (schema-invalid memo, wrong-key store error, Injected+nonempty); (D) TOTAL
// two-pass (Unbounded both orders, SizeOverflow, u32 boundary, capacity fit/+1); (E)
// per-import / event-join branches; (F) publish / ACK states + span stability; (G)
// terminal / consume incl raw ABI status classification + real Consumed{M,N}.


// The module/event/store fixture builders live in the SHARED test-support header
// (promoted by D2a-F4 so the production resume-host driver and Node e2e reuse them
// without duplication).
#include "resume_test_support.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using namespace ahfl::runtime::resume_test_support;

namespace rc = ahfl::runtime::core_wasm_resume_controller;
namespace ps = ahfl::runtime::payload_store;
namespace csm = ahfl::runtime::core_wasm_schema_module;

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWireSchemaNode;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_resume::CoreWasmResumeRecord;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::core_wasm_resume::NodeKind;
using ahfl::runtime::core_wasm_resume::PayloadSlotId;
using ahfl::runtime::core_wasm_resume::ResumeMemoEntry;
using ahfl::runtime::core_wasm_resume::ResumeNode;
using ahfl::runtime::core_wasm_resume::ResumePendingEntry;
using ahfl::runtime::core_wasm_resume::ResumeState;
using ahfl::runtime::core_wasm_schema_module::VerifiedCoreWasmSchemaModule;
using ahfl::runtime::payload_store::ResumeCheckpointId;
using ahfl::runtime::durable_effect_authority::DurableEffectAuthority;
using ahfl::runtime::durable_effect_authority::IDurableEffectBackend;
using ahfl::runtime::durable_effect_authority::make_in_memory_durable_effect_backend;
using ahfl::runtime::durable_effect_intent::FrozenAuthorityNamespaceBuilder;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyAuthorityId;
using ahfl::support::Sha256Digest;

namespace fs = std::filesystem;

namespace dea = ahfl::runtime::durable_effect_authority;
namespace dei = ahfl::runtime::durable_effect_intent;
namespace tok = ahfl::runtime::core_wasm_idempotency_token;

// ---- raw ABI status pinned to the SSOT (not magic numbers) ------------------
static_assert(AHFL_CAP_OK == 0u, "OK must be 0");
static_assert(AHFL_CAP_ERROR == 1u, "ERROR must be 1");
static_assert(AHFL_CAP_PENDING == 2u, "PENDING must be 2");

// ---- opaque-handle type discipline ------------------------------------------
static_assert(!std::is_copy_constructible_v<rc::PreparedResume>, "PreparedResume move-only");
static_assert(std::is_move_constructible_v<rc::PreparedResume>, "PreparedResume movable");
static_assert(!std::is_copy_constructible_v<rc::GatedResume>, "GatedResume move-only");
static_assert(!std::is_copy_constructible_v<rc::PendingInjection>, "PendingInjection move-only");
// Closed variant sizes (no accidental extra arms).
static_assert(std::variant_size_v<rc::AdmitOutcome> == 2, "AdmitOutcome has 2 arms");
static_assert(std::variant_size_v<rc::ImportStepDecision> == 6,
              "ImportStepDecision has 6 arms (ReturnMemo/NeedInjectedSlot/ReadyForLive + "
              "D2b-4 DedupReplay/DedupReplayFailure/RecoverPending)");
static_assert(std::variant_size_v<rc::Run2Exit> == 2, "Run2Exit has 2 arms");
// Reason enums are compact (u8), carry no bytes.
static_assert(sizeof(rc::ResumePrepareReason) == 1, "PrepareReason u8");
static_assert(sizeof(rc::ResumeStepReason) == 1, "StepReason u8");

int g_failures = 0;
int g_total = 0;

void check(bool ok, std::string_view name) {
    ++g_total;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// D2b-4: a delegating durable-effect backend whose READ-ONLY call-site lookup
// always reports a storage fault, so the controller's fail-closed arm is proven
// against a real backend error (not a synthetic variant literal).
class FaultLookupBackend final : public IDurableEffectBackend {
  public:
    explicit FaultLookupBackend(std::shared_ptr<IDurableEffectBackend> inner)
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
        // The read the D2b-4 controller performs always fails here.
        (void)authority;
        (void)checkpoint_namespace;
        (void)node;
        (void)ordinal;
        (void)capability;
        (void)source_symbol;
        return std::unexpected(dea::DurableEffectBackendError::StorageUnavailable);
    }
    [[nodiscard]] std::expected<std::vector<std::uint8_t>,
                                std::variant<dea::PayloadReadConflict,
                                             dea::DurableEffectBackendError>>
    read_payload(dea::ResultHandle handle) const override {
        return inner_->read_payload(handle);
    }

  private:
    std::shared_ptr<IDurableEffectBackend> inner_;
};

} // namespace


int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: core_wasm_resume_controller_tests <work-dir>\n";
        return 2;
    }
    const fs::path base = fs::path(argv[1]);
    const auto key = test_key();
    const auto key_id = test_key_id();
    const std::span<const std::uint8_t, 16> id_span(key_id);
    const CoreWorkflowId wf{7};
    const ResumeCheckpointId ckpt{3};
    const rc::LinearMemoryCapacityBytes cap{kPageBytes};

    // Platform / filesystem gate: SKIP (77) where the store cannot open.
    {
        const fs::path probe = base / "probe";
        auto s = open_store(probe);
        if (!s.has_value()) {
            std::cerr << "SKIP: durable-resume controller test needs a Linux durable FS\n";
            nuke(probe);
            return 77;
        }
        nuke(probe);
    }

    // ============ shared 2-node module: identity 40, capability 41 (cap 3, sym 900) ===
    // Schema: Int param (node 0) + bounded String result (node 1, max 8). One cap.
    ModuleSpec base_spec;
    base_spec.entry_id = 7;
    base_spec.extra_schema_nodes = {bounded_string_node(8)};
    base_spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}}};
    base_spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900}};
    const auto base_nodes = base_spec.nodes;
    const auto module_bytes = build_module(base_spec);
    auto mod_res =
        make_verified_core_wasm_schema_module(std::span<const std::uint8_t>(module_bytes));
    check(mod_res.ok(), "module.admits");
    if (!mod_res.ok()) {
        std::cerr << "core_wasm_resume_controller: FATAL module build\n";
        return 1;
    }
    const VerifiedCoreWasmSchemaModule mod = *mod_res.module;
    check(mod.node_count() == 2 && mod.call_site_count() == 1, "module.topology_2_1");

    const std::uint64_t arg_hash = param_arg_hash(mod, 0, kIntParamJson);
    const auto import_ord0 = mod.resolve(csm::ManifestCallSiteIndex{0}).call_site->import_ordinal();
    const std::vector<std::uint8_t> param_frame(kIntParamJson.begin(), kIntParamJson.end());
    const std::vector<std::uint8_t> injected(kStringResultJson.begin(), kStringResultJson.end());
    const std::vector<std::uint8_t> mem1 = event_memory(base_nodes, 1); // identity published
    const std::vector<std::uint8_t> mem2 = event_memory(base_nodes, 2); // both published

    // Build a Suspended record for the shared module (frontier node 41).
    const auto make_suspended = [&](std::uint64_t pending_arg_hash) {
        CoreWasmResumeRecord r;
        r.format_version = 1;
        set_matching_digests(r, mod);
        r.entry_id = wf;
        r.entry_input_slot = PayloadSlotId{9};
        r.suspended_node_id = CoreWorkflowNodeId{41};
        r.resume_state = ResumeState::Suspended;
        ResumeNode n0;
        n0.workflow_node_id = CoreWorkflowNodeId{40};
        n0.schedule_pos = 0;
        n0.node_kind = NodeKind::Identity;
        ResumeNode n1;
        n1.workflow_node_id = CoreWorkflowNodeId{41};
        n1.schedule_pos = 1;
        n1.node_kind = NodeKind::Capability;
        ResumePendingEntry p;
        p.invocation_ordinal = InvocationOrdinal{0};
        p.capability = CoreCapabilityId{3};
        p.source_symbol = 900;
        p.arg_hash = pending_arg_hash;
        n1.pending = p;
        r.nodes.push_back(n0);
        r.nodes.push_back(n1);
        return r;
    };
    const std::vector<ps::Slot> entry_only_slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes}};

    // Publish a Suspended record + open a gated resume. Returns the GatedResume.
    // D2b-4: an optional GatedResumeOptions threads the dedup authority binding.
    const auto publish_and_gate =
        [&](ps::IntegrityPayloadStore &store,
            const CoreWasmResumeRecord &rec, const std::vector<ps::Slot> &slots,
            const VerifiedCoreWasmSchemaModule &m,
            rc::GatedResumeOptions options = rc::GatedResumeOptions{})
        -> std::optional<std::expected<rc::GatedResume, rc::ResumePrepareError>> {
        if (!store.publish_available(wf, ckpt, 0, rec, slots, id_span, key).has_value()) {
            return std::nullopt;
        }
        auto snap = store.open_snapshot(wf, ckpt, id_span, key);
        if (!snap.has_value()) {
            return std::nullopt;
        }
        return rc::open_gated_resume(m, std::move(*snap), options);
    };

    // Full admit->supply into a PreparedResume for the shared module (Suspended->inject).
    const auto make_prepared =
        [&](const fs::path &work, std::optional<ps::IntegrityPayloadStore> &store_out)
        -> std::optional<rc::PreparedResume> {
        store_out = open_store(work);
        if (!store_out.has_value()) {
            return std::nullopt;
        }
        auto gated = publish_and_gate(*store_out, make_suspended(arg_hash), entry_only_slots, mod);
        if (!gated.has_value() || !gated->has_value()) {
            return std::nullopt;
        }
        auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                               std::span<const std::uint8_t>(), cap);
        if (!outcome.has_value() || !std::holds_alternative<rc::PendingInjection>(*outcome)) {
            return std::nullopt;
        }
        auto prepared = rc::supply_injected_result(
            std::get<rc::PendingInjection>(std::move(*outcome)),
            std::span<const std::uint8_t>(injected));
        if (!prepared.has_value()) {
            return std::nullopt;
        }
        return std::move(*prepared);
    };

    // ================= GROUP A: full happy path over a real store =================
    {
        const fs::path work = base / "a-happy";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "A.prepared");
        if (prepared.has_value()) {
            // EntryFrame view + byte-stability capture.
            auto frame = prepared->entry_frame();
            check(frame.slot() == PayloadSlotId{9} &&
                      std::vector<std::uint8_t>(frame.bytes().begin(), frame.bytes().end()) ==
                          kEntryBytes,
                  "A.entry_frame_verbatim");
            const std::uint8_t *entry_data0 = frame.bytes().data();

            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            auto dec = rc::next_import(*prepared, in);
            check(dec.has_value() && std::holds_alternative<rc::NeedInjectedSlot>(*dec),
                  "A.frontier_needs_slot");

            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            check(plan.has_value(), "A.bind_plan");
            if (plan.has_value()) {
                check(plan->expected_generation() == 1, "A.plan_gen1");
                check(plan->record().resume_state == ResumeState::Injected, "A.plan_injected");
                // Exact promoted-record fields: frontier node 41 gains one ordinal-0 memo bound
                // to the chosen slot 5, and its pending is cleared.
                const auto &frec = plan->record().nodes.back();
                check(!frec.pending.has_value() && frec.memo.size() == 1 &&
                          frec.memo[0].invocation_ordinal == InvocationOrdinal{0} &&
                          frec.memo[0].capability == CoreCapabilityId{3} &&
                          frec.memo[0].source_symbol == 900 &&
                          frec.memo[0].arg_hash == arg_hash &&
                          frec.memo[0].result_slot == PayloadSlotId{5},
                      "A.plan_promoted_frontier_memo");
                // Exact DISTINCT slot set {entry 9, injected 5} with exact payloads.
                check(plan->slots().size() == 2, "A.plan_two_slots");
                bool saw_entry = false, saw_injected = false;
                for (const auto &s : plan->slots()) {
                    if (s.slot == PayloadSlotId{9}) {
                        saw_entry = std::vector<std::uint8_t>(s.payload.begin(), s.payload.end()) ==
                                    kEntryBytes;
                    } else if (s.slot == PayloadSlotId{5}) {
                        saw_injected =
                            std::vector<std::uint8_t>(s.payload.begin(), s.payload.end()) ==
                                injected;
                    }
                }
                check(saw_entry && saw_injected, "A.plan_exact_slot_ids_and_payloads");
                // Ownership pin: while the plan is live, the EntryFrame view and the plan's
                // entry-slot (id 9) payload MUST be the SAME underlying buffer (the sole
                // admitted ResolvedSlot owner) -- identical .data(), not merely equal bytes.
                {
                    const std::uint8_t *frame_data = prepared->entry_frame().bytes().data();
                    const std::uint8_t *plan_entry_data = nullptr;
                    std::size_t plan_entry_size = 0;
                    for (const auto &s : plan->slots()) {
                        if (s.slot == PayloadSlotId{9}) {
                            plan_entry_data = s.payload.data();
                            plan_entry_size = s.payload.size();
                        }
                    }
                    check(plan_entry_data != nullptr && frame_data == plan_entry_data &&
                              prepared->entry_frame().bytes().size() == plan_entry_size,
                          "A.entry_frame_shares_admitted_owner_with_plan");
                }
                auto pub = store->publish_available(wf, ckpt, plan->expected_generation(),
                                                    plan->record(), plan->slots(), id_span, key);
                check(pub.has_value() && *pub == 2, "A.publish_injected_gen2");
                auto injected_span = rc::ack_publish_injected(*prepared, pub);
                check(injected_span.has_value(), "A.ack_publish");
                if (injected_span.has_value()) {
                    check(std::vector<std::uint8_t>(injected_span->begin(), injected_span->end()) ==
                              injected,
                          "A.ack_returns_injected_bytes");
                }
                auto loaded = store->load(wf, ckpt, id_span, key);
                check(loaded.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*loaded),
                      "A.reload_available");
                if (loaded.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*loaded)) {
                    const auto &av = std::get<ps::ResolvedAvailable>(*loaded);
                    check(av.record.resume_state == ResumeState::Injected && av.slots.size() == 2,
                          "A.injected_record_two_slots");
                }
            }

            // EntryFrame span stayed byte-stable across bind/ack transitions.
            auto frame2 = prepared->entry_frame();
            check(frame2.bytes().data() == entry_data0 &&
                      std::vector<std::uint8_t>(frame2.bytes().begin(), frame2.bytes().end()) ==
                          kEntryBytes,
                  "A.entry_frame_stable_after_transitions");

            // finish OK (cursor advanced past the only call site) -> consume.
            rc::Run2Returned ret;
            ret.raw_status = AHFL_CAP_OK;
            ret.whole_linear_memory = std::span<const std::uint8_t>(mem2);
            auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
            check(mc.has_value() && mc->expected_generation() == 2, "A.finish_ok");
            if (mc.has_value()) {
                auto cons = store->mark_consumed(wf, ckpt, mc->expected_generation(), id_span, key);
                check(cons.has_value() && *cons == 3, "A.mark_consumed_gen3");
                auto ack = rc::ack_mark_consumed(*prepared, cons);
                check(ack.has_value(), "A.ack_consumed");
                auto after = store->load(wf, ckpt, id_span, key);
                check(after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after),
                      "A.now_consumed");
                if (after.has_value() && std::holds_alternative<ps::ResolvedConsumed>(*after)) {
                    const auto &c = std::get<ps::ResolvedConsumed>(*after);
                    check(c.generation == 3 && c.consumed_generation == 2, "A.consumed_3_2");
                }
                // Duplicate consume ACK -> TransitionInvalid + Failed.
                auto dup = rc::ack_mark_consumed(
                    *prepared, std::expected<std::uint64_t, ps::PayloadStoreError>(3));
                check(!dup.has_value() &&
                          is_step_reason(dup.error(), rc::ResumeStepReason::TransitionInvalid),
                      "A.duplicate_consume_ack_invalid");
            }
        }
        nuke(work);
    }

    // ================= GROUP B: phase-1 digest + coordinate negatives ==============
    // Each proves failure at open_gated_resume (phase 1), i.e. before slot admission.
    {
        // B.digest priority: corrupt subsets and assert the typed arm by priority.
        struct DigestCase {
            bool bad_module, bad_wire, bad_manifest;
            rc::ResumePrepareReason want;
            std::string name;
        };
        const DigestCase cases[] = {
            {true, true, true, rc::ResumePrepareReason::ModuleDigestMismatch,
             "B.digest_all_module"},
            {false, true, true, rc::ResumePrepareReason::WireSchemaDigestMismatch, "B.digest_wire"},
            {false, false, true, rc::ResumePrepareReason::ExecManifestDigestMismatch,
             "B.digest_exec"},
        };
        for (const auto &c : cases) {
            const fs::path work = base / ("b-" + c.name);
            auto store = open_store(work);
            check(store.has_value(), c.name + ".store");
            if (store.has_value()) {
                CoreWasmResumeRecord rec = make_suspended(arg_hash);
                if (c.bad_module) {
                    rec.module_sha256 = hex_of_digest(ArtifactDigest{});
                }
                if (c.bad_wire) {
                    rec.wire_schema_sha256 = hex_of_digest(ArtifactDigest{});
                }
                if (c.bad_manifest) {
                    rec.exec_manifest_sha256 = hex_of_digest(ArtifactDigest{});
                }
                auto gated = publish_and_gate(*store, rec, entry_only_slots, mod);
                check(gated.has_value() && !gated->has_value() &&
                          is_prepare_reason(gated->error(), c.want),
                      c.name);
            }
            nuke(work);
        }
    }
    {
        // B-A1: records that violate an A1 record-internal invariant are UPSTREAM-owned and
        // impossible at D1b entry. The store's publish_available encodes the record through
        // core_wasm_resume::encode_and_authenticate, which runs the full A1 validation, so
        // an A1-invalid record is rejected at publish (Malformed) and NEVER reaches the
        // controller. (The A1 invariant set itself is covered by the core_wasm_resume_record
        // regression; here we only assert the store gate rejects, and we do NOT claim any
        // controller verdict.)
        const auto a1_reject_case =
            [&](const std::string &name,
                const std::function<void(CoreWasmResumeRecord &)> &mutate) {
                const fs::path work = base / ("b-a1-" + name);
                auto store = open_store(work);
                check(store.has_value(), "B_A1." + name + ".store");
                if (store.has_value()) {
                    CoreWasmResumeRecord rec = make_suspended(arg_hash);
                    mutate(rec);
                    auto pub = store->publish_available(wf, ckpt, 0, rec, entry_only_slots, id_span,
                                                        key);
                    check(!pub.has_value() && pub.error() == ps::PayloadStoreError::Malformed,
                          "B_A1." + name + "_rejected_at_publish");
                }
                nuke(work);
            };
        // WH-5b.2: an identity node carrying a memo is now A1-valid (the
        // record-level check cannot distinguish a P6 bridge node from a pure
        // identity node; the coordinate gate enforces the distinction). This
        // case is covered by the B-D1b block below as "identity_with_memo".
        // Suspended frontier pending ordinal != frontier.memo.size() (0): overlap invariant.
        a1_reject_case("frontier_nonzero_pending_ordinal", [&](CoreWasmResumeRecord &r) {
            r.nodes.back().pending->invocation_ordinal = InvocationOrdinal{1};
        });
        // Suspended frontier carrying BOTH a memo and a pending (A1: Suspended frontier memo
        // is empty; pending.ordinal must equal memo.size()).
        a1_reject_case("suspended_frontier_memo_and_pending", [&](CoreWasmResumeRecord &r) {
            ResumeMemoEntry m;
            m.invocation_ordinal = InvocationOrdinal{0};
            m.capability = CoreCapabilityId{3};
            m.source_symbol = 900;
            m.arg_hash = 0;
            m.result_slot = PayloadSlotId{5};
            r.nodes.back().memo.push_back(m); // now memo.size()==1 but pending.ordinal==0
        });
    }
    {
        // B-D1b: records that are A1-VALID (publishable) but disagree with the compiled
        // module/manifest -- these MUST be rejected by the controller's coordinate gate at
        // open_gated_resume (phase 1, before slot admission). Each mutation keeps the record
        // A1-internally consistent while breaking the module cross-check.
        const auto d1b_coord_case =
            [&](const std::string &name, const VerifiedCoreWasmSchemaModule &m,
                const CoreWasmResumeRecord &rec, const std::vector<ps::Slot> &slots) {
                const fs::path work = base / ("b-d1b-" + name);
                auto store = open_store(work);
                check(store.has_value(), "B_D1b." + name + ".store");
                if (store.has_value()) {
                    // The record is A1-valid, so publish MUST succeed (proves owner split).
                    auto pub = store->publish_available(wf, ckpt, 0, rec, slots, id_span, key);
                    check(pub.has_value(), "B_D1b." + name + "_publishes");
                    if (pub.has_value()) {
                        auto snap = store->open_snapshot(wf, ckpt, id_span, key);
                        check(snap.has_value(), "B_D1b." + name + "_snapshot");
                        if (snap.has_value()) {
                            auto gated = rc::open_gated_resume(m, std::move(*snap));
                            check(!gated.has_value() &&
                                      is_prepare_reason(
                                          gated.error(),
                                          rc::ResumePrepareReason::CoordinateMismatch),
                                  "B_D1b." + name);
                        }
                    }
                }
                nuke(work);
            };
        // wrong node workflow_node_id vs manifest (A1 does not cross-check the manifest).
        {
            CoreWasmResumeRecord r = make_suspended(arg_hash);
            r.nodes[0].workflow_node_id = CoreWorkflowNodeId{4040};
            d1b_coord_case("wrong_node_id", mod, r, entry_only_slots);
        }
        // frontier capability mismatch vs the A2 call site.
        {
            CoreWasmResumeRecord r = make_suspended(arg_hash);
            r.nodes.back().pending->capability = CoreCapabilityId{99};
            d1b_coord_case("frontier_cap_mismatch", mod, r, entry_only_slots);
        }
        // frontier source_symbol mismatch vs the A2 call site.
        {
            CoreWasmResumeRecord r = make_suspended(arg_hash);
            r.nodes.back().pending->source_symbol = 111;
            d1b_coord_case("frontier_source_mismatch", mod, r, entry_only_slots);
        }
        // WH-5b.2: a pure identity node (not a P6 bridge node) carrying a memo
        // is A1-valid but coordinate-invalid. The coordinate gate rejects it
        // because the manifest node has cap_call_count 0 + no bridge sites.
        // The memo's result_slot (5) must be in the slots set for publish.
        {
            CoreWasmResumeRecord r = make_suspended(arg_hash);
            ResumeMemoEntry m;
            m.invocation_ordinal = InvocationOrdinal{0};
            m.capability = CoreCapabilityId{0};
            m.source_symbol = 0;
            m.arg_hash = 0;
            m.result_slot = PayloadSlotId{5};
            r.nodes[0].memo.push_back(m);
            const std::vector<ps::Slot> slots_with_memo = {
                ps::Slot{PayloadSlotId{9}, kEntryBytes},
                ps::Slot{PayloadSlotId{5}, kEntryBytes}};
            d1b_coord_case("identity_with_memo", mod, r, slots_with_memo);
        }
        // prefix longer than the module node count: build a 3-node A1-valid record but run
        // it against the 2-node shared module. entry_id/digests must match the 2-node module,
        // so the record's coordinates diverge -> CoordinateMismatch (prefix>node_count is one
        // of the first checks). A1 validity: identity(40) + cap(41 memo) + cap(42 pending).
        {
            CoreWasmResumeRecord r;
            r.format_version = 1;
            set_matching_digests(r, mod); // digests of the 2-node module (so phase-1 digest ok)
            r.entry_id = wf;
            r.entry_input_slot = PayloadSlotId{9};
            r.suspended_node_id = CoreWorkflowNodeId{42};
            r.resume_state = ResumeState::Suspended;
            ResumeNode n0;
            n0.workflow_node_id = CoreWorkflowNodeId{40};
            n0.schedule_pos = 0;
            n0.node_kind = NodeKind::Identity;
            ResumeNode n1;
            n1.workflow_node_id = CoreWorkflowNodeId{41};
            n1.schedule_pos = 1;
            n1.node_kind = NodeKind::Capability;
            ResumeMemoEntry m1;
            m1.invocation_ordinal = InvocationOrdinal{0};
            m1.capability = CoreCapabilityId{3};
            m1.source_symbol = 900;
            m1.arg_hash = 0;
            m1.result_slot = PayloadSlotId{5};
            n1.memo.push_back(m1);
            ResumeNode n2;
            n2.workflow_node_id = CoreWorkflowNodeId{42};
            n2.schedule_pos = 2;
            n2.node_kind = NodeKind::Capability;
            ResumePendingEntry p;
            p.invocation_ordinal = InvocationOrdinal{0};
            p.capability = CoreCapabilityId{4};
            p.source_symbol = 901;
            p.arg_hash = 0;
            n2.pending = p;
            r.nodes = {n0, n1, n2};
            std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                           ps::Slot{PayloadSlotId{5}, injected}};
            d1b_coord_case("prefix_exceeds_node_count", mod, r, slots);
        }
        // A 3-node module (identity 40, cap 41, frontier cap 42) for the non-frontier
        // capability negatives. node 1 = String result.
        {
            ModuleSpec spec3;
            spec3.entry_id = 7;
            spec3.extra_schema_nodes = {bounded_string_node(8)};
            spec3.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                          CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
            spec3.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                           ManifestNodeSpec{42, 1, 4, 901}};
            auto mr3 =
                admit_module(spec3);
            check(mr3.ok(), "B_D1b.three_node_module");
            if (mr3.ok()) {
                const auto &m3 = *mr3.module;
                const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
                const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
                const auto base_three = [&]() {
                    CoreWasmResumeRecord r;
                    r.format_version = 1;
                    set_matching_digests(r, m3);
                    r.entry_id = wf;
                    r.entry_input_slot = PayloadSlotId{9};
                    r.suspended_node_id = CoreWorkflowNodeId{42};
                    r.resume_state = ResumeState::Suspended;
                    ResumeNode n0;
                    n0.workflow_node_id = CoreWorkflowNodeId{40};
                    n0.schedule_pos = 0;
                    n0.node_kind = NodeKind::Identity;
                    ResumeNode n1;
                    n1.workflow_node_id = CoreWorkflowNodeId{41};
                    n1.schedule_pos = 1;
                    n1.node_kind = NodeKind::Capability;
                    ResumeNode n2;
                    n2.workflow_node_id = CoreWorkflowNodeId{42};
                    n2.schedule_pos = 2;
                    n2.node_kind = NodeKind::Capability;
                    ResumePendingEntry p;
                    p.invocation_ordinal = InvocationOrdinal{0};
                    p.capability = CoreCapabilityId{4};
                    p.source_symbol = 901;
                    p.arg_hash = ah1;
                    n2.pending = p;
                    r.nodes = {n0, n1, n2};
                    return r;
                };
                // B-D1b: non-frontier capability node 41 with an EMPTY memo (A1-valid: a
                // capability node may carry zero memo entries; D1b requires exactly one).
                {
                    CoreWasmResumeRecord r = base_three(); // node 41 has no memo yet
                    d1b_coord_case("nonfrontier_missing_memo", m3, r,
                                   {ps::Slot{PayloadSlotId{9}, kEntryBytes}});
                }
                // B-D1b: non-frontier capability node 41 with memo_count 2 (dense ordinals
                // 0,1 -> A1-valid; D1b baseline requires exactly one ordinal-0 memo).
                {
                    CoreWasmResumeRecord r = base_three();
                    ResumeMemoEntry m0;
                    m0.invocation_ordinal = InvocationOrdinal{0};
                    m0.capability = CoreCapabilityId{3};
                    m0.source_symbol = 900;
                    m0.arg_hash = ah0;
                    m0.result_slot = PayloadSlotId{5};
                    ResumeMemoEntry m1;
                    m1.invocation_ordinal = InvocationOrdinal{1};
                    m1.capability = CoreCapabilityId{3};
                    m1.source_symbol = 900;
                    m1.arg_hash = ah0;
                    m1.result_slot = PayloadSlotId{6};
                    r.nodes[1].memo = {m0, m1};
                    d1b_coord_case("nonfrontier_memo_count_2", m3, r,
                                   {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                    ps::Slot{PayloadSlotId{5}, injected},
                                    ps::Slot{PayloadSlotId{6}, injected}});
                }
                // B-D1b: Suspended frontier node 42 carries one committed memo AND a pending
                // whose ordinal (1) == memo.size() (A1-valid multi-invocation record), but the
                // A2 baseline this slice supports is exactly one ordinal-0 memo per capability,
                // so D1b rejects the extra invocation.
                {
                    CoreWasmResumeRecord r = base_three();
                    ResumeMemoEntry fm0;
                    fm0.invocation_ordinal = InvocationOrdinal{0};
                    fm0.capability = CoreCapabilityId{4};
                    fm0.source_symbol = 901;
                    fm0.arg_hash = ah1;
                    fm0.result_slot = PayloadSlotId{6};
                    r.nodes[2].memo = {fm0};
                    r.nodes[2].pending->invocation_ordinal = InvocationOrdinal{1}; // == memo.size()
                    d1b_coord_case("suspended_frontier_memo_plus_pending", m3, r,
                                   {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                    ps::Slot{PayloadSlotId{6}, injected}});
                }
                // B-D1b: Injected frontier node 42 carries TWO committed memos (dense ordinals
                // 0,1). A1-valid, but the baseline allows exactly one -> CoordinateMismatch.
                {
                    CoreWasmResumeRecord r = base_three();
                    r.resume_state = ResumeState::Injected;
                    ResumeMemoEntry fm0;
                    fm0.invocation_ordinal = InvocationOrdinal{0};
                    fm0.capability = CoreCapabilityId{4};
                    fm0.source_symbol = 901;
                    fm0.arg_hash = ah1;
                    fm0.result_slot = PayloadSlotId{6};
                    ResumeMemoEntry fm1;
                    fm1.invocation_ordinal = InvocationOrdinal{1};
                    fm1.capability = CoreCapabilityId{4};
                    fm1.source_symbol = 901;
                    fm1.arg_hash = ah1;
                    fm1.result_slot = PayloadSlotId{7};
                    r.nodes[2].memo = {fm0, fm1};
                    r.nodes[2].pending.reset();
                    d1b_coord_case("injected_frontier_two_memos", m3, r,
                                   {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                    ps::Slot{PayloadSlotId{6}, injected},
                                    ps::Slot{PayloadSlotId{7}, injected}});
                }
                // B-D1b: node kind / cap_call_count disagreement. The record marks node 41 as
                // Identity, but the manifest node 41 is a capability (cap_call_count 1). A1
                // does not cross-check the manifest, so this is A1-valid but D1b rejects.
                {
                    CoreWasmResumeRecord r = base_three();
                    r.nodes[1].node_kind = NodeKind::Identity; // manifest 41 is a capability
                    d1b_coord_case("node_kind_disagreement", m3, r,
                                   {ps::Slot{PayloadSlotId{9}, kEntryBytes}});
                }
            }
        }
    }
    {
        // B-priority: a phase-1 gate must win BEFORE phase-2 slot admission. Publish a
        // 3-node record whose committed memo slot 5 is then tampered on disk (a cross-artifact
        // DIGEST tamper: the manifest's slot_artifact_sha256 cross-check, which runs before
        // the slot codec/HMAC, would fail admission with StateMismatch). With ALSO a
        // coordinate fault in the record, open_gated_resume returns CoordinateMismatch WITHOUT
        // ever admitting the slot. The mirror fixture -- identical tampered slot but a GOOD
        // coordinate -- reaches phase 2 and returns the exact store StateMismatch arm.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        check(mr.ok(), "Bpri.module");
        if (mr.ok()) {
            const auto &m3 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
            const std::vector<std::uint8_t> good(kStringResultJson.begin(),
                                                kStringResultJson.end());
            const auto bpri_record = [&](bool coord_fault) {
                CoreWasmResumeRecord r;
                r.format_version = 1;
                set_matching_digests(r, m3);
                r.entry_id = wf;
                r.entry_input_slot = PayloadSlotId{9};
                r.suspended_node_id = CoreWorkflowNodeId{42};
                r.resume_state = ResumeState::Suspended;
                ResumeNode n0;
                n0.workflow_node_id =
                    CoreWorkflowNodeId{static_cast<std::uint32_t>(coord_fault ? 4040 : 40)};
                n0.schedule_pos = 0;
                n0.node_kind = NodeKind::Identity;
                ResumeNode n1;
                n1.workflow_node_id = CoreWorkflowNodeId{41};
                n1.schedule_pos = 1;
                n1.node_kind = NodeKind::Capability;
                ResumeMemoEntry m1;
                m1.invocation_ordinal = InvocationOrdinal{0};
                m1.capability = CoreCapabilityId{3};
                m1.source_symbol = 900;
                m1.arg_hash = ah0;
                m1.result_slot = PayloadSlotId{5};
                n1.memo.push_back(m1);
                ResumeNode n2;
                n2.workflow_node_id = CoreWorkflowNodeId{42};
                n2.schedule_pos = 2;
                n2.node_kind = NodeKind::Capability;
                ResumePendingEntry p;
                p.invocation_ordinal = InvocationOrdinal{0};
                p.capability = CoreCapabilityId{4};
                p.source_symbol = 901;
                p.arg_hash = ah1;
                n2.pending = p;
                r.nodes = {n0, n1, n2};
                return r;
            };
            const std::vector<ps::Slot> bpri_slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                      ps::Slot{PayloadSlotId{5}, good}};
            // (a) coordinate fault + corrupt slot -> CoordinateMismatch at phase 1.
            {
                const fs::path work = base / "b-priority-coord";
                auto store = open_store(work);
                check(store.has_value(), "Bpri.a_store");
                if (store.has_value()) {
                    auto pub = store->publish_available(wf, ckpt, 0, bpri_record(true), bpri_slots,
                                                        id_span, key);
                    check(pub.has_value(), "Bpri.a_publish");
                    if (pub.has_value()) {
                        corrupt_artifacts(work, "slot"); // phase-2 admission WOULD fail
                        auto snap = store->open_snapshot(wf, ckpt, id_span, key);
                        check(snap.has_value(), "Bpri.a_snapshot"); // phase 1 auth's record only
                        if (snap.has_value()) {
                            auto gated = rc::open_gated_resume(m3, std::move(*snap));
                            check(!gated.has_value() &&
                                      is_prepare_reason(
                                          gated.error(),
                                          rc::ResumePrepareReason::CoordinateMismatch),
                                  "B.phase1_coordinate_beats_corrupt_slot");
                        }
                    }
                }
                nuke(work);
            }
            // (b) mirror: good coordinate + tampered slot -> phase 2 -> exact StateMismatch
            // (the manifest slot-digest cross-artifact check, before the slot HMAC).
            {
                const fs::path work = base / "b-priority-mirror";
                auto store = open_store(work);
                check(store.has_value(), "Bpri.b_store");
                if (store.has_value()) {
                    auto pub = store->publish_available(wf, ckpt, 0, bpri_record(false), bpri_slots,
                                                        id_span, key);
                    check(pub.has_value(), "Bpri.b_publish");
                    if (pub.has_value()) {
                        corrupt_artifacts(work, "slot");
                        auto snap = store->open_snapshot(wf, ckpt, id_span, key);
                        check(snap.has_value(), "Bpri.b_snapshot");
                        if (snap.has_value()) {
                            auto gated = rc::open_gated_resume(m3, std::move(*snap));
                            check(gated.has_value(), "Bpri.b_gated"); // good coordinate passes
                            if (gated.has_value()) {
                                auto outcome = rc::admit_and_preflight(
                                    std::move(*gated), id_span, key,
                                    std::span<const std::uint8_t>(injected), cap);
                                check(!outcome.has_value() &&
                                          std::holds_alternative<ps::PayloadStoreError>(
                                              outcome.error()) &&
                                          std::get<ps::PayloadStoreError>(outcome.error()) ==
                                              ps::PayloadStoreError::StateMismatch,
                                      "B.good_coordinate_reaches_phase2_store_error");
                            }
                        }
                    }
                }
                nuke(work);
            }
        }
    }

    // ================= GROUP B-AC14: P6 bridge node frontier acceptance ======
    //
    // WH-5b.2 AC14: a P6 bridge node (cap_call_count==0 + nonempty
    // bridge_sites) is accepted as a legal frontier by the coordinate gate.
    // The pending entry's (capability, source_symbol) is cross-checked
    // against the manifest bridge-sites table at the per-node ordinal.
    // Mismatch variants (wrong capability, wrong source_symbol, wrong
    // ordinal) are REJECTED with CoordinateMismatch.
    //
    // FOUNDATION only: this is a decision-only controller test. It never
    // touches a Wasm VM, allocates linear memory, or transfers a frame. The
    // tag-0 identity event_join is proven by the event_memory synthesizer
    // (cap_call_count==0 -> tag 0) and the coordinate gate's acceptance of
    // node_kind Identity for a P6 bridge node.
    {
        ModuleSpec bridge_spec;
        bridge_spec.entry_id = 7;
        bridge_spec.extra_schema_nodes = {bounded_string_node(8)};
        bridge_spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}}};
        bridge_spec.nodes = {
            ManifestNodeSpec{50, 0, 0, 0,
                             {ManifestBridgeSiteSpec{0, 0, 3, 900}}}};
        auto bridge_mr = admit_module(bridge_spec);
        check(bridge_mr.ok(), "AC14.module_admits");
        if (bridge_mr.ok()) {
            const auto &bridge_mod = *bridge_mr.module;
            check(bridge_mod.node_count() == 1, "AC14.one_node");

            // Build a Suspended record: the P6 bridge node is the frontier.
            // node_kind is Identity (cap_call_count==0); the pending entry
            // carries the bridge site's (capability, source_symbol, ordinal).
            const auto make_bridge_suspended = [&](std::uint32_t cap,
                                                   std::uint64_t sym,
                                                   std::uint32_t ord) {
                                CoreWasmResumeRecord r;
                                r.format_version = 1;
                                set_matching_digests(r, bridge_mod);
                                r.entry_id = wf;
                                r.entry_input_slot = PayloadSlotId{9};
                                r.suspended_node_id = CoreWorkflowNodeId{50};
                                r.resume_state = ResumeState::Suspended;
                                ResumeNode n;
                                n.workflow_node_id = CoreWorkflowNodeId{50};
                                n.schedule_pos = 0;
                                n.node_kind = NodeKind::Identity;
                                ResumePendingEntry p;
                                p.invocation_ordinal = InvocationOrdinal{ord};
                                p.capability = CoreCapabilityId{cap};
                                p.source_symbol = sym;
                                p.arg_hash = 0;
                                n.pending = p;
                                r.nodes.push_back(n);
                                return r;
                            };

            // Matching record: ACCEPTED by the coordinate gate.
            {
                const fs::path work = base / "b-ac14-match";
                auto store = open_store(work);
                check(store.has_value(), "AC14.match.store");
                if (store.has_value()) {
                    auto rec = make_bridge_suspended(3, 900, 0);
                    auto gated = publish_and_gate(*store, rec, entry_only_slots,
                                                  bridge_mod);
                    check(gated.has_value() && gated->has_value(),
                          "AC14.match.accepted");
                }
                nuke(work);
            }

            // Mismatch variant: wrong capability.
            {
                const fs::path work = base / "b-ac14-wrong-cap";
                auto store = open_store(work);
                check(store.has_value(), "AC14.wrong_cap.store");
                if (store.has_value()) {
                    auto rec = make_bridge_suspended(99, 900, 0);
                    auto gated = publish_and_gate(*store, rec, entry_only_slots,
                                                  bridge_mod);
                    check(gated.has_value() && !gated->has_value() &&
                              is_prepare_reason(gated->error(),
                                                rc::ResumePrepareReason::CoordinateMismatch),
                          "AC14.wrong_cap.rejected");
                }
                nuke(work);
            }

            // Mismatch variant: wrong source_symbol.
            {
                const fs::path work = base / "b-ac14-wrong-sym";
                auto store = open_store(work);
                check(store.has_value(), "AC14.wrong_sym.store");
                if (store.has_value()) {
                    auto rec = make_bridge_suspended(3, 111, 0);
                    auto gated = publish_and_gate(*store, rec, entry_only_slots,
                                                  bridge_mod);
                    check(gated.has_value() && !gated->has_value() &&
                              is_prepare_reason(gated->error(),
                                                rc::ResumePrepareReason::CoordinateMismatch),
                          "AC14.wrong_sym.rejected");
                }
                nuke(work);
            }

            // Mismatch variant: wrong ordinal (>= bridge_sites.size()).
            // A Suspended frontier's pending ordinal must equal memo.size()
            // (A1: 0 for an empty memo), so the out-of-range case is
            // unreachable for Suspended. Use an Injected frontier with two
            // dense memo entries (ordinals 0 and 1) against a single-site
            // bridge node: ordinal 1 is out of range and the coordinate
            // gate rejects it.
            {
                const fs::path work = base / "b-ac14-wrong-ord";
                auto store = open_store(work);
                check(store.has_value(), "AC14.wrong_ord.store");
                if (store.has_value()) {
                    CoreWasmResumeRecord r;
                    r.format_version = 1;
                    set_matching_digests(r, bridge_mod);
                    r.entry_id = wf;
                    r.entry_input_slot = PayloadSlotId{9};
                    r.suspended_node_id = CoreWorkflowNodeId{50};
                    r.resume_state = ResumeState::Injected;
                    ResumeNode n;
                    n.workflow_node_id = CoreWorkflowNodeId{50};
                    n.schedule_pos = 0;
                    n.node_kind = NodeKind::Identity;
                    // Two dense memo entries (ordinals 0 and 1). The bridge
                    // node has only one site, so ordinal 1 is out of range.
                    ResumeMemoEntry m0;
                    m0.invocation_ordinal = InvocationOrdinal{0};
                    m0.capability = CoreCapabilityId{3};
                    m0.source_symbol = 900;
                    m0.arg_hash = 0;
                    m0.result_slot = PayloadSlotId{5};
                    n.memo.push_back(m0);
                    ResumeMemoEntry m1;
                    m1.invocation_ordinal = InvocationOrdinal{1};
                    m1.capability = CoreCapabilityId{3};
                    m1.source_symbol = 900;
                    m1.arg_hash = 0;
                    m1.result_slot = PayloadSlotId{6};
                    n.memo.push_back(m1);
                    r.nodes.push_back(n);
                    const std::vector<ps::Slot> slots_with_memos = {
                        ps::Slot{PayloadSlotId{9}, kEntryBytes},
                        ps::Slot{PayloadSlotId{5}, kEntryBytes},
                        ps::Slot{PayloadSlotId{6}, kEntryBytes}};
                    auto gated = publish_and_gate(*store, r, slots_with_memos,
                                                  bridge_mod);
                    check(gated.has_value() && !gated->has_value() &&
                              is_prepare_reason(gated->error(),
                                                rc::ResumePrepareReason::CoordinateMismatch),
                          "AC14.wrong_ord.rejected");
                }
                nuke(work);
            }
        }
    }

    // ================= GROUP C: phase-2 eligibility priority =======================
    {
        // C.memo_schema_invalid: a committed memo whose stored slot bytes fail the Result
        // decode -> PayloadSchemaInvalid (a phase-2 gate that runs before eligibility).
        // Build a 3-node module: identity, committed cap (result node), frontier cap.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)}; // node 1 = String result
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        check(mr.ok(), "C.module_admits");
        if (mr.ok()) {
            const auto &m3 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
            // A record: node 41 committed (memo, result_slot 5), frontier node 42 pending.
            const auto make_c_record = [&](const std::vector<std::uint8_t> & /*unused*/) {
                CoreWasmResumeRecord r;
                r.format_version = 1;
                set_matching_digests(r, m3);
                r.entry_id = wf;
                r.entry_input_slot = PayloadSlotId{9};
                r.suspended_node_id = CoreWorkflowNodeId{42};
                r.resume_state = ResumeState::Suspended;
                ResumeNode n0;
                n0.workflow_node_id = CoreWorkflowNodeId{40};
                n0.schedule_pos = 0;
                n0.node_kind = NodeKind::Identity;
                ResumeNode n1;
                n1.workflow_node_id = CoreWorkflowNodeId{41};
                n1.schedule_pos = 1;
                n1.node_kind = NodeKind::Capability;
                ResumeMemoEntry m;
                m.invocation_ordinal = InvocationOrdinal{0};
                m.capability = CoreCapabilityId{3};
                m.source_symbol = 900;
                m.arg_hash = ah0;
                m.result_slot = PayloadSlotId{5};
                n1.memo.push_back(m);
                ResumeNode n2;
                n2.workflow_node_id = CoreWorkflowNodeId{42};
                n2.schedule_pos = 2;
                n2.node_kind = NodeKind::Capability;
                ResumePendingEntry p;
                p.invocation_ordinal = InvocationOrdinal{0};
                p.capability = CoreCapabilityId{4};
                p.source_symbol = 901;
                p.arg_hash = ah1;
                n2.pending = p;
                r.nodes = {n0, n1, n2};
                return r;
            };
            (void)ah1;
            const std::vector<std::uint8_t> good_result(kStringResultJson.begin(),
                                                        kStringResultJson.end());
            const std::vector<std::uint8_t> bad_result = {'n', 'o', 't', 'j', 's', 'o', 'n'};

            // C1: memo slot bytes are NOT valid JSON for the String binding ->
            // PayloadSchemaInvalid.
            {
                const fs::path work = base / "c-memo-schema-invalid";
                auto store = open_store(work);
                check(store.has_value(), "C1.store");
                if (store.has_value()) {
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, bad_result}};
                    auto gated = publish_and_gate(*store, make_c_record({}), slots, m3);
                    check(gated.has_value() && gated->has_value(), "C.memo_invalid_gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, key,
                            std::span<const std::uint8_t>(injected), cap);
                        check(!outcome.has_value() &&
                                  is_prepare_reason(outcome.error(),
                                                    rc::ResumePrepareReason::PayloadSchemaInvalid),
                              "C.memo_schema_invalid");
                    }
                }
                nuke(work);
            }
            // C2: wrong admission key -> the store's EXACT IntegrityFailed arm (not just any).
            {
                const fs::path work = base / "c-wrong-key";
                auto store = open_store(work);
                check(store.has_value(), "C2.store");
                if (store.has_value()) {
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good_result}};
                    auto gated = publish_and_gate(*store, make_c_record({}), slots, m3);
                    check(gated.has_value() && gated->has_value(), "C.wrongkey_gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto wrong_key = std::vector<std::uint8_t>(32, 0x00);
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, wrong_key,
                            std::span<const std::uint8_t>(injected), cap);
                        check(!outcome.has_value() &&
                                  std::holds_alternative<ps::PayloadStoreError>(outcome.error()) &&
                                  std::get<ps::PayloadStoreError>(outcome.error()) ==
                                      ps::PayloadStoreError::IntegrityFailed,
                              "C.wrong_key_integrity_failed");
                    }
                }
                nuke(work);
            }
            // C3: Injected + nonempty injected input -> TransitionInvalid (AFTER admit + memo
            // decode succeed with valid bytes).
            {
                const fs::path work = base / "c-injected-nonempty";
                auto store = open_store(work);
                check(store.has_value(), "C3.store");
                if (store.has_value()) {
                    // Injected record: frontier 42 has a committed ordinal-0 memo, no pending.
                    CoreWasmResumeRecord r = make_c_record({});
                    r.resume_state = ResumeState::Injected;
                    ResumeMemoEntry fm;
                    fm.invocation_ordinal = InvocationOrdinal{0};
                    fm.capability = CoreCapabilityId{4};
                    fm.source_symbol = 901;
                    fm.arg_hash = ah1;
                    fm.result_slot = PayloadSlotId{6};
                    r.nodes.back().memo.push_back(fm);
                    r.nodes.back().pending.reset();
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good_result},
                                                   ps::Slot{PayloadSlotId{6}, good_result}};
                    auto gated = publish_and_gate(*store, r, slots, m3);
                    check(gated.has_value() && gated->has_value(), "C.injected_nonempty_gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, key,
                            std::span<const std::uint8_t>(injected), cap);
                        check(!outcome.has_value() &&
                                  is_prepare_reason(outcome.error(),
                                                    rc::ResumePrepareReason::TransitionInvalid),
                              "C.injected_nonempty_transition_invalid");
                    }
                }
                nuke(work);
            }
            // C4: Injected + nonempty COMBINED with a schema-invalid stored memo. The memo
            // schema gate (phase-2 stored-memo decode) MUST win before the input-matrix
            // eligibility check, so the verdict is PayloadSchemaInvalid, not TransitionInvalid.
            {
                const fs::path work = base / "c-injected-nonempty-badmemo";
                auto store = open_store(work);
                check(store.has_value(), "C4.store");
                if (store.has_value()) {
                    CoreWasmResumeRecord r = make_c_record({}); // node 41 committed memo slot 5
                    r.resume_state = ResumeState::Injected;
                    ResumeMemoEntry fm;
                    fm.invocation_ordinal = InvocationOrdinal{0};
                    fm.capability = CoreCapabilityId{4};
                    fm.source_symbol = 901;
                    fm.arg_hash = ah1;
                    fm.result_slot = PayloadSlotId{6};
                    r.nodes.back().memo.push_back(fm);
                    r.nodes.back().pending.reset();
                    // node 41's memo slot 5 holds INVALID JSON -> memo decode fails first.
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, bad_result},
                                                   ps::Slot{PayloadSlotId{6}, good_result}};
                    auto gated = publish_and_gate(*store, r, slots, m3);
                    check(gated.has_value() && gated->has_value(), "C4.gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, key,
                            std::span<const std::uint8_t>(injected), cap);
                        check(!outcome.has_value() &&
                                  is_prepare_reason(outcome.error(),
                                                    rc::ResumePrepareReason::PayloadSchemaInvalid),
                              "C.memo_schema_gate_beats_eligibility");
                    }
                }
                nuke(work);
            }
            // C5: a CORRUPT committed slot -> the store's EXACT StateMismatch arm. Flipping a
            // byte of the slot-5 artifact breaks its manifest digest cross-check (which runs
            // before the HMAC decode) at phase-2 admission; the reason is carried back verbatim.
            {
                const fs::path work = base / "c-corrupt-slot";
                auto store = open_store(work);
                check(store.has_value(), "C5.store");
                if (store.has_value()) {
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good_result}};
                    auto pub = store->publish_available(wf, ckpt, 0, make_c_record({}), slots,
                                                        id_span, key);
                    check(pub.has_value(), "C5.publish");
                    if (pub.has_value()) {
                        // Corrupt the on-disk slot artifact(s) of the published generation.
                        corrupt_artifacts(work, "slot");
                        auto snap = store->open_snapshot(wf, ckpt, id_span, key);
                        // Phase 1 authenticates the record (not the slot), so open_snapshot
                        // succeeds; the corrupt slot is caught at admit (phase 2).
                        check(snap.has_value(), "C5.snapshot_phase1_ok");
                        if (snap.has_value()) {
                            auto gated = rc::open_gated_resume(m3, std::move(*snap));
                            check(gated.has_value(), "C5.gated");
                            if (gated.has_value()) {
                                auto outcome = rc::admit_and_preflight(
                                    std::move(*gated), id_span, key,
                                    std::span<const std::uint8_t>(injected), cap);
                                check(!outcome.has_value() &&
                                          std::holds_alternative<ps::PayloadStoreError>(
                                              outcome.error()) &&
                                          std::get<ps::PayloadStoreError>(outcome.error()) ==
                                              ps::PayloadStoreError::StateMismatch,
                                      "C.corrupt_slot_state_mismatch");
                            }
                        }
                    }
                }
                nuke(work);
            }
        }
    }

    // ================= GROUP C-repeat: repeated result_slot positive ==============
    {
        // Two committed caps (nodes 41, 42) whose memos reuse ONE result_slot 5. The store
        // dedups the expected set to {9,5}; build_prepared views both occurrences into the
        // single admitted payload (same .data()) yet decodes each binding.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}},
                     CapSpec{5, 902, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}, ManifestNodeSpec{43, 1, 5, 902}};
        auto mr = admit_module(spec);
        check(mr.ok(), "Crepeat.module_admits");
        if (mr.ok()) {
            const auto &m4 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m4, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m4, 1, kIntParamJson);
            const std::uint64_t ah2 = param_arg_hash(m4, 2, kIntParamJson);
            const std::vector<std::uint8_t> good_result(kStringResultJson.begin(),
                                                        kStringResultJson.end());
            CoreWasmResumeRecord r;
            r.format_version = 1;
            set_matching_digests(r, m4);
            r.entry_id = wf;
            r.entry_input_slot = PayloadSlotId{9};
            r.suspended_node_id = CoreWorkflowNodeId{43};
            r.resume_state = ResumeState::Suspended;
            const auto mk_ident = [](std::uint32_t id, std::uint32_t sched) {
                ResumeNode n;
                n.workflow_node_id = CoreWorkflowNodeId{id};
                n.schedule_pos = sched;
                n.node_kind = NodeKind::Identity;
                return n;
            };
            const auto mk_cap_memo = [](std::uint32_t id, std::uint32_t sched, std::uint32_t capid,
                                        std::uint64_t sym, std::uint64_t ah, std::uint64_t slot) {
                ResumeNode n;
                n.workflow_node_id = CoreWorkflowNodeId{id};
                n.schedule_pos = sched;
                n.node_kind = NodeKind::Capability;
                ResumeMemoEntry m;
                m.invocation_ordinal = InvocationOrdinal{0};
                m.capability = CoreCapabilityId{capid};
                m.source_symbol = sym;
                m.arg_hash = ah;
                m.result_slot = PayloadSlotId{slot};
                n.memo.push_back(m);
                return n;
            };
            (void)mk_ident;
            ResumeNode n0 = mk_ident(40, 0);
            ResumeNode n1 = mk_cap_memo(41, 1, 3, 900, ah0, 5); // result_slot 5
            ResumeNode n2 = mk_cap_memo(42, 2, 4, 901, ah1, 5); // SAME result_slot 5
            ResumeNode n3;
            n3.workflow_node_id = CoreWorkflowNodeId{43};
            n3.schedule_pos = 3;
            n3.node_kind = NodeKind::Capability;
            ResumePendingEntry p;
            p.invocation_ordinal = InvocationOrdinal{0};
            p.capability = CoreCapabilityId{5};
            p.source_symbol = 902;
            p.arg_hash = ah2;
            n3.pending = p;
            r.nodes = {n0, n1, n2, n3};
            const fs::path work = base / "c-repeat-slot";
            auto store = open_store(work);
            check(store.has_value(), "Crepeat.store");
            if (store.has_value()) {
                std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                               ps::Slot{PayloadSlotId{5}, good_result}};
                auto gated = publish_and_gate(*store, r, slots, m4);
                check(gated.has_value() && gated->has_value(), "Crepeat.gated");
                if (gated.has_value() && gated->has_value()) {
                    auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                           std::span<const std::uint8_t>(), cap);
                    check(outcome.has_value() &&
                              std::holds_alternative<rc::PendingInjection>(*outcome),
                          "Crepeat.pending");
                    if (outcome.has_value() &&
                        std::holds_alternative<rc::PendingInjection>(*outcome)) {
                        auto prepared = rc::supply_injected_result(
                            std::get<rc::PendingInjection>(std::move(*outcome)),
                            std::span<const std::uint8_t>(injected));
                        check(prepared.has_value(), "Crepeat.prepared");
                        if (prepared.has_value()) {
                            // Replay imports 0 and 1 (both memoized, both slot 5): the two
                            // ReturnMemo spans must share .data() (one admitted authority).
                            const std::vector<std::uint8_t> mem_p1 = event_memory(spec.nodes, 1);
                            const std::vector<std::uint8_t> mem_p2 = event_memory(spec.nodes, 2);
                            const auto ord0 =
                                m4.resolve(csm::ManifestCallSiteIndex{0})
                                    .call_site->import_ordinal();
                            const auto ord1 =
                                m4.resolve(csm::ManifestCallSiteIndex{1})
                                    .call_site->import_ordinal();
                            rc::ImportStepInput i0;
                            i0.observed_ordinal = ord0;
                            i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                            auto d0 = rc::next_import(*prepared, i0);
                            rc::ImportStepInput i1;
                            i1.observed_ordinal = ord1;
                            i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i1.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                            auto d1 = rc::next_import(*prepared, i1);
                            check(d0.has_value() && std::holds_alternative<rc::ReturnMemo>(*d0) &&
                                      d1.has_value() &&
                                      std::holds_alternative<rc::ReturnMemo>(*d1),
                                  "Crepeat.two_return_memo");
                            if (d0.has_value() && std::holds_alternative<rc::ReturnMemo>(*d0) &&
                                d1.has_value() && std::holds_alternative<rc::ReturnMemo>(*d1)) {
                                check(std::get<rc::ReturnMemo>(*d0).memo_bytes.data() ==
                                          std::get<rc::ReturnMemo>(*d1).memo_bytes.data(),
                                      "Crepeat.shared_authority_same_data");
                            }
                        }
                    }
                }
            }
            nuke(work);
        }
    }

    // ================= GROUP D: TOTAL two-pass preflight ===========================
    // A 3-node module: identity 40, frontier cap 41 (bounded result node 1), future cap 42
    // whose result is schema node index 2. `extra` supplies every schema node after the
    // Int param (node 0): node 1 = bounded frontier String; node 2 = the future result;
    // any further nodes (e.g. a List element) must be referenced, so callers pass exactly
    // the reachable set (the wire-schema verifier rejects orphan nodes).
    const auto build_three_node = [&](std::vector<CoreWireSchemaNode> extra)
        -> std::optional<VerifiedCoreWasmSchemaModule> {
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = std::move(extra);
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{2}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        if (!mr.ok()) {
            return std::nullopt;
        }
        return *mr.module;
    };
    const auto d_record = [&](const VerifiedCoreWasmSchemaModule &m3, std::uint64_t ah) {
        CoreWasmResumeRecord r;
        r.format_version = 1;
        set_matching_digests(r, m3);
        r.entry_id = wf;
        r.entry_input_slot = PayloadSlotId{9};
        r.suspended_node_id = CoreWorkflowNodeId{41};
        r.resume_state = ResumeState::Suspended;
        ResumeNode n0;
        n0.workflow_node_id = CoreWorkflowNodeId{40};
        n0.schedule_pos = 0;
        n0.node_kind = NodeKind::Identity;
        ResumeNode n1;
        n1.workflow_node_id = CoreWorkflowNodeId{41};
        n1.schedule_pos = 1;
        n1.node_kind = NodeKind::Capability;
        ResumePendingEntry p;
        p.invocation_ordinal = InvocationOrdinal{0};
        p.capability = CoreCapabilityId{3};
        p.source_symbol = 900;
        p.arg_hash = ah;
        n1.pending = p;
        r.nodes = {n0, n1};
        return r;
    };
    // Run admit_and_preflight (Suspended + supplied injected) and return the prepare error.
    const auto d_preflight_error =
        [&](const std::string &name, const VerifiedCoreWasmSchemaModule &m3,
            rc::LinearMemoryCapacityBytes capacity)
        -> std::optional<rc::ResumePrepareError> {
        const std::uint64_t ah = param_arg_hash(m3, 0, kIntParamJson);
        const fs::path work = base / ("d-" + name);
        auto store = open_store(work);
        std::optional<rc::ResumePrepareError> err;
        if (store.has_value()) {
            std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes}};
            auto gated = publish_and_gate(*store, d_record(m3, ah), slots, m3);
            if (gated.has_value() && gated->has_value()) {
                auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                       std::span<const std::uint8_t>(injected),
                                                       capacity);
                if (!outcome.has_value()) {
                    err = outcome.error();
                } else {
                    err = std::nullopt; // success sentinel handled by caller
                }
            } else {
                check(false, name + ".gate_setup");
            }
        } else {
            check(false, name + ".store_setup");
        }
        nuke(work);
        return err;
    };

    {
        // D1: future-live Unbounded -> Unbounded. node 1 = bounded frontier String,
        // node 2 = unbounded future String.
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        extra.push_back(unbounded_string_node());
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D.unbounded_module");
        if (m3.has_value()) {
            auto e = d_preflight_error("unbounded", *m3, cap);
            check(e.has_value() && is_prepare_reason(*e, rc::ResumePrepareReason::Unbounded),
                  "D.future_unbounded");
        }
    }
    {
        // D2: SizeOverflow-only future binding -> ResourceExhausted. node 2 = a List of
        // capacity UINT64_MAX over Int element node 3 (overflows the checked u64 size).
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        extra.push_back(
            list_node(3, std::optional<std::uint64_t>(std::numeric_limits<std::uint64_t>::max())));
        extra.push_back(int_node()); // node 3, referenced by the List element
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D.overflow_module");
        if (m3.has_value()) {
            auto e = d_preflight_error("overflow", *m3, cap);
            check(e.has_value() &&
                      is_prepare_reason(*e, rc::ResumePrepareReason::ResourceExhausted),
                  "D.future_size_overflow");
        }
    }
    {
        // D3: bounded future binding + tiny capacity -> ResourceExhausted (capacity verdict).
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        extra.push_back(bounded_string_node(8));
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D.bounded_module");
        if (m3.has_value()) {
            auto e = d_preflight_error("tinycap", *m3, rc::LinearMemoryCapacityBytes{1});
            check(e.has_value() &&
                      is_prepare_reason(*e, rc::ResumePrepareReason::ResourceExhausted),
                  "D.tiny_capacity");
        }
    }
    {
        // D4: capacity exact-fit vs +1 boundary. Compute the total the preflight sums, then
        // set capacity to total-1 (reject) and total (accept). This proves the frontier is
        // counted once (heap_base + entry + injected + future-bound), no double count.
        // heap_base for 3 nodes = 1152; entry = 3; injected "\"ok\"" = 4; frontier is the
        // Suspended pending (counted via injected only, its callsite_memo is absent); the
        // future bound (bounded String max 8) = 2 + 6*8 = 50. total = 1152+3+4+50 = 1209.
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        extra.push_back(bounded_string_node(8));
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D.fit_module");
        if (m3.has_value()) {
            const std::uint64_t expect_total = 1152 + 3 + 4 + 50;
            auto reject = d_preflight_error("fit_minus1", *m3,
                                            rc::LinearMemoryCapacityBytes{expect_total - 1});
            check(reject.has_value() &&
                      is_prepare_reason(*reject, rc::ResumePrepareReason::ResourceExhausted),
                  "D.capacity_exact_minus1_rejects");
            auto accept = d_preflight_error("fit_exact", *m3,
                                            rc::LinearMemoryCapacityBytes{expect_total});
            check(!accept.has_value(), "D.capacity_exact_fit_accepts");
        }
    }
    {
        // D5: Unbounded beats SizeOverflow regardless of future-binding ORDER. Build a
        // 4-node module: identity 40, frontier cap 41, then two future caps 42/43 -- one
        // with an unbounded result, one with an overflowing List result -- in each order.
        // The verdict must be Unbounded both times.
        const auto build_two_future = [&](bool unbounded_first)
            -> std::optional<VerifiedCoreWasmSchemaModule> {
            std::vector<CoreWireSchemaNode> extra;
            extra.push_back(bounded_string_node(8)); // node 1: frontier result
            if (unbounded_first) {
                extra.push_back(unbounded_string_node()); // node 2
                extra.push_back(list_node(4, std::optional<std::uint64_t>(
                                                 std::numeric_limits<std::uint64_t>::max()))); // 3
            } else {
                extra.push_back(list_node(4, std::optional<std::uint64_t>(
                                                 std::numeric_limits<std::uint64_t>::max()))); // 2
                extra.push_back(unbounded_string_node()); // node 3
            }
            extra.push_back(int_node()); // node 4: List element
            ModuleSpec spec;
            spec.entry_id = 7;
            spec.extra_schema_nodes = std::move(extra);
            spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                         CapSpec{4, 901, CoreWireSchemaNodeId{2}},
                         CapSpec{5, 902, CoreWireSchemaNodeId{3}}};
            spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                          ManifestNodeSpec{42, 1, 4, 901}, ManifestNodeSpec{43, 1, 5, 902}};
            auto mr =
                admit_module(spec);
            if (!mr.ok()) {
                return std::nullopt;
            }
            return *mr.module;
        };
        for (bool unbounded_first : {true, false}) {
            auto m4 = build_two_future(unbounded_first);
            const std::string tag = unbounded_first ? "unbounded_first" : "overflow_first";
            check(m4.has_value(), "D5." + tag + "_module");
            if (m4.has_value()) {
                auto e = d_preflight_error("d5_" + tag, *m4, cap);
                check(e.has_value() && is_prepare_reason(*e, rc::ResumePrepareReason::Unbounded),
                      "D.unbounded_beats_overflow_" + tag);
            }
        }
    }
    {
        // D6: PendingInjection -> supply hits the FULL pass1+pass2. A future-live Unbounded
        // binding is discovered only during the supply continuation's preflight (the admit
        // step returns PendingInjection without running the injected-length preflight). The
        // supply call must therefore fail with Unbounded.
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        extra.push_back(unbounded_string_node());
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D6.module");
        if (m3.has_value()) {
            const std::uint64_t ah = param_arg_hash(*m3, 0, kIntParamJson);
            const fs::path work = base / "d6-supply-unbounded";
            auto store = open_store(work);
            check(store.has_value(), "D6.store");
            if (store.has_value()) {
                std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes}};
                auto gated = publish_and_gate(*store, d_record(*m3, ah), slots, *m3);
                check(gated.has_value() && gated->has_value(), "D6.gated");
                if (gated.has_value() && gated->has_value()) {
                    // Admit with EMPTY injected -> PendingInjection (no preflight yet).
                    auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                           std::span<const std::uint8_t>(), cap);
                    check(outcome.has_value() &&
                              std::holds_alternative<rc::PendingInjection>(*outcome),
                          "D6.pending");
                    if (outcome.has_value() &&
                        std::holds_alternative<rc::PendingInjection>(*outcome)) {
                        auto sup = rc::supply_injected_result(
                            std::get<rc::PendingInjection>(std::move(*outcome)),
                            std::span<const std::uint8_t>(injected));
                        check(!sup.has_value() &&
                                  is_prepare_reason(sup.error(),
                                                    rc::ResumePrepareReason::Unbounded),
                              "D.supply_full_preflight_unbounded");
                    }
                }
            }
            nuke(work);
        }
    }
    {
        // D7: total > UINT32_MAX with supplied capacity ALSO > UINT32_MAX. A future-live
        // bounded String whose max length pushes its canonical bound past 4 GiB makes the
        // checked-u64 total exceed the wasm32 domain; capacity = UINT64_MAX proves the u32
        // host-transfer-domain gate fires independently of the capacity verdict.
        std::vector<CoreWireSchemaNode> extra;
        extra.push_back(bounded_string_node(8));
        // bound = 2 + 6*max; choose max so 6*max > UINT32_MAX (max = 800,000,000 -> ~4.8e9).
        extra.push_back(bounded_string_node(800000000));
        auto m3 = build_three_node(std::move(extra));
        check(m3.has_value(), "D7.module");
        if (m3.has_value()) {
            auto e = d_preflight_error(
                "d7_u32", *m3,
                rc::LinearMemoryCapacityBytes{std::numeric_limits<std::uint64_t>::max()});
            check(e.has_value() &&
                      is_prepare_reason(*e, rc::ResumePrepareReason::ResourceExhausted),
                  "D.total_exceeds_u32_even_with_large_capacity");
        }
    }
    {
        // D8: memo-multiplicity exact boundary with a REPEATED result_slot. A 4-node module
        // (identity 40, committed caps 41+42 whose memos BOTH reference slot 5, frontier cap
        // 43 pending). One admitted payload backs both occurrences, yet the total must add
        // its actual length TWICE. heap_base(4)=1192 + entry 3 + memo "ok"(4)*2 + injected
        // "ok"(4) = 1207. capacity 1207 accepts; 1206 rejects.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}},
                     CapSpec{5, 902, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}, ManifestNodeSpec{43, 1, 5, 902}};
        auto mr = admit_module(spec);
        check(mr.ok(), "D8.module");
        if (mr.ok()) {
            const auto &m4 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m4, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m4, 1, kIntParamJson);
            const std::uint64_t ah2 = param_arg_hash(m4, 2, kIntParamJson);
            const std::vector<std::uint8_t> good(kStringResultJson.begin(),
                                                kStringResultJson.end());
            const auto d8_record = [&]() {
                CoreWasmResumeRecord r;
                r.format_version = 1;
                set_matching_digests(r, m4);
                r.entry_id = wf;
                r.entry_input_slot = PayloadSlotId{9};
                r.suspended_node_id = CoreWorkflowNodeId{43};
                r.resume_state = ResumeState::Suspended;
                ResumeNode n0;
                n0.workflow_node_id = CoreWorkflowNodeId{40};
                n0.schedule_pos = 0;
                n0.node_kind = NodeKind::Identity;
                const auto cap_memo = [](std::uint32_t id, std::uint32_t sched, std::uint32_t c,
                                         std::uint64_t sym, std::uint64_t ah, std::uint64_t slot) {
                    ResumeNode n;
                    n.workflow_node_id = CoreWorkflowNodeId{id};
                    n.schedule_pos = sched;
                    n.node_kind = NodeKind::Capability;
                    ResumeMemoEntry m;
                    m.invocation_ordinal = InvocationOrdinal{0};
                    m.capability = CoreCapabilityId{c};
                    m.source_symbol = sym;
                    m.arg_hash = ah;
                    m.result_slot = PayloadSlotId{slot};
                    n.memo.push_back(m);
                    return n;
                };
                ResumeNode n1 = cap_memo(41, 1, 3, 900, ah0, 5); // slot 5
                ResumeNode n2 = cap_memo(42, 2, 4, 901, ah1, 5); // SAME slot 5
                ResumeNode n3;
                n3.workflow_node_id = CoreWorkflowNodeId{43};
                n3.schedule_pos = 3;
                n3.node_kind = NodeKind::Capability;
                ResumePendingEntry p;
                p.invocation_ordinal = InvocationOrdinal{0};
                p.capability = CoreCapabilityId{5};
                p.source_symbol = 902;
                p.arg_hash = ah2;
                n3.pending = p;
                r.nodes = {n0, n1, n2, n3};
                return r;
            };
            const std::uint64_t d8_total = 1192 + 3 + 4 + 4 + 4; // heap + entry + 2*memo + injected
            const auto run_d8 = [&](std::uint64_t capacity_bytes)
                -> std::optional<rc::ResumePrepareError> {
                const fs::path work = base / ("d8-" + std::to_string(capacity_bytes));
                auto store = open_store(work);
                std::optional<rc::ResumePrepareError> err;
                check(store.has_value(), "D8.store");
                if (store.has_value()) {
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good}};
                    auto gated = publish_and_gate(*store, d8_record(), slots, m4);
                    check(gated.has_value() && gated->has_value(), "D8.gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, key,
                            std::span<const std::uint8_t>(injected),
                            rc::LinearMemoryCapacityBytes{capacity_bytes});
                        if (!outcome.has_value()) {
                            err = outcome.error();
                        }
                    }
                }
                nuke(work);
                return err;
            };
            auto reject = run_d8(d8_total - 1);
            check(reject.has_value() &&
                      is_prepare_reason(*reject, rc::ResumePrepareReason::ResourceExhausted),
                  "D.repeated_memo_multiplicity_minus1_rejects");
            auto accept = run_d8(d8_total);
            check(!accept.has_value(), "D.repeated_memo_multiplicity_exact_fit_accepts");
        }
    }
    {
        // D9: Injected-frontier committed-memo counted-once exact boundary. A 3-node Injected
        // record (identity 40, committed cap 41 slot 5, frontier cap 42 committed slot 6, no
        // pending, NO injected input). heap_base(3)=1152 + entry 3 + memo41 "ok"(4) + memo42
        // "ok"(4) = 1163. No injected_actual is added (Injected path). capacity 1163 accepts;
        // 1162 rejects -- proving the frontier committed memo is counted once, not doubled.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        check(mr.ok(), "D9.module");
        if (mr.ok()) {
            const auto &m3 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
            const std::vector<std::uint8_t> good(kStringResultJson.begin(),
                                                kStringResultJson.end());
            const auto d9_record = [&]() {
                CoreWasmResumeRecord r;
                r.format_version = 1;
                set_matching_digests(r, m3);
                r.entry_id = wf;
                r.entry_input_slot = PayloadSlotId{9};
                r.suspended_node_id = CoreWorkflowNodeId{42};
                r.resume_state = ResumeState::Injected;
                ResumeNode n0;
                n0.workflow_node_id = CoreWorkflowNodeId{40};
                n0.schedule_pos = 0;
                n0.node_kind = NodeKind::Identity;
                ResumeNode n1;
                n1.workflow_node_id = CoreWorkflowNodeId{41};
                n1.schedule_pos = 1;
                n1.node_kind = NodeKind::Capability;
                ResumeMemoEntry m1;
                m1.invocation_ordinal = InvocationOrdinal{0};
                m1.capability = CoreCapabilityId{3};
                m1.source_symbol = 900;
                m1.arg_hash = ah0;
                m1.result_slot = PayloadSlotId{5};
                n1.memo.push_back(m1);
                ResumeNode n2;
                n2.workflow_node_id = CoreWorkflowNodeId{42};
                n2.schedule_pos = 2;
                n2.node_kind = NodeKind::Capability;
                ResumeMemoEntry m2;
                m2.invocation_ordinal = InvocationOrdinal{0};
                m2.capability = CoreCapabilityId{4};
                m2.source_symbol = 901;
                m2.arg_hash = ah1;
                m2.result_slot = PayloadSlotId{6};
                n2.memo.push_back(m2);
                r.nodes = {n0, n1, n2};
                return r;
            };
            const std::uint64_t d9_total = 1152 + 3 + 4 + 4; // heap + entry + memo41 + memo42
            const auto run_d9 = [&](std::uint64_t capacity_bytes)
                -> std::optional<rc::ResumePrepareError> {
                const fs::path work = base / ("d9-" + std::to_string(capacity_bytes));
                auto store = open_store(work);
                std::optional<rc::ResumePrepareError> err;
                check(store.has_value(), "D9.store");
                if (store.has_value()) {
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good},
                                                   ps::Slot{PayloadSlotId{6}, good}};
                    auto gated = publish_and_gate(*store, d9_record(), slots, m3);
                    check(gated.has_value() && gated->has_value(), "D9.gated");
                    if (gated.has_value() && gated->has_value()) {
                        // Injected record admits NO new input -> empty injected span.
                        auto outcome = rc::admit_and_preflight(
                            std::move(**gated), id_span, key, std::span<const std::uint8_t>(),
                            rc::LinearMemoryCapacityBytes{capacity_bytes});
                        if (!outcome.has_value()) {
                            err = outcome.error();
                        }
                    }
                }
                nuke(work);
                return err;
            };
            auto reject = run_d9(d9_total - 1);
            check(reject.has_value() &&
                      is_prepare_reason(*reject, rc::ResumePrepareReason::ResourceExhausted),
                  "D.injected_frontier_memo_minus1_rejects");
            auto accept = run_d9(d9_total);
            check(!accept.has_value(), "D.injected_frontier_memo_exact_fit_accepts");
        }
    }

    // ================= GROUP E: per-import / event-join branches ===================
    {
        // E1: after-frontier ReadyForLive + AwaitingLiveResult locks out further steps.
        // Use an Injected-frontier variant? Simpler: drive the shared module past its
        // frontier via publish ACK, then a SECOND next_import would be after the (only)
        // call site -> cursor == call_site_count -> next_import CoordinateMismatch. Instead
        // build a 3-node module whose SECOND cap is live (after the frontier) to hit
        // ReadyForLive. Frontier = node 41; node 42 is future-live.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        check(mr.ok(), "E.module_admits");
        if (mr.ok()) {
            const auto &m3 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
            const std::vector<std::uint8_t> mem_p1 = event_memory(spec.nodes, 1);
            const std::vector<std::uint8_t> mem_p2 = event_memory(spec.nodes, 2);
            const std::vector<std::uint8_t> mem_p3 = event_memory(spec.nodes, 3);
            const auto ord0 = m3.resolve(csm::ManifestCallSiteIndex{0}).call_site->import_ordinal();
            const auto ord1 = m3.resolve(csm::ManifestCallSiteIndex{1}).call_site->import_ordinal();

            // Helper: prepare a Suspended record whose frontier is node 41.
            const auto prep_e = [&](std::optional<ps::IntegrityPayloadStore> &store,
                                    const fs::path &work) -> std::optional<rc::PreparedResume> {
                store = open_store(work);
                if (!store.has_value()) {
                    return std::nullopt;
                }
                CoreWasmResumeRecord r = d_record(m3, ah0); // frontier 41, 2-node prefix
                auto gated = publish_and_gate(
                    *store, r, {ps::Slot{PayloadSlotId{9}, kEntryBytes}}, m3);
                if (!gated.has_value() || !gated->has_value()) {
                    return std::nullopt;
                }
                auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                       std::span<const std::uint8_t>(), cap);
                if (!outcome.has_value() ||
                    !std::holds_alternative<rc::PendingInjection>(*outcome)) {
                    return std::nullopt;
                }
                auto prepared = rc::supply_injected_result(
                    std::get<rc::PendingInjection>(std::move(*outcome)),
                    std::span<const std::uint8_t>(injected));
                if (!prepared.has_value()) {
                    return std::nullopt;
                }
                return std::move(*prepared);
            };

            // E1: reach the frontier, bind+ACK, then next_import at cursor 1 (node 42, live)
            // -> ReadyForLive with owned arity-1 Value + computed arg_hash. A `reach_live`
            // helper rebuilds an INDEPENDENT AwaitingLiveResult PreparedResume so each
            // rejecting-API case below is proven against AwaitingLiveResult itself (not a
            // Failed state left by a prior invalid call).
            const auto reach_live =
                [&](const fs::path &work, std::optional<ps::IntegrityPayloadStore> &store_out)
                -> std::optional<rc::PreparedResume> {
                auto prepared = prep_e(store_out, work);
                if (!prepared.has_value() || !store_out.has_value()) {
                    return std::nullopt;
                }
                rc::ImportStepInput i0;
                i0.observed_ordinal = ord0;
                i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                if (!rc::next_import(*prepared, i0).has_value()) {
                    return std::nullopt;
                }
                auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
                if (!plan.has_value()) {
                    return std::nullopt;
                }
                auto pub = store_out->publish_available(wf, ckpt, plan->expected_generation(),
                                                        plan->record(), plan->slots(), id_span,
                                                        key);
                if (!rc::ack_publish_injected(*prepared, pub).has_value()) {
                    return std::nullopt;
                }
                rc::ImportStepInput i1;
                i1.observed_ordinal = ord1;
                i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                i1.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                auto d1 = rc::next_import(*prepared, i1);
                if (!d1.has_value() || !std::holds_alternative<rc::ReadyForLive>(*d1)) {
                    return std::nullopt;
                }
                return std::move(*prepared);
            };
            // E1a: the ReadyForLive payload itself (owned arity-1 Value + computed hash).
            {
                const fs::path work = base / "e-readylive";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                check(prepared.has_value(), "E.readylive_prepared");
                if (prepared.has_value()) {
                    rc::ImportStepInput i0;
                    i0.observed_ordinal = ord0;
                    i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                    check(rc::next_import(*prepared, i0).has_value(), "E.readylive_needslot");
                    auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
                    check(plan.has_value(), "E.readylive_bind");
                    if (plan.has_value()) {
                        auto pub = store->publish_available(wf, ckpt, plan->expected_generation(),
                                                            plan->record(), plan->slots(), id_span,
                                                            key);
                        check(rc::ack_publish_injected(*prepared, pub).has_value(),
                              "E.readylive_ack");
                        rc::ImportStepInput i1;
                        i1.observed_ordinal = ord1;
                        i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                        i1.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                        auto d1 = rc::next_import(*prepared, i1);
                        check(d1.has_value() && std::holds_alternative<rc::ReadyForLive>(*d1),
                              "E.ready_for_live");
                        if (d1.has_value() && std::holds_alternative<rc::ReadyForLive>(*d1)) {
                            const auto &rl = std::get<rc::ReadyForLive>(*d1);
                            check(rl.params.size() == 1 && rl.arg_hash == ah1,
                                  "E.readylive_owned_param_and_hash");
                        }
                    }
                }
                nuke(work);
            }
            // E1b: next_import in AwaitingLiveResult (fresh fixture) -> TransitionInvalid.
            {
                const fs::path work = base / "e-live-next";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1b.reach_live");
                if (prepared.has_value()) {
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord1;
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::TransitionInvalid),
                          "E.awaiting_live_next_invalid");
                }
                nuke(work);
            }
            // E1c: finish_run in AwaitingLiveResult (fresh fixture) -> TransitionInvalid.
            {
                const fs::path work = base / "e-live-finish";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1c.reach_live");
                if (prepared.has_value()) {
                    rc::Run2Returned ret;
                    ret.raw_status = AHFL_CAP_OK;
                    ret.whole_linear_memory = std::span<const std::uint8_t>(mem_p3);
                    auto fin = rc::finish_run(*prepared, rc::Run2Exit{ret});
                    check(!fin.has_value() &&
                              is_step_reason(fin.error(), rc::ResumeStepReason::TransitionInvalid),
                          "E.awaiting_live_finish_invalid");
                }
                nuke(work);
            }
            // E1d: bind_publish_injected in AwaitingLiveResult (fresh fixture) -> Invalid.
            {
                const fs::path work = base / "e-live-bind";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1d.reach_live");
                if (prepared.has_value()) {
                    auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{6});
                    check(!plan.has_value() &&
                              is_step_reason(plan.error(), rc::ResumeStepReason::TransitionInvalid),
                          "E.awaiting_live_bind_invalid");
                }
                nuke(work);
            }
            // E1e: ack_publish_injected in AwaitingLiveResult (fresh fixture) -> Invalid.
            {
                const fs::path work = base / "e-live-puback";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1e.reach_live");
                if (prepared.has_value()) {
                    std::expected<std::uint64_t, ps::PayloadStoreError> m(3);
                    auto ack = rc::ack_publish_injected(*prepared, m);
                    check(!ack.has_value() &&
                              is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                          "E.awaiting_live_publish_ack_invalid");
                }
                nuke(work);
            }
            // E1f: ack_mark_consumed in AwaitingLiveResult (fresh fixture) -> Invalid.
            {
                const fs::path work = base / "e-live-conack";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1f.reach_live");
                if (prepared.has_value()) {
                    std::expected<std::uint64_t, ps::PayloadStoreError> m(3);
                    auto ack = rc::ack_mark_consumed(*prepared, m);
                    check(!ack.has_value() &&
                              is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                          "E.awaiting_live_consume_ack_invalid");
                }
                nuke(work);
            }
            // E1g: Failed IS terminal -- after a first fault, a second call still rejects.
            {
                const fs::path work = base / "e-failed-terminal";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = reach_live(work, store);
                check(prepared.has_value(), "E1g.reach_live");
                if (prepared.has_value()) {
                    // First invalid call -> Failed.
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord1;
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                    check(!rc::next_import(*prepared, in).has_value(), "E1g.first_fault");
                    // Second call on the now-Failed handle also rejects.
                    auto again = rc::next_import(*prepared, in);
                    check(!again.has_value() &&
                              is_step_reason(again.error(),
                                             rc::ResumeStepReason::TransitionInvalid),
                          "E.failed_is_terminal");
                }
                nuke(work);
            }

            // E2: Param schema-invalid at the frontier -> PayloadSchemaInvalid.
            {
                const fs::path work = base / "e-param-invalid";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                if (prepared.has_value()) {
                    const std::vector<std::uint8_t> bad_param = {'x'};
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord0;
                    in.module_param_frame = std::span<const std::uint8_t>(bad_param);
                    in.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(),
                                             rc::ResumeStepReason::PayloadSchemaInvalid),
                          "E.param_schema_invalid");
                } else {
                    check(false, "E.param_invalid_setup");
                }
                nuke(work);
            }
            // E3: ledger arg_hash mismatch -> CoordinateMismatch. Build a record whose
            // pending arg_hash is wrong (but still passes phase-1 identity, which ignores
            // arg_hash), so next_import's hash compare rejects.
            {
                const fs::path work = base / "e-arghash";
                auto store = open_store(work);
                if (store.has_value()) {
                    CoreWasmResumeRecord r = d_record(m3, ah0 ^ 0xABCDEF); // wrong pending hash
                    auto gated =
                        publish_and_gate(*store, r, {ps::Slot{PayloadSlotId{9}, kEntryBytes}}, m3);
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                               std::span<const std::uint8_t>(),
                                                               cap);
                        if (outcome.has_value() &&
                            std::holds_alternative<rc::PendingInjection>(*outcome)) {
                            auto prepared = rc::supply_injected_result(
                                std::get<rc::PendingInjection>(std::move(*outcome)),
                                std::span<const std::uint8_t>(injected));
                            if (prepared.has_value()) {
                                rc::ImportStepInput in;
                                in.observed_ordinal = ord0;
                                in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                                in.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                                auto d = rc::next_import(*prepared, in);
                                check(!d.has_value() &&
                                          is_step_reason(d.error(),
                                                         rc::ResumeStepReason::CoordinateMismatch),
                                      "E.ledger_arg_hash_mismatch");
                            } else {
                                check(false, "E.arghash_prepared");
                            }
                        } else {
                            check(false, "E.arghash_pending");
                        }
                    } else {
                        check(false, "E.arghash_gated");
                    }
                } else {
                    check(false, "E.arghash_store");
                }
                nuke(work);
            }
            // E4: event decoder malformed (bad header pad) -> EventMalformed.
            {
                const fs::path work = base / "e-malformed";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                if (prepared.has_value()) {
                    std::vector<std::uint8_t> bad = mem_p1;
                    bad[kEventLogBase + 4] = 0x01; // header pad nonzero -> decoder BadHeaderPad
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord0;
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(bad);
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::EventMalformed),
                          "E.event_malformed");
                } else {
                    check(false, "E.malformed_setup");
                }
                nuke(work);
            }
            // E5: valid decode but count != expected schedule_pos -> CoordinateMismatch.
            {
                const fs::path work = base / "e-count";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                if (prepared.has_value()) {
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord0;
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(mem_p2); // count 2 != 1
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::CoordinateMismatch),
                          "E.event_count_mismatch");
                } else {
                    check(false, "E.count_setup");
                }
                nuke(work);
            }
            // E6: wrong observed import ordinal -> CoordinateMismatch (cursor authority).
            {
                const fs::path work = base / "e-ordinal";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                if (prepared.has_value()) {
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord1; // cursor 0 expects ord0
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::CoordinateMismatch),
                          "E.wrong_ordinal");
                } else {
                    check(false, "E.ordinal_setup");
                }
                nuke(work);
            }
            // E7: below-frontier ReturnMemo returns the EXACT committed slot bytes. A record
            // with committed cap 41 (memo slot 5) and frontier cap 42; replaying import 0
            // returns node 41's admitted result bytes verbatim.
            {
                const std::vector<std::uint8_t> memo_bytes(kStringResultJson.begin(),
                                                           kStringResultJson.end());
                const fs::path work = base / "e-below-frontier-bytes";
                auto store = open_store(work);
                check(store.has_value(), "E7.store");
                if (store.has_value()) {
                    CoreWasmResumeRecord r;
                    r.format_version = 1;
                    set_matching_digests(r, m3);
                    r.entry_id = wf;
                    r.entry_input_slot = PayloadSlotId{9};
                    r.suspended_node_id = CoreWorkflowNodeId{42};
                    r.resume_state = ResumeState::Suspended;
                    ResumeNode n0;
                    n0.workflow_node_id = CoreWorkflowNodeId{40};
                    n0.schedule_pos = 0;
                    n0.node_kind = NodeKind::Identity;
                    ResumeNode n1;
                    n1.workflow_node_id = CoreWorkflowNodeId{41};
                    n1.schedule_pos = 1;
                    n1.node_kind = NodeKind::Capability;
                    ResumeMemoEntry m1;
                    m1.invocation_ordinal = InvocationOrdinal{0};
                    m1.capability = CoreCapabilityId{3};
                    m1.source_symbol = 900;
                    m1.arg_hash = ah0;
                    m1.result_slot = PayloadSlotId{5};
                    n1.memo.push_back(m1);
                    ResumeNode n2;
                    n2.workflow_node_id = CoreWorkflowNodeId{42};
                    n2.schedule_pos = 2;
                    n2.node_kind = NodeKind::Capability;
                    ResumePendingEntry p;
                    p.invocation_ordinal = InvocationOrdinal{0};
                    p.capability = CoreCapabilityId{4};
                    p.source_symbol = 901;
                    p.arg_hash = ah1;
                    n2.pending = p;
                    r.nodes = {n0, n1, n2};
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, memo_bytes}};
                    auto gated = publish_and_gate(*store, r, slots, m3);
                    check(gated.has_value() && gated->has_value(), "E7.gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                               std::span<const std::uint8_t>(),
                                                               cap);
                        check(outcome.has_value() &&
                                  std::holds_alternative<rc::PendingInjection>(*outcome),
                              "E7.pending");
                        if (outcome.has_value() &&
                            std::holds_alternative<rc::PendingInjection>(*outcome)) {
                            auto prepared = rc::supply_injected_result(
                                std::get<rc::PendingInjection>(std::move(*outcome)),
                                std::span<const std::uint8_t>(injected));
                            check(prepared.has_value(), "E7.prepared");
                            if (prepared.has_value()) {
                                rc::ImportStepInput i0;
                                i0.observed_ordinal = ord0;
                                i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                                i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                                auto d = rc::next_import(*prepared, i0);
                                check(d.has_value() && std::holds_alternative<rc::ReturnMemo>(*d),
                                      "E7.return_memo");
                                if (d.has_value() && std::holds_alternative<rc::ReturnMemo>(*d)) {
                                    const auto &rm = std::get<rc::ReturnMemo>(*d);
                                    check(std::vector<std::uint8_t>(rm.memo_bytes.begin(),
                                                                    rm.memo_bytes.end()) ==
                                              memo_bytes,
                                          "E.below_frontier_return_memo_exact_bytes");
                                }
                            }
                        }
                    }
                }
                nuke(work);
            }
            // E8: a VALID decoded prefix whose capability coordinate disagrees with the
            // manifest join -> CoordinateMismatch (distinct from the count-mismatch case). The
            // published record for schedule slot 1 carries a wrong capability id.
            {
                const fs::path work = base / "e-coord-join";
                std::optional<ps::IntegrityPayloadStore> store;
                auto prepared = prep_e(store, work);
                check(prepared.has_value(), "E8.prepared");
                if (prepared.has_value()) {
                    // A well-framed 1-record prefix, but node 0 is marked a CAPABILITY event
                    // (kind mismatch: manifest node 0 is identity) -> valid decode, join fails.
                    std::vector<ManifestNodeSpec> tampered = spec.nodes;
                    tampered[0] = ManifestNodeSpec{40, 1, 999, 12345}; // pretend identity is a cap
                    const auto bad = event_memory(tampered, 1);
                    rc::ImportStepInput in;
                    in.observed_ordinal = ord0;
                    in.module_param_frame = std::span<const std::uint8_t>(param_frame);
                    in.whole_linear_memory = std::span<const std::uint8_t>(bad);
                    auto d = rc::next_import(*prepared, in);
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::CoordinateMismatch),
                          "E.event_join_kind_mismatch");
                } else {
                    check(false, "E8.setup");
                }
                nuke(work);
            }
            // (AwaitingLiveResult rejection of bind + both ACK APIs is proven per-API with
            // fresh fixtures in E1d/E1e/E1f above, so no combined-handle E9 case here -- a
            // single handle would only prove "Failed rejects" after the first fault.)
            // E10: Injected-record frontier ReturnMemo (no second CAS). An Injected record
            // whose frontier node 42 already carries a committed ordinal-0 memo (result slot
            // 6); replaying import 1 returns ReturnMemo from the Injected branch, and a
            // subsequent bind_publish_injected is TransitionInvalid (state is Replaying, not
            // AwaitingSlot) -> the already-committed injected frontier is never re-CAS'd.
            {
                const std::vector<std::uint8_t> good(kStringResultJson.begin(),
                                                     kStringResultJson.end());
                const fs::path work = base / "e-injected-frontier-memo";
                auto store = open_store(work);
                check(store.has_value(), "E10.store");
                if (store.has_value()) {
                    CoreWasmResumeRecord r;
                    r.format_version = 1;
                    set_matching_digests(r, m3);
                    r.entry_id = wf;
                    r.entry_input_slot = PayloadSlotId{9};
                    r.suspended_node_id = CoreWorkflowNodeId{42};
                    r.resume_state = ResumeState::Injected;
                    ResumeNode n0;
                    n0.workflow_node_id = CoreWorkflowNodeId{40};
                    n0.schedule_pos = 0;
                    n0.node_kind = NodeKind::Identity;
                    ResumeNode n1;
                    n1.workflow_node_id = CoreWorkflowNodeId{41};
                    n1.schedule_pos = 1;
                    n1.node_kind = NodeKind::Capability;
                    ResumeMemoEntry m1;
                    m1.invocation_ordinal = InvocationOrdinal{0};
                    m1.capability = CoreCapabilityId{3};
                    m1.source_symbol = 900;
                    m1.arg_hash = ah0;
                    m1.result_slot = PayloadSlotId{5};
                    n1.memo.push_back(m1);
                    ResumeNode n2;
                    n2.workflow_node_id = CoreWorkflowNodeId{42};
                    n2.schedule_pos = 2;
                    n2.node_kind = NodeKind::Capability;
                    ResumeMemoEntry m2; // frontier already committed (Injected)
                    m2.invocation_ordinal = InvocationOrdinal{0};
                    m2.capability = CoreCapabilityId{4};
                    m2.source_symbol = 901;
                    m2.arg_hash = ah1;
                    m2.result_slot = PayloadSlotId{6};
                    n2.memo.push_back(m2);
                    r.nodes = {n0, n1, n2};
                    std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                                   ps::Slot{PayloadSlotId{5}, good},
                                                   ps::Slot{PayloadSlotId{6}, good}};
                    auto gated = publish_and_gate(*store, r, slots, m3);
                    check(gated.has_value() && gated->has_value(), "E10.gated");
                    if (gated.has_value() && gated->has_value()) {
                        // Injected record admits no injected input.
                        auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                               std::span<const std::uint8_t>(),
                                                               cap);
                        check(outcome.has_value() &&
                                  std::holds_alternative<rc::PreparedResume>(*outcome),
                              "E10.prepared_directly");
                        if (outcome.has_value() &&
                            std::holds_alternative<rc::PreparedResume>(*outcome)) {
                            auto prepared = std::get<rc::PreparedResume>(std::move(*outcome));
                            // Replay import 0, then the Injected frontier import 1 -> ReturnMemo.
                            rc::ImportStepInput i0;
                            i0.observed_ordinal = ord0;
                            i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                            check(rc::next_import(prepared, i0).has_value(), "E10.replay0");
                            rc::ImportStepInput i1;
                            i1.observed_ordinal = ord1;
                            i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i1.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                            auto d1 = rc::next_import(prepared, i1);
                            check(d1.has_value() && std::holds_alternative<rc::ReturnMemo>(*d1),
                                  "E.injected_frontier_return_memo");
                            // bind is now invalid (Replaying, not AwaitingSlot) -> no second CAS.
                            auto plan = rc::bind_publish_injected(prepared, PayloadSlotId{7});
                            check(!plan.has_value() &&
                                      is_step_reason(plan.error(),
                                                     rc::ResumeStepReason::TransitionInvalid),
                                  "E.injected_frontier_no_second_cas");
                        }
                    }
                }
                nuke(work);
            }
        }
    }
    {
        // E11: an EXTRA import after all expected imports are consumed. On the shared 1-call
        // module, reaching the frontier + publish ACK advances the cursor to 1 ==
        // call_site_count; a further next_import finds cursor past the last call site ->
        // CoordinateMismatch (distinct from the wrong-observed-ordinal-at-cursor-0 case).
        const fs::path work = base / "e-extra-after-end";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "E11.prepared");
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "E11.needslot");
            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            check(plan.has_value(), "E11.bind");
            if (plan.has_value()) {
                auto pub = store->publish_available(wf, ckpt, plan->expected_generation(),
                                                    plan->record(), plan->slots(), id_span, key);
                check(rc::ack_publish_injected(*prepared, pub).has_value(), "E11.ack");
                // cursor is now 1 == call_site_count; one more next_import is out of range.
                rc::ImportStepInput extra;
                extra.observed_ordinal = import_ord0;
                extra.module_param_frame = std::span<const std::uint8_t>(param_frame);
                extra.whole_linear_memory = std::span<const std::uint8_t>(mem2);
                auto d = rc::next_import(*prepared, extra);
                check(!d.has_value() &&
                          is_step_reason(d.error(), rc::ResumeStepReason::CoordinateMismatch),
                      "E.extra_import_after_end");
            }
        }
        nuke(work);
    }

    // ================= GROUP F: publish / ACK states + span stability =============
    {
        // F1: bind with an invalid slot (kInvalid) -> TransitionInvalid + Failed.
        const fs::path work = base / "f-invalid-slot";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "F.invalid_prepared");
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "F.invalid_needslot");
            auto plan =
                rc::bind_publish_injected(*prepared, PayloadSlotId{PayloadSlotId::kInvalid});
            check(!plan.has_value() &&
                      is_step_reason(plan.error(), rc::ResumeStepReason::TransitionInvalid),
                  "F.bind_invalid_slot");
            // Failed is terminal: a subsequent next_import is TransitionInvalid.
            auto after = rc::next_import(*prepared, in);
            check(!after.has_value() &&
                      is_step_reason(after.error(), rc::ResumeStepReason::TransitionInvalid),
                  "F.failed_is_terminal");
        }
        nuke(work);
    }
    {
        // F2: ACK store error preserved verbatim -> the exact PayloadStoreError arm + Failed.
        const fs::path work = base / "f-store-err";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "F.storerr_needslot");
            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            check(plan.has_value(), "F.storerr_bind");
            if (plan.has_value()) {
                std::expected<std::uint64_t, ps::PayloadStoreError> fail =
                    std::unexpected(ps::PayloadStoreError::WriteFailed);
                auto ack = rc::ack_publish_injected(*prepared, fail);
                check(!ack.has_value() && is_store_error(ack.error(),
                                                         ps::PayloadStoreError::WriteFailed),
                      "F.ack_store_error_verbatim");
            }
        } else {
            check(false, "F.storerr_setup");
        }
        nuke(work);
    }
    {
        // F3: ACK generation mismatch (N+1 != M) -> TransitionInvalid + Failed.
        const fs::path work = base / "f-gen-mismatch";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "F.gen_needslot");
            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            if (plan.has_value()) {
                std::expected<std::uint64_t, ps::PayloadStoreError> wrong_m(99); // expected 2
                auto ack = rc::ack_publish_injected(*prepared, wrong_m);
                check(!ack.has_value() &&
                          is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                      "F.ack_gen_mismatch");
            } else {
                check(false, "F.gen_bind");
            }
        } else {
            check(false, "F.gen_setup");
        }
        nuke(work);
    }
    {
        // F4: wrong-state ACK (ack before bind) -> TransitionInvalid.
        const fs::path work = base / "f-wrong-state";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        if (prepared.has_value()) {
            std::expected<std::uint64_t, ps::PayloadStoreError> m(2);
            auto ack = rc::ack_publish_injected(*prepared, m); // still Replaying
            check(!ack.has_value() &&
                      is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                  "F.ack_wrong_state");
        } else {
            check(false, "F.wrong_state_setup");
        }
        nuke(work);
    }
    {
        // F5: bind collides with an EXISTING committed memo slot -> TransitionInvalid. Build a
        // 3-node record: identity 40, committed cap 41 (memo slot 5), frontier cap 42 (pending).
        // At the frontier, binding the chosen slot to 5 collides with node 41's memo slot.
        ModuleSpec spec;
        spec.entry_id = 7;
        spec.extra_schema_nodes = {bounded_string_node(8)};
        spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                     CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                      ManifestNodeSpec{42, 1, 4, 901}};
        auto mr = admit_module(spec);
        check(mr.ok(), "F5.module");
        if (mr.ok()) {
            const auto &m3 = *mr.module;
            const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
            const std::uint64_t ah1 = param_arg_hash(m3, 1, kIntParamJson);
            const std::vector<std::uint8_t> good(kStringResultJson.begin(),
                                                kStringResultJson.end());
            const std::vector<std::uint8_t> mem_p1 = event_memory(spec.nodes, 1);
            const std::vector<std::uint8_t> mem_p2 = event_memory(spec.nodes, 2);
            const auto ord1 = m3.resolve(csm::ManifestCallSiteIndex{1}).call_site->import_ordinal();
            const fs::path work = base / "f-memo-collision";
            auto store = open_store(work);
            check(store.has_value(), "F5.store");
            if (store.has_value()) {
                CoreWasmResumeRecord r;
                r.format_version = 1;
                set_matching_digests(r, m3);
                r.entry_id = wf;
                r.entry_input_slot = PayloadSlotId{9};
                r.suspended_node_id = CoreWorkflowNodeId{42};
                r.resume_state = ResumeState::Suspended;
                ResumeNode n0;
                n0.workflow_node_id = CoreWorkflowNodeId{40};
                n0.schedule_pos = 0;
                n0.node_kind = NodeKind::Identity;
                ResumeNode n1;
                n1.workflow_node_id = CoreWorkflowNodeId{41};
                n1.schedule_pos = 1;
                n1.node_kind = NodeKind::Capability;
                ResumeMemoEntry m1;
                m1.invocation_ordinal = InvocationOrdinal{0};
                m1.capability = CoreCapabilityId{3};
                m1.source_symbol = 900;
                m1.arg_hash = ah0;
                m1.result_slot = PayloadSlotId{5};
                n1.memo.push_back(m1);
                ResumeNode n2;
                n2.workflow_node_id = CoreWorkflowNodeId{42};
                n2.schedule_pos = 2;
                n2.node_kind = NodeKind::Capability;
                ResumePendingEntry p;
                p.invocation_ordinal = InvocationOrdinal{0};
                p.capability = CoreCapabilityId{4};
                p.source_symbol = 901;
                p.arg_hash = ah1;
                n2.pending = p;
                r.nodes = {n0, n1, n2};
                std::vector<ps::Slot> slots = {ps::Slot{PayloadSlotId{9}, kEntryBytes},
                                               ps::Slot{PayloadSlotId{5}, good}};
                auto gated = publish_and_gate(*store, r, slots, m3);
                check(gated.has_value() && gated->has_value(), "F5.gated");
                if (gated.has_value() && gated->has_value()) {
                    auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                           std::span<const std::uint8_t>(), cap);
                    check(outcome.has_value() &&
                              std::holds_alternative<rc::PendingInjection>(*outcome),
                          "F5.pending");
                    if (outcome.has_value() &&
                        std::holds_alternative<rc::PendingInjection>(*outcome)) {
                        auto prepared = rc::supply_injected_result(
                            std::get<rc::PendingInjection>(std::move(*outcome)),
                            std::span<const std::uint8_t>(injected));
                        check(prepared.has_value(), "F5.prepared");
                        if (prepared.has_value()) {
                            // Replay import 0 (committed memo) then reach the frontier import 1.
                            const auto ord0 =
                                m3.resolve(csm::ManifestCallSiteIndex{0})
                                    .call_site->import_ordinal();
                            rc::ImportStepInput i0;
                            i0.observed_ordinal = ord0;
                            i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                            check(rc::next_import(*prepared, i0).has_value(), "F5.replay0");
                            rc::ImportStepInput i1;
                            i1.observed_ordinal = ord1;
                            i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                            i1.whole_linear_memory = std::span<const std::uint8_t>(mem_p2);
                            check(rc::next_import(*prepared, i1).has_value(), "F5.frontier");
                            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
                            check(!plan.has_value() &&
                                      is_step_reason(plan.error(),
                                                     rc::ResumeStepReason::TransitionInvalid),
                                  "F.bind_collides_existing_memo_slot");
                        }
                    }
                }
            }
            nuke(work);
        }
    }
    {
        // F6: duplicate publish ACK after a SUCCESSFUL ack -> TransitionInvalid (the controller
        // returned to Replaying; a second ACK is a wrong-state / late ACK). Also proves the
        // successful injected ACK span stays byte-stable across the later finish transition,
        // and the store stays Available before the terminal command.
        const fs::path work = base / "f-dup-ack";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "F6.prepared");
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "F6.needslot");
            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            check(plan.has_value(), "F6.bind");
            if (plan.has_value()) {
                auto pub = store->publish_available(wf, ckpt, plan->expected_generation(),
                                                    plan->record(), plan->slots(), id_span, key);
                auto span1 = rc::ack_publish_injected(*prepared, pub);
                check(span1.has_value(), "F6.first_ack");
                const std::uint8_t *span_data = span1.has_value() ? span1->data() : nullptr;
                // The store is Available at gen 2 before any terminal command.
                auto pre = store->load(wf, ckpt, id_span, key);
                check(pre.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*pre) &&
                          std::get<ps::ResolvedAvailable>(*pre).generation == 2,
                      "F6.store_available_pre_terminal");
                // A duplicate ACK (now in Replaying) -> TransitionInvalid + Failed.
                std::expected<std::uint64_t, ps::PayloadStoreError> again(2);
                auto dup = rc::ack_publish_injected(*prepared, again);
                check(!dup.has_value() &&
                          is_step_reason(dup.error(), rc::ResumeStepReason::TransitionInvalid),
                      "F.duplicate_publish_ack_invalid");
                // The first ACK span is unaffected by the failed duplicate.
                if (span1.has_value()) {
                    check(span1->data() == span_data &&
                              std::vector<std::uint8_t>(span1->begin(), span1->end()) == injected,
                          "F.injected_ack_span_stable");
                }
            }
        }
        nuke(work);
    }
    {
        // F7: a FAILED publish ACK (store error) leaves the store's newest generation the
        // pre-command Available (the controller never wrote; the test never published).
        const fs::path work = base / "f-failed-publish-available";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "F7.prepared");
        if (prepared.has_value()) {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            check(rc::next_import(*prepared, in).has_value(), "F7.needslot");
            auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
            check(plan.has_value(), "F7.bind");
            if (plan.has_value()) {
                std::expected<std::uint64_t, ps::PayloadStoreError> fail =
                    std::unexpected(ps::PayloadStoreError::WriteFailed);
                auto ack = rc::ack_publish_injected(*prepared, fail);
                check(!ack.has_value(), "F7.ack_failed");
                // The store's live generation is still the original Suspended gen 1.
                auto live = store->load(wf, ckpt, id_span, key);
                check(live.has_value() && std::holds_alternative<ps::ResolvedAvailable>(*live) &&
                          std::get<ps::ResolvedAvailable>(*live).generation == 1 &&
                          std::get<ps::ResolvedAvailable>(*live).record.resume_state ==
                              ResumeState::Suspended,
                      "F.failed_publish_store_unchanged");
            }
        }
        nuke(work);
    }
    {
        // G1: immediate finish (cursor 0) -> TransitionInvalid, no plan.
        const fs::path work = base / "g-immediate";
        std::optional<ps::IntegrityPayloadStore> store;
        auto prepared = make_prepared(work, store);
        check(prepared.has_value(), "G.immediate_prepared");
        if (prepared.has_value()) {
            rc::Run2Returned ret;
            ret.raw_status = AHFL_CAP_OK;
            ret.whole_linear_memory = std::span<const std::uint8_t>(mem2);
            auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
            check(!mc.has_value() &&
                      is_step_reason(mc.error(), rc::ResumeStepReason::TransitionInvalid),
                  "G.immediate_finish_invalid");
        }
        nuke(work);
    }
    {
        // G2: raw ABI status classification (ERROR / PENDING / unknown / trap) after the
        // cursor has advanced past the frontier (so only classification is under test).
        const auto drive_to_live = [&](rc::PreparedResume &prepared,
                                       ps::IntegrityPayloadStore &store) -> bool {
            rc::ImportStepInput in;
            in.observed_ordinal = import_ord0;
            in.module_param_frame = std::span<const std::uint8_t>(param_frame);
            in.whole_linear_memory = std::span<const std::uint8_t>(mem1);
            if (!rc::next_import(prepared, in).has_value()) {
                return false;
            }
            auto plan = rc::bind_publish_injected(prepared, PayloadSlotId{5});
            if (!plan.has_value()) {
                return false;
            }
            auto pub = store.publish_available(wf, ckpt, plan->expected_generation(),
                                               plan->record(), plan->slots(), id_span, key);
            return rc::ack_publish_injected(prepared, pub).has_value();
        };
        const auto status_case = [&](const std::string &name, std::uint32_t raw,
                                     rc::ResumeStepReason want) {
            const fs::path work = base / ("g-" + name);
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                rc::Run2Returned ret;
                ret.raw_status = raw;
                ret.whole_linear_memory = std::span<const std::uint8_t>(mem2);
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                check(!mc.has_value() && is_step_reason(mc.error(), want), "G." + name);
            } else {
                check(false, "G." + name + ".setup");
            }
            nuke(work);
        };
        status_case("error", AHFL_CAP_ERROR, rc::ResumeStepReason::ModuleError);
        status_case("pending", AHFL_CAP_PENDING, rc::ResumeStepReason::TransitionInvalid);
        status_case("unknown", 4242, rc::ResumeStepReason::ModuleError);
        {
            const fs::path work = base / "g-trap";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{rc::Run2Trapped{}});
                check(!mc.has_value() &&
                          is_step_reason(mc.error(), rc::ResumeStepReason::ModuleTrap),
                      "G.trap_module_trap");
            } else {
                check(false, "G.trap.setup");
            }
            nuke(work);
        }
        {
            // G3: OK but a full-prefix event mismatch -> CoordinateMismatch (not consume).
            const fs::path work = base / "g-prefix-mismatch";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                // A full 2-record prefix but node 0 carries a wrong workflow_node_id.
                std::vector<ManifestNodeSpec> tampered = base_nodes;
                tampered[0].workflow_node_id = 4040;
                const auto bad_mem = event_memory(tampered, 2);
                rc::Run2Returned ret;
                ret.raw_status = AHFL_CAP_OK;
                ret.whole_linear_memory = std::span<const std::uint8_t>(bad_mem);
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                check(!mc.has_value() &&
                          is_step_reason(mc.error(), rc::ResumeStepReason::CoordinateMismatch),
                      "G.full_prefix_mismatch");
            } else {
                check(false, "G.prefix_mismatch.setup");
            }
            nuke(work);
        }
        {
            // G4: mark_consumed store error preserved; then a fresh run: N+1 mismatch.
            const fs::path work = base / "g-consume-err";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                rc::Run2Returned ret;
                ret.raw_status = AHFL_CAP_OK;
                ret.whole_linear_memory = std::span<const std::uint8_t>(mem2);
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                check(mc.has_value(), "G.consume_err_finish");
                if (mc.has_value()) {
                    std::expected<std::uint64_t, ps::PayloadStoreError> fail =
                        std::unexpected(ps::PayloadStoreError::WriteFailed);
                    auto ack = rc::ack_mark_consumed(*prepared, fail);
                    check(!ack.has_value() &&
                              is_store_error(ack.error(), ps::PayloadStoreError::WriteFailed),
                          "G.consume_store_error_verbatim");
                }
            } else {
                check(false, "G.consume_err.setup");
            }
            nuke(work);
        }
        {
            // G5: partial-replay OK negative on a MULTI-call module. A 3-node module whose
            // frontier is node 41; after bind+ACK the cursor is at 1 but call_site_count is 2,
            // so finish(OK) before the second import is a partial replay -> TransitionInvalid.
            ModuleSpec spec;
            spec.entry_id = 7;
            spec.extra_schema_nodes = {bounded_string_node(8)};
            spec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                         CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
            spec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                          ManifestNodeSpec{42, 1, 4, 901}};
            auto mr =
                admit_module(spec);
            check(mr.ok(), "G5.module");
            if (mr.ok()) {
                const auto &m3 = *mr.module;
                const std::uint64_t ah0 = param_arg_hash(m3, 0, kIntParamJson);
                const std::vector<std::uint8_t> mem_p1 = event_memory(spec.nodes, 1);
                const std::vector<std::uint8_t> mem_p3 = event_memory(spec.nodes, 3);
                const auto ord0 =
                    m3.resolve(csm::ManifestCallSiteIndex{0}).call_site->import_ordinal();
                const fs::path work = base / "g-partial-replay";
                auto store = open_store(work);
                check(store.has_value(), "G5.store");
                if (store.has_value()) {
                    auto gated =
                        publish_and_gate(*store, d_record(m3, ah0),
                                         {ps::Slot{PayloadSlotId{9}, kEntryBytes}}, m3);
                    check(gated.has_value() && gated->has_value(), "G5.gated");
                    if (gated.has_value() && gated->has_value()) {
                        auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                               std::span<const std::uint8_t>(),
                                                               cap);
                        check(outcome.has_value() &&
                                  std::holds_alternative<rc::PendingInjection>(*outcome),
                              "G5.pending");
                        if (outcome.has_value() &&
                            std::holds_alternative<rc::PendingInjection>(*outcome)) {
                            auto prepared = rc::supply_injected_result(
                                std::get<rc::PendingInjection>(std::move(*outcome)),
                                std::span<const std::uint8_t>(injected));
                            check(prepared.has_value(), "G5.prepared");
                            if (prepared.has_value()) {
                                rc::ImportStepInput i0;
                                i0.observed_ordinal = ord0;
                                i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                                i0.whole_linear_memory = std::span<const std::uint8_t>(mem_p1);
                                check(rc::next_import(*prepared, i0).has_value(), "G5.frontier");
                                auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
                                check(plan.has_value(), "G5.bind");
                                if (plan.has_value()) {
                                    auto pub = store->publish_available(
                                        wf, ckpt, plan->expected_generation(), plan->record(),
                                        plan->slots(), id_span, key);
                                    check(rc::ack_publish_injected(*prepared, pub).has_value(),
                                          "G5.ack");
                                    // cursor now 1, call_site_count 2: a partial finish.
                                    rc::Run2Returned ret;
                                    ret.raw_status = AHFL_CAP_OK;
                                    ret.whole_linear_memory = std::span<const std::uint8_t>(mem_p3);
                                    auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                                    check(!mc.has_value() &&
                                              is_step_reason(
                                                  mc.error(),
                                                  rc::ResumeStepReason::TransitionInvalid),
                                          "G.partial_replay_finish_invalid");
                                }
                            }
                        }
                    }
                }
                nuke(work);
            }
        }
        {
            // G6: OK with a decoder-MALFORMED full-prefix memory -> EventMalformed (distinct
            // from a valid-decode coordinate mismatch). Corrupt the event header pad.
            const fs::path work = base / "g-full-malformed";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                std::vector<std::uint8_t> bad = mem2;
                bad[kEventLogBase + 4] = 0x01; // header pad nonzero -> decoder BadHeaderPad
                rc::Run2Returned ret;
                ret.raw_status = AHFL_CAP_OK;
                ret.whole_linear_memory = std::span<const std::uint8_t>(bad);
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                check(!mc.has_value() &&
                          is_step_reason(mc.error(), rc::ResumeStepReason::EventMalformed),
                      "G.full_prefix_decoder_malformed");
            } else {
                check(false, "G.full_malformed.setup");
            }
            nuke(work);
        }
        {
            // G7: mark-consumed ACK in the wrong state (before finish_run) -> TransitionInvalid.
            const fs::path work = base / "g-consume-wrong-state";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                // Still in Replaying (finish_run not called): mark ACK is wrong-state.
                std::expected<std::uint64_t, ps::PayloadStoreError> m(3);
                auto ack = rc::ack_mark_consumed(*prepared, m);
                check(!ack.has_value() &&
                          is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                      "G.mark_consumed_wrong_state");
            } else {
                check(false, "G.consume_wrong_state.setup");
            }
            nuke(work);
        }
        {
            // G8: mark-consumed ACK generation mismatch (N+1 != M) -> TransitionInvalid.
            const fs::path work = base / "g-consume-genmismatch";
            std::optional<ps::IntegrityPayloadStore> store;
            auto prepared = make_prepared(work, store);
            if (prepared.has_value() && drive_to_live(*prepared, *store)) {
                rc::Run2Returned ret;
                ret.raw_status = AHFL_CAP_OK;
                ret.whole_linear_memory = std::span<const std::uint8_t>(mem2);
                auto mc = rc::finish_run(*prepared, rc::Run2Exit{ret});
                check(mc.has_value(), "G8.finish");
                if (mc.has_value()) {
                    std::expected<std::uint64_t, ps::PayloadStoreError> wrong(99); // expected 3
                    auto ack = rc::ack_mark_consumed(*prepared, wrong);
                    check(!ack.has_value() &&
                              is_step_reason(ack.error(), rc::ResumeStepReason::TransitionInvalid),
                          "G.mark_consumed_gen_mismatch");
                }
            } else {
                check(false, "G.consume_genmismatch.setup");
            }
            nuke(work);
        }
    }

    // ================= GROUP H: D2b-4 token-aware ReadyForLive decision seam =====
    // The controller is DECISION-ONLY: it consults the D2b authority READ-ONLY and
    // seals nothing. The AFTER-frontier ordinal-1 import (node 42, cap 4/sym 901)
    // is the ReadyForLive frontier where the verdict is surfaced.
    {
        ModuleSpec hspec;
        hspec.entry_id = 7;
        hspec.extra_schema_nodes = {bounded_string_node(8)};
        hspec.caps = {CapSpec{3, 900, CoreWireSchemaNodeId{1}},
                      CapSpec{4, 901, CoreWireSchemaNodeId{1}}};
        hspec.nodes = {ManifestNodeSpec{40, 0, 0, 0}, ManifestNodeSpec{41, 1, 3, 900},
                       ManifestNodeSpec{42, 1, 4, 901}};
        auto hmr = admit_module(hspec);
        check(hmr.ok(), "H.module_admits");
        if (hmr.ok()) {
            const auto &hm = *hmr.module;
            const std::uint64_t ah0 = param_arg_hash(hm, 0, kIntParamJson);
            const auto ord0 = hm.resolve(csm::ManifestCallSiteIndex{0}).call_site->import_ordinal();
            const auto ord1 = hm.resolve(csm::ManifestCallSiteIndex{1}).call_site->import_ordinal();
            const std::vector<std::uint8_t> mem_h1 = event_memory(hspec.nodes, 1);
            const std::vector<std::uint8_t> mem_h2 = event_memory(hspec.nodes, 2);

            std::array<std::uint8_t, 16> auth_raw{};
            for (std::size_t i = 0; i < auth_raw.size(); ++i) {
                auth_raw[i] = static_cast<std::uint8_t>(0x40 + i);
            }
            const IdempotencyAuthorityId auth_id{auth_raw};
            std::array<std::uint8_t, 16> foreign_raw{};
            for (std::size_t i = 0; i < foreign_raw.size(); ++i) {
                foreign_raw[i] = static_cast<std::uint8_t>(0x80 + i);
            }
            const IdempotencyAuthorityId foreign_id{foreign_raw};

            namespace dei = ahfl::runtime::durable_effect_intent;
            namespace dea = ahfl::runtime::durable_effect_authority;
            namespace tok = ahfl::runtime::core_wasm_idempotency_token;

            // The controller's exact canonical digest for the live call's param "1".
            const Sha256Digest digest_p1 = param_canonical_digest(hm, 1, kIntParamJson);
            // A DIVERGENT prior-attempt digest (same call site, different param "2").
            const Sha256Digest digest_p2 = param_canonical_digest(hm, 1, "2");

            // Mint an intent at the node-42 after-frontier call site under a given
            // authority id + param digest (same namespace wf/ckpt).
            const auto mint_live = [&](IdempotencyAuthorityId who, Sha256Digest digest) {
                FrozenAuthorityNamespaceBuilder b;
                static_cast<void>(b.bind(who, dei::CheckpointNamespace{wf, ckpt}));
                dei::IntentCoordinate c;
                c.checkpoint_namespace = {wf, ckpt};
                c.node = CoreWorkflowNodeId{42};
                c.ordinal = InvocationOrdinal{0};
                c.capability = CoreCapabilityId{4};
                c.source_symbol = 901;
                c.param_digest = digest;
                return b.mint(c).value();
            };

            // Publish Suspended (frontier 41), gate with the D2b-4 options, admit +
            // drive the frontier publish/ACK so the cursor sits at the live node 42.
            const auto drive_h =
                [&](const fs::path &work, std::optional<ps::IntegrityPayloadStore> &store_out,
                    const DurableEffectAuthority *dedup, IdempotencyAuthorityId who)
                -> std::optional<rc::PreparedResume> {
                store_out = open_store(work);
                if (!store_out.has_value()) {
                    return std::nullopt;
                }
                rc::GatedResumeOptions opts;
                opts.authority_id = who;
                opts.dedup_authority = dedup;
                auto gated = publish_and_gate(
                    *store_out, d_record(hm, ah0),
                    {ps::Slot{PayloadSlotId{9}, kEntryBytes}}, hm, opts);
                if (!gated.has_value() || !gated->has_value()) {
                    return std::nullopt;
                }
                auto outcome = rc::admit_and_preflight(std::move(**gated), id_span, key,
                                                       std::span<const std::uint8_t>(), cap);
                if (!outcome.has_value() ||
                    !std::holds_alternative<rc::PendingInjection>(*outcome)) {
                    return std::nullopt;
                }
                auto prepared = rc::supply_injected_result(
                    std::get<rc::PendingInjection>(std::move(*outcome)),
                    std::span<const std::uint8_t>(injected));
                if (!prepared.has_value()) {
                    return std::nullopt;
                }
                rc::ImportStepInput i0;
                i0.observed_ordinal = ord0;
                i0.module_param_frame = std::span<const std::uint8_t>(param_frame);
                i0.whole_linear_memory = std::span<const std::uint8_t>(mem_h1);
                if (!rc::next_import(*prepared, i0).has_value()) {
                    return std::nullopt;
                }
                auto plan = rc::bind_publish_injected(*prepared, PayloadSlotId{5});
                if (!plan.has_value()) {
                    return std::nullopt;
                }
                auto pub = store_out->publish_available(wf, ckpt, plan->expected_generation(),
                                                        plan->record(), plan->slots(), id_span,
                                                        key);
                if (!rc::ack_publish_injected(*prepared, pub).has_value()) {
                    return std::nullopt;
                }
                return std::move(*prepared);
            };
            // The ordinal-1 after-frontier consultation.
            const auto consult1 = [&](rc::PreparedResume &p) {
                rc::ImportStepInput i1;
                i1.observed_ordinal = ord1;
                i1.module_param_frame = std::span<const std::uint8_t>(param_frame);
                i1.whole_linear_memory = std::span<const std::uint8_t>(mem_h2);
                return rc::next_import(p, i1);
            };

            // (a) unknown token -> ReadyForLive, and the READ-ONLY preview sealed
            // NOTHING (a subsequent begin still returns a Fresh New).
            {
                const fs::path work = base / "h-fresh";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority dedup{backend};
                auto prepared = drive_h(work, store, &dedup, auth_id);
                check(prepared.has_value(), "H.fresh_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() && std::holds_alternative<rc::ReadyForLive>(*d),
                          "H.unknown_token_ready_for_live");
                    // Decision-only: the controller's preview created no row.
                    auto again = dedup.begin_effect(mint_live(auth_id, digest_p1));
                    check(again.has_value() &&
                              std::holds_alternative<dea::EffectNew>(*again),
                          "H.preview_seals_nothing");
                    auto pending =
                        dedup.recover(dei::CheckpointNamespace{wf, ckpt});
                    check(pending.has_value() && pending->size() == 1,
                          "H.fresh_then_begin_seals_exactly_one");
                }
                nuke(work);
            }
            // (b) a prior SUCCEEDED terminal -> DedupReplay{token,handle}, never
            // ReadyForLive; the handle round-trips the recorded typed bytes.
            {
                const fs::path work = base / "h-replay-ok";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority dedup{backend};
                const auto intent = mint_live(auth_id, digest_p1);
                auto begin = dedup.begin_effect(intent);
                check(begin.has_value() && std::holds_alternative<dea::EffectNew>(*begin),
                      "H.replay_seal_new");
                const std::vector<std::uint8_t> recorded = {0x52, 0x45, 0x50};
                auto rh = dedup.record_result(intent.token(), recorded);
                check(rh.has_value(), "H.replay_record_result");
                auto prepared = drive_h(work, store, &dedup, auth_id);
                check(prepared.has_value(), "H.replay_prepared");
                if (prepared.has_value() && rh.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() && std::holds_alternative<rc::DedupReplay>(*d) &&
                              !std::holds_alternative<rc::ReadyForLive>(*d),
                          "H.replay_succeeded_arm");
                    if (d.has_value() && std::holds_alternative<rc::DedupReplay>(*d)) {
                        const auto &rep = std::get<rc::DedupReplay>(*d);
                        check(rep.token == intent.token(), "H.replay_token_matches");
                        check(rep.handle == *rh, "H.replay_handle_matches");
                        auto bytes = dedup.read_result(rep.handle);
                        check(bytes.has_value() && *bytes == recorded,
                              "H.replay_handle_bytes_roundtrip");
                    }
                }
                nuke(work);
            }
            // (c) a Pending row reopened over the SAME backend (simulated crash =
            // fresh authority over the live backend) -> RecoverPending.
            {
                const fs::path work = base / "h-recover";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                const auto intent = [&] {
                    DurableEffectAuthority before{backend};
                    auto sealed = before.begin_effect(mint_live(auth_id, digest_p1));
                    check(sealed.has_value(), "H.recover_seal_before_crash");
                    return mint_live(auth_id, digest_p1);
                }();
                // Simulated crash: a brand-new authority instance over the same
                // backend, bound to the same frozen authority id.
                DurableEffectAuthority after{backend};
                auto rows = after.recover(dei::CheckpointNamespace{wf, ckpt});
                check(rows.has_value() && rows->size() == 1, "H.recover_one_pending_row");
                auto prepared = drive_h(work, store, &after, auth_id);
                check(prepared.has_value(), "H.recover_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() && std::holds_alternative<rc::RecoverPending>(*d),
                          "H.recover_pending_arm");
                    if (d.has_value() && std::holds_alternative<rc::RecoverPending>(*d)) {
                        check(std::get<rc::RecoverPending>(*d).token == intent.token(),
                              "H.recover_pending_token_matches");
                    }
                }
                nuke(work);
            }
            // (d) same call site, DIFFERENT prior param digest -> the authority
            // already sealed a Pending for another argument; preview is Diverged
            // and the controller fails closed CoordinateMismatch -> Failed.
            {
                const fs::path work = base / "h-diverged";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority dedup{backend};
                // A prior attempt sealed node 42 under the "2" digest.
                auto prior = dedup.begin_effect(mint_live(auth_id, digest_p2));
                check(prior.has_value() && std::holds_alternative<dea::EffectNew>(*prior),
                      "H.diverged_prior_sealed");
                auto prepared = drive_h(work, store, &dedup, auth_id);
                check(prepared.has_value(), "H.diverged_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared); // controller computes digest "1"
                    check(!d.has_value() &&
                              is_step_reason(d.error(), rc::ResumeStepReason::CoordinateMismatch),
                          "H.diverged_coordinate_mismatch");
                    // Failed is terminal: the next step rejects TransitionInvalid.
                    auto again = consult1(*prepared);
                    check(!again.has_value() &&
                              is_step_reason(again.error(),
                                             rc::ResumeStepReason::TransitionInvalid),
                          "H.diverged_failed_terminal");
                }
                nuke(work);
            }
            // (e) a prior recorded FAILURE -> DedupReplayFailure (no second effect).
            {
                const fs::path work = base / "h-replay-fail";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority dedup{backend};
                const auto intent = mint_live(auth_id, digest_p1);
                static_cast<void>(dedup.begin_effect(intent));
                const std::vector<std::uint8_t> failure = {0x46, 0x41, 0x49, 0x4c};
                auto fh = dedup.record_failure(intent.token(), failure);
                check(fh.has_value(), "H.fail_record_failure");
                auto prepared = drive_h(work, store, &dedup, auth_id);
                if (prepared.has_value() && fh.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() &&
                              std::holds_alternative<rc::DedupReplayFailure>(*d),
                          "H.replay_failed_arm");
                    if (d.has_value() &&
                        std::holds_alternative<rc::DedupReplayFailure>(*d)) {
                        const auto &rep = std::get<rc::DedupReplayFailure>(*d);
                        check(rep.token == intent.token() && rep.handle == *fh,
                              "H.replay_failed_token_handle");
                    }
                } else {
                    check(false, "H.replay_failed_setup");
                }
                nuke(work);
            }
            // (f) a real backend storage fault on the read -> carried verbatim as
            // DurableEffectBackendError and the controller fails closed.
            {
                const fs::path work = base / "h-backend-fault";
                std::optional<ps::IntegrityPayloadStore> store;
                auto raw = make_in_memory_durable_effect_backend();
                auto faulting = std::make_shared<FaultLookupBackend>(raw);
                DurableEffectAuthority dedup{faulting};
                auto prepared = drive_h(work, store, &dedup, auth_id);
                check(prepared.has_value(), "H.fault_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared);
                    check(!d.has_value() &&
                              is_dedup_backend_error(
                                  d.error(),
                                  dea::DurableEffectBackendError::StorageUnavailable),
                          "H.backend_fault_verbatim");
                }
                nuke(work);
            }
            // (g) a FOREIGN-authority row at the same site is isolated: the
            // controller's authority owns no row, so the verdict is still
            // ReadyForLive (never a cross-authority replay).
            {
                const fs::path work = base / "h-foreign";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                {
                    DurableEffectAuthority foreign{backend};
                    auto sealed = foreign.begin_effect(mint_live(foreign_id, digest_p1));
                    check(sealed.has_value(), "H.foreign_seal");
                }
                DurableEffectAuthority dedup{backend};
                auto prepared = drive_h(work, store, &dedup, auth_id);
                check(prepared.has_value(), "H.foreign_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() && std::holds_alternative<rc::ReadyForLive>(*d),
                          "H.foreign_isolated_ready_for_live");
                }
                nuke(work);
            }
            // (h) legacy: no authority bound (nullptr) always yields ReadyForLive
            // even when a same-token row exists in a backend the controller never
            // consults.
            {
                const fs::path work = base / "h-legacy-null";
                std::optional<ps::IntegrityPayloadStore> store;
                auto backend = make_in_memory_durable_effect_backend();
                DurableEffectAuthority dedup{backend};
                static_cast<void>(dedup.begin_effect(mint_live(auth_id, digest_p1)));
                auto prepared = drive_h(work, store, nullptr, auth_id);
                check(prepared.has_value(), "H.legacy_prepared");
                if (prepared.has_value()) {
                    auto d = consult1(*prepared);
                    check(d.has_value() && std::holds_alternative<rc::ReadyForLive>(*d),
                          "H.legacy_null_authority_ready_for_live");
                }
                nuke(work);
            }
        }
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_resume_controller: all checks passed (" << g_total
                  << " assertions)\n";
        return 0;
    }
    std::cerr << "core_wasm_resume_controller: " << g_failures << " failure(s)\n";
    return 1;
}
