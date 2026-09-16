// RFC 0026 KR6.5 E4-B2-D2a (F2): the stable `resume.*` host-code catalogue
// mapper. See core_wasm_resume_host_codes.hpp for the totality contract and
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md lines 1244-1279 for the
// locked, range-less, no-echo code table transcribed below.

#include "runtime/engine/core_wasm_resume_host_codes.hpp"

#include <string_view>
#include <utility>

namespace ahfl::runtime::core_wasm_resume_host_codes {

namespace {

// ---- store / auth codes (the 16 PayloadStoreError arms) --------------------
constexpr std::string_view kStoreNotFound{"resume.store.not_found"};
constexpr std::string_view kStoreConsumed{"resume.store.consumed"};
constexpr std::string_view kStoreGenerationMismatch{"resume.store.generation_mismatch"};
constexpr std::string_view kStoreStateMismatch{"resume.store.state_mismatch"};
constexpr std::string_view kStoreSizeCapExceeded{"resume.store.size_cap_exceeded"};
constexpr std::string_view kStoreArtifactMalformed{"resume.store.artifact_malformed"};
constexpr std::string_view kSlotsSetMismatch{"resume.slots.set_mismatch"};
constexpr std::string_view kStoreCrossCheckpointRejected{"resume.store.cross_checkpoint_rejected"};
constexpr std::string_view kStoreCommitFailed{"resume.store.commit_failed"};
constexpr std::string_view kStoreUnsupportedPlatform{"resume.store.unsupported_platform"};
constexpr std::string_view kStoreUnsupportedFilesystem{"resume.store.unsupported_filesystem"};
constexpr std::string_view kAuthKeyIdMismatch{"resume.auth.key_id_mismatch"};
// IntegrityFailed is phase-sensitive (seam lines 1250-1253): the phase-1
// control-record HMAC and the phase-2 payload-slot HMAC are two distinct
// staged-admission steps, never one "top of admission" code.
constexpr std::string_view kAuthIntegrityFailedRecord{"resume.auth.integrity_failed.record"};
constexpr std::string_view kAuthIntegrityFailedSlot{"resume.auth.integrity_failed.slot"};

// ---- host controller / gate codes (the prepare + step reason arms) ---------
constexpr std::string_view kDigestModuleMismatch{"resume.digest.module_mismatch"};
constexpr std::string_view kDigestWireSchemaMismatch{"resume.digest.wire_schema_mismatch"};
constexpr std::string_view kDigestExecManifestMismatch{"resume.digest.exec_manifest_mismatch"};
constexpr std::string_view kCoordinateMismatch{"resume.coordinate.mismatch"};
constexpr std::string_view kEventMalformed{"resume.event.malformed"};
constexpr std::string_view kPayloadSchemaInvalid{"resume.payload.schema_invalid"};
constexpr std::string_view kTransitionInvalid{"resume.transition.invalid"};
constexpr std::string_view kPreflightUnbounded{"resume.preflight.unbounded"};
constexpr std::string_view kPreflightResourceExhausted{"resume.preflight.resource_exhausted"};
constexpr std::string_view kModuleError{"resume.module.error"};
constexpr std::string_view kModuleTrap{"resume.module.trap"};

} // namespace

std::string_view host_code_for(payload_store::PayloadStoreError error,
                               AdmissionPhase integrity_phase) noexcept {
    switch (error) {
    case payload_store::PayloadStoreError::NotFound:
        return kStoreNotFound;
    case payload_store::PayloadStoreError::Consumed:
        return kStoreConsumed;
    case payload_store::PayloadStoreError::IntegrityFailed:
        switch (integrity_phase) {
        case AdmissionPhase::Record:
            return kAuthIntegrityFailedRecord;
        case AdmissionPhase::Slot:
            return kAuthIntegrityFailedSlot;
        }
        std::unreachable();
    case payload_store::PayloadStoreError::KeyIdMismatch:
        return kAuthKeyIdMismatch;
    case payload_store::PayloadStoreError::GenerationMismatch:
        // Also covers CAS / manifest-state disagreement, not pure auth.
        return kStoreGenerationMismatch;
    case payload_store::PayloadStoreError::StateMismatch:
        return kStoreStateMismatch;
    case payload_store::PayloadStoreError::SizeCapExceeded:
        return kStoreSizeCapExceeded;
    case payload_store::PayloadStoreError::Truncated:
    case payload_store::PayloadStoreError::TrailingBytes:
    case payload_store::PayloadStoreError::Malformed:
        // A structural failure may originate in the pointer, manifest, slot, or
        // record; it is never labelled record-only.
        return kStoreArtifactMalformed;
    case payload_store::PayloadStoreError::SlotSetMismatch:
        return kSlotsSetMismatch;
    case payload_store::PayloadStoreError::CrossCheckpointRejected:
        return kStoreCrossCheckpointRejected;
    case payload_store::PayloadStoreError::WriteFailed:
    case payload_store::PayloadStoreError::CommitInterrupted:
        return kStoreCommitFailed;
    case payload_store::PayloadStoreError::UnsupportedPlatform:
        return kStoreUnsupportedPlatform;
    case payload_store::PayloadStoreError::UnsupportedFilesystem:
        return kStoreUnsupportedFilesystem;
    }
    // Exhaustive: -Wswitch (no `default`) makes a missing 17th arm a compile
    // error; this marks the post-switch path unreachable for -Wreturn-type.
    std::unreachable();
}

std::string_view host_code_for(core_wasm_resume_controller::ResumePrepareReason reason) noexcept {
    using core_wasm_resume_controller::ResumePrepareReason;
    switch (reason) {
    case ResumePrepareReason::ModuleDigestMismatch:
        return kDigestModuleMismatch;
    case ResumePrepareReason::WireSchemaDigestMismatch:
        return kDigestWireSchemaMismatch;
    case ResumePrepareReason::ExecManifestDigestMismatch:
        return kDigestExecManifestMismatch;
    case ResumePrepareReason::CoordinateMismatch:
        return kCoordinateMismatch;
    case ResumePrepareReason::PayloadSchemaInvalid:
        return kPayloadSchemaInvalid;
    case ResumePrepareReason::TransitionInvalid:
        return kTransitionInvalid;
    case ResumePrepareReason::Unbounded:
        return kPreflightUnbounded;
    case ResumePrepareReason::ResourceExhausted:
        // Distinct from the compile-time wasm.RESOURCE_EXHAUSTED owner.
        return kPreflightResourceExhausted;
    }
    std::unreachable();
}

std::string_view host_code_for(core_wasm_resume_controller::ResumeStepReason reason) noexcept {
    using core_wasm_resume_controller::ResumeStepReason;
    switch (reason) {
    case ResumeStepReason::CoordinateMismatch:
        return kCoordinateMismatch;
    case ResumeStepReason::EventMalformed:
        return kEventMalformed;
    case ResumeStepReason::PayloadSchemaInvalid:
        return kPayloadSchemaInvalid;
    case ResumeStepReason::TransitionInvalid:
        return kTransitionInvalid;
    case ResumeStepReason::ModuleError:
        // A run2 ERROR, including fail-closed normalization of an unknown /
        // import ERROR.
        return kModuleError;
    case ResumeStepReason::ModuleTrap:
        return kModuleTrap;
    }
    std::unreachable();
}

} // namespace ahfl::runtime::core_wasm_resume_host_codes
