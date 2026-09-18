// RFC 0026 KR6.5 E4-B2-D2a (F4): see the matching header for the contract.

#include "runtime/engine/core_wasm_resume_host.hpp"

#include <optional>
#include <utility>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "base/support/sha256.hpp"
#include "runtime/engine/core_wasm_resume_capacity.hpp"
#include "runtime/engine/core_wasm_resume_controller.hpp"

namespace ahfl::runtime::core_wasm_resume_host {

namespace {

namespace ir_core = ahfl::ir::core;

using engine::CoreWasmResumeEngine;
using engine::EngineError;
using engine::GuestPointer;
using engine::ImportAbort;
using engine::ImportCallbackResult;
using engine::ImportObservation;
using engine::ImportReply;
using engine::Run2HostAborted;
using engine::Run2ResultTuple;
using engine::Run2Trapped;
using host_codes::AdmissionPhase;
using host_codes::host_code_for;
using ps::PayloadStoreError;
using rc::ImportStepDecision;
using rc::NeedInjectedSlot;
using rc::PreparedResume;
using rc::ReturnMemo;

// Fail-closed shorthand constructors.
[[nodiscard]] ResumeFailure store_failure(PayloadStoreError error, AdmissionPhase phase) {
    return ResumeFailure{StoreFailure{error, phase}};
}
[[nodiscard]] ResumeFailure prepare_failure(rc::ResumePrepareReason reason) {
    return ResumeFailure{PrepareFailure{reason}};
}
[[nodiscard]] ResumeFailure step_failure(rc::ResumeStepReason reason) {
    return ResumeFailure{StepFailure{reason}};
}
[[nodiscard]] ResumeFailure host_failure(ResumeHostReason reason,
                                         EngineError engine_error = EngineError::InvalidSequence) {
    return ResumeFailure{HostFailure{reason, engine_error}};
}

// Map a controller prepare error. Store errors surfaced during open_snapshot
// are phase-1 (record) integrity; store errors surfaced during
// admit_and_preflight are phase-2 (slot) integrity.
[[nodiscard]] ResumeFailure map_prepare_error(const rc::ResumePrepareError &error,
                                              AdmissionPhase integrity_phase) {
    return std::visit(
        ahfl::Overloaded{
            [](rc::ResumePrepareReason reason) -> ResumeFailure {
                return prepare_failure(reason);
            },
            [integrity_phase](PayloadStoreError store_error) -> ResumeFailure {
                return store_failure(store_error, integrity_phase);
            },
        },
        error);
}

// Map a controller step error. The D2b durable-effect backend has NO locked
// host code by design (the D2b-4 landing deliberately adds none to the F2
// catalogue): its verbatim storage fault is retained in the typed
// DedupBackendFailure arm for the D2b reconciliation owner, and this
// replay-only adapter reports the blocked live-frontier transition through
// host_code().
[[nodiscard]] ResumeFailure map_step_error(const rc::ResumeStepError &error) {
    return std::visit(
        ahfl::Overloaded{
            [](rc::ResumeStepReason reason) -> ResumeFailure { return step_failure(reason); },
            [](PayloadStoreError store_error) -> ResumeFailure {
                // A step-phase store error is a publish / consume transaction
                // fault; the record-side integrity code applies there.
                return store_failure(store_error, AdmissionPhase::Record);
            },
            [](durable_effect_authority::DurableEffectBackendError backend_error)
                -> ResumeFailure { return ResumeFailure{DedupBackendFailure{backend_error}}; },
        },
        error);
}

// F3 -> F1 gate: the admitted artifact's OWN declared Memory section must be
// exactly the fixed single 64 KiB page before any instance exists.
[[nodiscard]] bool
artifact_declares_fixed_single_page(const csm::VerifiedCoreWasmSchemaModule &module) {
    const auto declared = module.declared_linear_memory_capacity();
    if (!declared.has_value()) {
        return false;
    }
    return declared->capacity_bytes == ir_core::kCoreWasmFixedLinearMemoryCapacityBytes &&
           declared->min_pages == ir_core::kCoreWasmFixedLinearMemoryMinPages &&
           !declared->has_max;
}

// Per-replay mutable state shared between run_resume and the synchronous
// import callback the engine invokes nested inside invoke_run2.
struct ReplayState {
    const ResumeRequest *request{nullptr};
    PreparedResume *prepared{nullptr};
    // The precise fail-closed reason a callback decision observed; surfaced by
    // run_resume when the engine unwinds run2 with Run2HostAborted.
    std::optional<ResumeFailure> abort_failure{};
};

// Write one decision's exact bytes as an instance-lifetime L3/L4 bump and hand
// the engine its guest (ptr,len).
[[nodiscard]] std::expected<ImportReply, EngineError>
write_frame(CoreWasmResumeEngine &engine, ReplayState &state,
            std::span<const std::uint8_t> bytes) {
    auto ptr = engine.alloc_then_write(bytes);
    if (!ptr.has_value()) {
        state.abort_failure = host_failure(ResumeHostReason::EngineFailure, ptr.error());
        return std::unexpected(ptr.error());
    }
    return ImportReply{*ptr, static_cast<std::uint32_t>(bytes.size())};
}

// Fail closed with a precise ResumeFailure and an engine-side abort.
[[nodiscard]] ImportCallbackResult abort_with(ReplayState &state, ResumeFailure failure) {
    state.abort_failure = std::move(failure);
    return ImportCallbackResult{ImportAbort{}};
}

// The synchronous import decision -> frame-transfer action (seam 5.3 L3/L4).
[[nodiscard]] ImportCallbackResult serve_import(ReplayState &state,
                                                const ImportObservation &observation) {
    CoreWasmResumeEngine &engine = *state.request->engine;

    rc::ImportStepInput input;
    input.observed_ordinal = csm::CapabilityImportOrdinal{observation.import_ordinal};
    input.module_param_frame = observation.param_frame;
    input.whole_linear_memory = observation.whole_memory;

    auto decision = rc::next_import(*state.prepared, input);
    if (!decision.has_value()) {
        return abort_with(state, map_step_error(decision.error()));
    }

    using Result = ImportCallbackResult;
    return std::visit(
        ahfl::Overloaded{
            // L3: an OLD committed memo (below-frontier) OR an Injected
            // frontier's already-committed memo. Exact authenticated bytes, NO
            // CAS and NO live call.
            [&](const ReturnMemo &memo) -> Result {
                auto reply = write_frame(engine, state, memo.memo_bytes);
                if (!reply.has_value()) {
                    return ImportCallbackResult{ImportAbort{}};
                }
                return ImportCallbackResult{*reply};
            },

            // L4: the Suspended frontier. bind -> CAS publish -> explicit ACK;
            // only AFTER the ACK succeeds is the new frame written/transferred.
            [&](const NeedInjectedSlot &) -> Result {
                auto plan = rc::bind_publish_injected(*state.prepared,
                                                      state.request->chosen_injected_slot);
                if (!plan.has_value()) {
                    return abort_with(state, map_step_error(plan.error()));
                }
                const std::span<const std::uint8_t, 16> key_id_16{
                    state.request->key_id.data(), 16};
                auto published = state.request->store->publish_available(
                    state.request->workflow, state.request->checkpoint,
                    plan->expected_generation(), plan->record(), plan->slots(),
                    key_id_16, state.request->key);
                auto injected = rc::ack_publish_injected(*state.prepared, published);
                if (!injected.has_value()) {
                    // CAS failure: nothing was transferred; the generation stays
                    // Available / unconsumed.
                    return abort_with(state, map_step_error(injected.error()));
                }
                auto reply = write_frame(engine, state, *injected);
                if (!reply.has_value()) {
                    // The publish is already ACKed: the newest generation stays
                    // Available and is replayable; only the transfer failed.
                    return ImportCallbackResult{ImportAbort{}};
                }
                return ImportCallbackResult{*reply};
            },

            // After-frontier verdicts. The live OK/PENDING/ERROR response API
            // and the D2b replay feed-back are the blocked D2b follow-on: this
            // adapter never invokes a live capability and cannot consume here.
            // Fail closed with no fabricated frame; the generation stays
            // Available / unconsumed.
            [&](const rc::ReadyForLive &) -> Result {
                return abort_with(state, host_failure(ResumeHostReason::ReadyForLiveBlocked));
            },
            [&](const rc::DedupReplay &) -> Result {
                return abort_with(state, host_failure(ResumeHostReason::ReadyForLiveBlocked));
            },
            [&](const rc::DedupReplayFailure &) -> Result {
                return abort_with(state, host_failure(ResumeHostReason::ReadyForLiveBlocked));
            },
            [&](const rc::RecoverPending &) -> Result {
                return abort_with(state, host_failure(ResumeHostReason::ReadyForLiveBlocked));
            },
        },
        *decision);
}

} // namespace

std::string_view host_code(const ResumeFailure &failure) noexcept {
    return std::visit(
        ahfl::Overloaded{
            [](const StoreFailure &f) -> std::string_view {
                return host_code_for(f.error, f.phase);
            },
            [](const PrepareFailure &f) -> std::string_view {
                return host_code_for(f.reason);
            },
            [](const StepFailure &f) -> std::string_view {
                return host_code_for(f.reason);
            },
            // The F2 catalogue intentionally defines no dedup-backend code; a
            // replay-only host cannot act past the live frontier, so the
            // blocked ineligible-transition code is reported while the typed
            // backend error stays verbatim in the arm.
            [](const DedupBackendFailure &) -> std::string_view {
                return host_code_for(rc::ResumeStepReason::TransitionInvalid);
            },
            [](const HostFailure &f) -> std::string_view {
                switch (f.reason) {
                // The seam assigns the caller-capacity / u32 transfer-domain
                // gates to resume.preflight.resource_exhausted; the F3-vs-F1
                // fixed-page mismatch is that gate before instantiation.
                case ResumeHostReason::ArtifactMemoryContractMismatch:
                    return host_code_for(rc::ResumePrepareReason::ResourceExhausted);
                // A live frontier is ineligible to move to consumed under a
                // replay-only adapter.
                case ResumeHostReason::ReadyForLiveBlocked:
                    return host_code_for(rc::ResumeStepReason::TransitionInvalid);
                case ResumeHostReason::EngineFailure:
                    switch (f.engine_error) {
                    case EngineError::MemoryCapacityExceeded:
                        return host_code_for(rc::ResumePrepareReason::ResourceExhausted);
                    case EngineError::InvalidSequence:
                        return host_code_for(rc::ResumeStepReason::TransitionInvalid);
                    case EngineError::InstanceUnavailable:
                        return host_code_for(rc::ResumeStepReason::ModuleError);
                    }
                }
                std::unreachable();
            },
        },
        failure);
}

std::expected<ResumeCompleted, ResumeFailure> run_resume(const ResumeRequest &request) {
    if (request.module == nullptr || request.module_bytes.empty() ||
        request.key_id.size() != 16 || request.key.empty()) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            EngineError::InvalidSequence));
    }
    const csm::VerifiedCoreWasmSchemaModule &module = *request.module;

    // (1) F3 -> F1 fixed-page contract gate, before any instance or store.
    if (!artifact_declares_fixed_single_page(module)) {
        return std::unexpected(
            host_failure(ResumeHostReason::ArtifactMemoryContractMismatch));
    }

    // Bind the bytes the engine will instantiate to the VERIFIED handle: hash
    // the request's module bytes and compare them (fixed work) against the
    // digest the A2 module was admitted over. Every other gate (F3, the
    // phase-1 record digests, the coordinate and slot admissions) inspects the
    // handle or the stored record; without THIS compare nothing proves the
    // byte stream handed across the engine port is the digest-admitted
    // artifact rather than a swapped / re-read buffer. Failing closed here
    // turns the ResumeRequest "never re-read" precondition into an enforced
    // gate and future-proofs ports that read the artifact from a file path.
    if (!support::fixed_work_equal(support::sha256(request.module_bytes),
                                   module.module_sha256())) {
        return std::unexpected(
            host_failure(ResumeHostReason::ArtifactMemoryContractMismatch));
    }

    if (request.engine == nullptr || request.store == nullptr) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            EngineError::InvalidSequence));
    }
    CoreWasmResumeEngine &engine = *request.engine;
    // Widen the validated dynamic span into the store API's fixed-extent key id.
    const std::span<const std::uint8_t, 16> key_id_16{request.key_id.data(), 16};

    // (2) Phase-1 gates: pinned snapshot then digest + coordinate gates.
    auto snapshot = request.store->open_snapshot(request.workflow, request.checkpoint,
                                                 key_id_16, request.key);
    if (!snapshot.has_value()) {
        return std::unexpected(store_failure(snapshot.error(), AdmissionPhase::Record));
    }
    auto gated = rc::open_gated_resume(module, std::move(*snapshot), request.gated_options);
    if (!gated.has_value()) {
        return std::unexpected(map_prepare_error(gated.error(), AdmissionPhase::Record));
    }

    // (3) Phase-2: admit the exact slot set + eligibility + the two-pass TOTAL
    // preflight against the F1 fixed single-page capacity.
    const auto capacity = core_wasm_resume_capacity::fixed_single_page_capacity();
    auto outcome =
        rc::admit_and_preflight(std::move(*gated), key_id_16, request.key,
                                request.injected_result, capacity);
    if (!outcome.has_value()) {
        return std::unexpected(map_prepare_error(outcome.error(), AdmissionPhase::Slot));
    }

    // This one-shot host always carries the Suspended frontier's injected
    // bytes in the request: admit_and_preflight consumes Suspended+nonempty
    // directly. A PendingInjection outcome means the caller omitted the frame;
    // the continuation surface is a separate entry, not a half-driven replay.
    if (std::holds_alternative<rc::PendingInjection>(*outcome)) {
        return std::unexpected(prepare_failure(rc::ResumePrepareReason::TransitionInvalid));
    }
    PreparedResume prepared = std::move(std::get<rc::PreparedResume>(*outcome));

    // (4) The synchronous per-import decision callback + a FRESH instance.
    ReplayState state;
    state.request = &request;
    state.prepared = &prepared;
    engine::ImportCallback callback =
        [&state](const ImportObservation &observation) -> ImportCallbackResult {
        return serve_import(state, observation);
    };

    auto instantiated = engine.fresh_instance(request.module_bytes, std::move(callback));
    if (!instantiated.has_value()) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            instantiated.error()));
    }

    // L0: write the verbatim opaque entry frame and hand run2 its (ptr,len).
    const auto entry = prepared.entry_frame().bytes();
    auto entry_ptr = engine.alloc_then_write(entry);
    if (!entry_ptr.has_value()) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            entry_ptr.error()));
    }

    // (5) run2 blocks while every ahfl_cap import is served synchronously.
    auto run = engine.invoke_run2(*entry_ptr, static_cast<std::uint32_t>(entry.size()));
    if (!run.has_value()) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure, run.error()));
    }

    // A callback fail-closed decision unwound run2. Report its precise reason.
    if (std::holds_alternative<Run2HostAborted>(*run)) {
        if (state.abort_failure.has_value()) {
            return std::unexpected(*state.abort_failure);
        }
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            EngineError::InvalidSequence));
    }

    // A genuine Wasm trap: the controller classifies it ModuleTrap and moves to
    // Failed; no tombstone is emitted and the generation stays Available /
    // unconsumed. finish_run never yields a plan for a trap, but both arms are
    // handled so a contract change can't dereference a nonexistent error.
    if (std::holds_alternative<Run2Trapped>(*run)) {
        auto classified = rc::finish_run(prepared, rc::Run2Exit{rc::Run2Trapped{}});
        if (!classified.has_value()) {
            return std::unexpected(map_step_error(classified.error()));
        }
        // Defensive: a plan on a trap is itself an ineligible transition.
        return std::unexpected(step_failure(rc::ResumeStepReason::ModuleTrap));
    }

    const Run2ResultTuple tuple = std::get<Run2ResultTuple>(*run);

    // Re-read the WHOLE post-run2 page once: it is both the terminal event
    // prefix authority and the source of the forwarded L5 output span.
    auto whole = engine.read_whole_memory();
    if (!whole.has_value()) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure, whole.error()));
    }

    // The engine-port contract promises the WHOLE fixed 64 KiB page (and the
    // F3 gate proved the module declares exactly one): a short or oversized
    // span from an engine port (e.g. a truncated transport read) violates the
    // fixed-page identity the whole replay math assumes. Fail closed before
    // the terminal event-prefix decode and the L5 output copy.
    if (whole->size() != ir_core::kCoreWasmFixedLinearMemoryCapacityBytes) {
        return std::unexpected(host_failure(ResumeHostReason::ArtifactMemoryContractMismatch));
    }

    // (6) Terminal gate on the raw status + full published prefix.
    rc::Run2Returned returned;
    returned.raw_status = tuple.raw_status;
    returned.whole_linear_memory = *whole;
    auto consumed_plan = rc::finish_run(prepared, rc::Run2Exit{returned});
    if (!consumed_plan.has_value()) {
        return std::unexpected(map_step_error(consumed_plan.error()));
    }

    // Copy the forwarded output out of guest memory before the instance is
    // torn down. An out-of-page tuple pointer is a fail-closed module fault.
    const std::uint64_t out_begin = tuple.output_ptr.value;
    const std::uint64_t out_end = out_begin + tuple.output_len;
    if (out_end > whole->size() || out_begin > out_end) {
        return std::unexpected(host_failure(ResumeHostReason::EngineFailure,
                                            EngineError::InvalidSequence));
    }
    ResumeCompleted completed;
    completed.raw_status = tuple.raw_status;
    completed.output.assign(whole->begin() + static_cast<std::ptrdiff_t>(out_begin),
                            whole->begin() + static_cast<std::ptrdiff_t>(out_end));

    // Tombstone the consumed generation (CAS) and ACK the N+1 == M result.
    const std::uint64_t consumed_from = consumed_plan->expected_generation();
    auto marked = request.store->mark_consumed(request.workflow, request.checkpoint,
                                               consumed_from, key_id_16, request.key);
    auto ack = rc::ack_mark_consumed(prepared, marked);
    if (!ack.has_value()) {
        return std::unexpected(map_step_error(ack.error()));
    }

    completed.consumed_from = consumed_from;
    completed.consumed_generation = *marked;
    return completed;
}

} // namespace ahfl::runtime::core_wasm_resume_host
