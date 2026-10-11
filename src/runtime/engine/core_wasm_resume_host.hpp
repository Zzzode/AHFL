#pragma once

// RFC 0026 KR6.5 E4-B2-D2a (F4): the engine-AGNOSTIC production resume-host
// driver. It is the first non-test PRODUCTION caller of the D1b decision-only
// replay controller and of the F1/F3 fixed-page capacity / artifact-declared
// memory authorities.
//
// It executes seam section 5's ordered host transaction over the
// core_wasm_resume_engine port, owning every ACTION the D1b controller only
// decides (seam 5 step 7 + 5.3 L0-L5):
//
//   1. A2 admit the artifact; F3 cross-check its OWN declared Memory section
//      against the F1 fixed single-page SSOT BEFORE instantiation, and bind the
//      request's module bytes to the verified handle's module_sha256() so the
//      engine can never instantiate a different buffer than the one admitted.
//   2. IntegrityPayloadStore.open_snapshot -> rc::open_gated_resume (phase-1
//      digest + coordinate gates).
//   3. rc::admit_and_preflight with the F1 capacity (phase-2 slot admission,
//      eligibility, two-pass TOTAL preflight); a Suspended record with no
//      injected input is resumed via rc::supply_injected_result.
//   4. engine.fresh_instance; L0 alloc/write of the verbatim entry frame.
//   5. engine.invoke_run2; per synchronous import:
//        read whole memory + borrow the L2 Param span -> rc::next_import;
//        ReturnMemo       -> engine.alloc_then_write exact authenticated
//                             bytes, reply (OK, ptr, len), no CAS, no live call;
//        NeedInjectedSlot -> choose a fresh slot, rc::bind_publish_injected,
//                             store.publish_available(plan),
//                             rc::ack_publish_injected, alloc/write the exact
//                             new bytes, reply (OK, ptr, len);
//        ReadyForLive -> D2b-5: seal the effect intent (begin_effect) BEFORE
//                             the caller-supplied live_invoker call, record the
//                             OK/ERROR terminal, pack the canonical result;
//                             PENDING leaves the row sealed and aborts. An empty
//                             live_invoker keeps the fail-closed baseline.
//        DedupReplay / DedupReplayFailure / RecoverPending -> fail closed
//                             (the D2b-6 / D2b-7 follow-ons).
//   6. rc::finish_run(Run2Exit) -> store.mark_consumed ->
//      rc::ack_mark_consumed.
//
// Every failure is fail-CLOSED: no frame is transferred on a failing step, and
// a failed resume leaves the authenticated generation Available / unconsumed.
// Errors surface through a CLOSED typed variant; `host_code()` maps each arm
// to the F2 locked resume.* catalogue (range-less, no-echo), the stable string
// production hosts report.
//
// ENGINE EVIDENCE (WH-3, 2026-09-30): the driver is verified on three engine
// ports: (1) the synchronous in-test double (FakeResumeEngine, F4 unit tests),
// (2) the Node embedded-engine port (F5 Node e2e), and (3) the REAL vendored
// wasm3 interpreter via Wasm3ResumeEngine (core_wasm_resume_wasm3_e2e.cpp).
// On wasm3 the driver drives a real emitted capability-workflow module through
// the full run_resume transaction: below-frontier memo replay (ReturnMemo,
// zero live capability invocations), Suspended-frontier inject/publish/ack
// (NeedInjectedSlot), module trap -> ModuleTrap, and host abort surfacing
// state.abort_failure (CoordinateMismatch). The same TU also exercises the
// capability-import executor on wasm3 (bridge lane, live OK/ERROR/
// PENDING, Suspended->Injected->Consumed replay, ImportAbort). The D2b
// live-result API landed for ReadyForLive in D2b-5 (seal -> invoke ->
// record -> pack): with a caller-supplied live_invoker the driver invokes a
// live capability past the frontier under the dedup authority's seal. The
// DedupReplay / DedupReplayFailure / RecoverPending arms remain the blocked
// D2b-6 / D2b-7 follow-on, and an empty live_invoker keeps the
// ReadyForLiveBlocked baseline. B2 / KR6.5 stay false until those land.

#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"               // CoreWorkflowId
#include "runtime/engine/core_wasm_resume_controller.hpp"
#include "runtime/engine/core_wasm_resume_engine.hpp"
#include "runtime/engine/core_wasm_resume_host_codes.hpp"
#include "runtime/engine/durable_effect_authority.hpp" // DurableEffectBackendError
#include "runtime/engine/payload_store.hpp"
#include "runtime/engine/payload_store_codec.hpp" // ResumeCheckpointId, PayloadStoreError
#include "runtime/engine/core_wasm_schema_module.hpp"

