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
//        ReadyForLive / dedup arms -> fail closed (the live OK/PENDING/ERROR
//                             response API is the blocked D2b follow-on).
//   6. rc::finish_run(Run2Exit) -> store.mark_consumed ->
//      rc::ack_mark_consumed.
//
// Every failure is fail-CLOSED: no frame is transferred on a failing step, no
// live capability is ever invoked by this driver, and a failed resume leaves
// the authenticated generation Available / unconsumed. Errors surface through
// a CLOSED typed variant; `host_code()` maps each arm to the F2 locked
// resume.* catalogue (range-less, no-echo), the stable string production hosts
// report.
//
// FOUNDATION (honest): verified with a synchronous in-test engine double, not
// a real Wasm VM. The first real-engine port (F5, Node embedded engine) and
// the D2b live-result API remain separate slices; B2 / KR6.5 stay false.

#include <cstdint>
#include <expected>
#include <span>
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
    // Optional D2b-4 dedup binding (nullptr = legacy always-ReadyForLive,
    // which then fails closed at this driver's blocked live frontier).
    rc::GatedResumeOptions gated_options{};
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
    ReadyForLiveBlocked,            // frontier passed; live result API is the D2b follow-on
    EngineFailure,                  // the engine port returned EngineError
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