namespace ahfl::runtime::core_wasm_resume_host {

namespace rc = core_wasm_resume_controller;
namespace ps = payload_store;
namespace csm = core_wasm_schema_module;
namespace engine = core_wasm_resume_engine;
namespace host_codes = core_wasm_resume_host_codes;

// Everything one resume needs. Key material is BORROWED for the call (the
// store itself never retains it); the store root and work namespace name the
// REAL IntegrityPayloadStore this driver drives.

// Packs the entry frame (wire JSON) into the entry node's P4-D I block and
// returns the (ptr, len) pair for invoke_run2. The bridge lane requires the
// entry frame at the node's fixed input region, not a bump allocation.
using EntryFramePacker = std::function<std::expected<std::pair<engine::GuestPointer, std::uint32_t>,
                                                     engine::EngineError>(
    engine::CoreWasmResumeEngine &engine, std::span<const std::uint8_t> entry_json)>;

// Packs a wire-JSON result into the P4-D bridge result block identified by
// the control-block pointer in obs.scalar_arg. The packer resolves the bridge
// call site, decodes the JSON against the call site's result binding, packs
// the value into the fixed result block, and returns the ImportReply carrying
// (AHFL_CAP_OK, result_base).
using BridgeResultPacker =
    std::function<engine::ImportCallbackResult(engine::CoreWasmResumeEngine &engine,
                                               const engine::ImportObservation &obs,
                                               const csm::VerifiedCoreWasmCallSite &call_site,
                                               std::span<const std::uint8_t> result_json)>;

// Decodes the P4-D bridge arguments from the control block identified by
// obs.scalar_arg and computes the wire-canonical arg_hash. The bridge lane
// has no wire-JSON param frame (the args live in the P4-D control block), so
// the controller needs the pre-computed hash to cross-check against the
// ledgered memo/pending. Returns nullopt on a decode failure.
using BridgeArgHasher =
    std::function<std::optional<std::uint64_t>(engine::CoreWasmResumeEngine &engine,
                                               const engine::ImportObservation &obs,
                                               const csm::VerifiedCoreWasmCallSite &call_site)>;

// D2b-5: the caller-supplied live capability response API. The driver is
// engine-agnostic: it has no string names (workflow/agent/state), no
// name_resolver and no frame_section, so the caller (which holds the
// Program/IR + capability registry + frame layout) closes over its
// ContextualCapabilityInvoker inside this lambda, exactly as it does for the
// pack_entry_frame / pack_bridge_result / bridge_arg_hasher seams.
//
// The invoker receives the TYPED params (ReadyForLive.params), never
// wire-JSON bytes. It is responsible for serialize -> Verified-decode ->
// canonical re-encode -> length-check <= the bridge site's reserved result
// bound, and returns the canonical bytes on success. The driver sees only
// canonical bytes; it never depends on the wire codec or frame_section.

// A verified live OK: the canonical wire-JSON result bytes, already
// Verified-decoded + canonical re-encoded + length-checked by the invoker.
struct LiveOk {
    std::vector<std::uint8_t> canonical_result_json;
};
// The capability returned PENDING: no Result frame exists. The driver leaves
// the sealed Pending row for recovery and aborts (the follow-on
// suspend/persist decision that forms a new pending coordinate is D2b-8).
struct LivePending {};
// The capability returned ERROR/Timeout/RetryExhausted/CircuitOpen. The
// diagnostic is abort-diagnostic only; it is recorded in the authority as
// the failure bytes so a future resume replays the terminal failure.
struct LiveError {
    std::string diagnostic;
};
using LiveCapabilityOutcome = std::variant<LiveOk, LivePending, LiveError>;

// A host-decision fault of the invoker lambda itself (NOT a capability
// execution fault): the capability name is unknown, the result failed
// Verified-decode, the canonical result exceeded the reserved bound, or the
// invoker failed outright.
enum class LiveCapabilityFault : std::uint8_t {
    CapabilityNameUnknown,
    ResultVerificationFailed,
    ResultTooLarge,
    InvokerFailed,
};

// Invoke the live capability for one ReadyForLive decision. Returns the
// outcome, or a fault if the invoker itself could not produce one.
using LiveCapabilityInvoker =
    std::function<std::expected<LiveCapabilityOutcome, LiveCapabilityFault>(
        const rc::ReadyForLive &, const engine::ImportObservation &)>;

struct ResumeRequest {
    const csm::VerifiedCoreWasmSchemaModule *module{nullptr};
    // The exact digest-admitted artifact bytes handed to the engine. They are
    // the SAME bytes the A2 module was admitted over (never re-read); run_resume
    // enforces this precondition by hashing them and comparing against the
    // verified handle's module_sha256() before fresh_instance.
    std::span<const std::uint8_t> module_bytes{};
    engine::CoreWasmResumeEngine *engine{nullptr};
    ps::IntegrityPayloadStore *store{nullptr};
    ir::core::CoreWorkflowId workflow{};
    ps::ResumeCheckpointId checkpoint{};
    // Dynamic-extent input span; run_resume validates size == 16 before it is
    // widened into the store API's fixed-extent key-id span.
    std::span<const std::uint8_t> key_id{};
    std::span<const std::uint8_t> key{};
    // The externally-supplied injected result for a Suspended frontier
    // (empty for an already-Injected replay; an Injected record with a
    // non-empty span is rejected TransitionInvalid by the controller).
    std::span<const std::uint8_t> injected_result{};
    // The fresh, never-used slot id to publish the Suspended->Injected frame
    // under. Used only when the frontier is Suspended.
    core_wasm_resume::PayloadSlotId chosen_injected_slot{};
    // Optional D2b-4 dedup binding (nullptr = the no-dedup baseline that always
    // returns ReadyForLive; the read/recover/dedup authority lands in D2b-3,
    // which then fails closed at this driver's blocked live frontier).
    rc::GatedResumeOptions gated_options{};
    // Optional D2b-5 live capability invoker. Empty = the ReadyForLive
    // frontier stays fail-closed (ReadyForLiveBlocked): the no-dedup baseline
    // is structurally unchanged. When set, the driver seals the effect intent
    // via gated_options' dedup authority BEFORE the live call (a crash during
    // the call leaves a Pending row for recovery), invokes the capability, and
    // records the OK/ERROR terminal; a PENDING outcome leaves the row sealed.
    // The DedupReplay / DedupReplayFailure / RecoverPending arms stay blocked
    // (D2b-6 / D2b-7).
    LiveCapabilityInvoker live_invoker{};
    // RFC 0026 dual-track elimination: the bridge lane packs P4-D frames at
    // fixed addresses (the entry node's I block and the bridge result blocks).
    // The packers are the engine-agnostic seam: the caller (which has the
    // frame layout section + wire schema from the codegen descriptor) provides
    // the P4-D packing logic; this driver stays free of wasm_host dependencies.
    EntryFramePacker pack_entry_frame{};
    BridgeResultPacker pack_bridge_result{};
    BridgeArgHasher bridge_arg_hasher{};
};

// A completed replay: the run2 raw status word and the verbatim forwarded
// output frame bytes (copied out of guest memory before the instance is torn
// down), plus the final Consumed tombstone generation and the Available
// generation it consumed.
struct ResumeCompleted {
    std::uint32_t raw_status{0};
    std::vector<std::uint8_t> output{};
    std::uint64_t consumed_generation{0}; // the ResolvedConsumed generation M
    std::uint64_t consumed_from{0};       // the Available generation N it consumed
};

// The closed set of fail-closed reasons. Prepare/step arms carry the
// controller's typed reason; store arms the verbatim PayloadStoreError; the
// integrity split states which admission phase observed it; the D2a-owned
// arms name the actions this layer performs.
struct StoreFailure {
    ps::PayloadStoreError error{ps::PayloadStoreError::Malformed};
    host_codes::AdmissionPhase phase{host_codes::AdmissionPhase::Record};
};
struct PrepareFailure {
    rc::ResumePrepareReason reason{rc::ResumePrepareReason::TransitionInvalid};
};
struct StepFailure {
    rc::ResumeStepReason reason{rc::ResumeStepReason::TransitionInvalid};
};
struct DedupBackendFailure {
    durable_effect_authority::DurableEffectBackendError error{};
};
enum class ResumeHostReason : std::uint8_t {
    // F3 declared memory != F1 fixed single page; the request's module bytes
    // do not hash to the verified handle's module_sha256(); or the engine
    // returned a post-run2 page other than the fixed 64 KiB.
    ArtifactMemoryContractMismatch,
    ReadyForLiveBlocked, // no live_invoker (or no dedup authority to seal under):
                         // the live frontier stays fail-closed
    EngineFailure,      // the engine port returned EngineError
    // D2b-5 live capability outcomes. The F2 catalogue is LOCKED with no
    // resume.capability.* code, so the four capability-fault arms map to the
    // non-OK import status code (resume.module.error) and the pending-recovery
    // arm to the ineligible-transition code (resume.transition.invalid).
    LiveCapabilityFault,           // the invoker returned a LiveCapabilityFault
    LiveCapabilityPending,         // the capability returned PENDING; row sealed
    LiveCapabilityError,           // the capability returned ERROR; failure recorded
    LiveCapabilityReplayedFailure, // (D2b-6) begin_effect raced to a prior FAILED terminal
    RecoverPendingRequired,        // (D2b-7) a Pending row needs host reconciliation
};
struct HostFailure {
    ResumeHostReason reason{ResumeHostReason::EngineFailure};
    // Set only for EngineFailure: the engine-port arm (kept in the variant so
    // the failure is inspectable, not collapsed to a bool).
    engine::EngineError engine_error{engine::EngineError::InstanceUnavailable};
};

using ResumeFailure = std::variant<StoreFailure,
                                   PrepareFailure,
                                   StepFailure,
                                   DedupBackendFailure,
                                   HostFailure>;

// Map ANY failure arm to its locked F2 host code. This is the single place
// production turns a typed replay failure into the stable resume.* string.
[[nodiscard]] std::string_view host_code(const ResumeFailure &failure) noexcept;

// Run the full host transaction over `request`'s engine + store. On success
// the live generation is a ResolvedConsumed tombstone and the output frame is
// returned; on failure the generation is left Available / unconsumed.
[[nodiscard]] std::expected<ResumeCompleted, ResumeFailure>
run_resume(const ResumeRequest &request);

} // namespace ahfl::runtime::core_wasm_resume_host
