// RFC 0026 KR6.5 E4-B2-D2a (F2) KAT for the stable `resume.*` host-code
// catalogue mapper. Hand-rolled check()/main(). This is a table-driven
// known-answer test that pins the COMPLETE, LOCKED table in
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md lines 1244-1279:
// every one of the 16 PayloadStoreError arms (including the phase-1 record /
// phase-2 slot split of IntegrityFailed), every one of the 8
// ResumePrepareReason arms, and every one of the 6 ResumeStepReason arms maps
// to its exact locked host string. Totality is additionally compile-proven by
// the default-less exhaustive -Wswitch in the mapper; this file locks the
// exact STRINGS a future D2a host reports.

#include "runtime/engine/core_wasm_resume_host_codes.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ahfl::runtime::core_wasm_resume_controller::ResumePrepareReason;
using ahfl::runtime::core_wasm_resume_controller::ResumeStepReason;
using ahfl::runtime::core_wasm_resume_host_codes::AdmissionPhase;
using ahfl::runtime::core_wasm_resume_host_codes::host_code_for;
using ahfl::runtime::payload_store::PayloadStoreError;

int g_failures = 0;
int g_checks = 0;

void check(bool ok, std::string_view name) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

void check_code(std::string_view actual, std::string_view expected, std::string_view name) {
    check(actual == expected, name);
}

struct StoreCase {
    PayloadStoreError error;
    AdmissionPhase phase;
    std::string_view expected;
    std::string_view name;
};

struct PrepareCase {
    ResumePrepareReason reason;
    std::string_view expected;
    std::string_view name;
};

struct StepCase {
    ResumeStepReason reason;
    std::string_view expected;
    std::string_view name;
};

} // namespace

int main() {
    // ---- 16 PayloadStoreError arms ----------------------------------------
    // IntegrityFailed appears twice (record phase 1, slot phase 2); every
    // other arm appears once. The phase argument is passed for ALL rows so a
    // non-integrity arm is proven phase-INDEPENDENT.
    constexpr StoreCase kStoreCases[] = {
        {PayloadStoreError::NotFound,
         AdmissionPhase::Record,
         "resume.store.not_found",
         "NotFound/record"},
        {PayloadStoreError::NotFound,
         AdmissionPhase::Slot,
         "resume.store.not_found",
         "NotFound/slot"},
        {PayloadStoreError::Consumed,
         AdmissionPhase::Record,
         "resume.store.consumed",
         "Consumed/record"},
        {PayloadStoreError::Consumed,
         AdmissionPhase::Slot,
         "resume.store.consumed",
         "Consumed/slot"},
        {PayloadStoreError::IntegrityFailed,
         AdmissionPhase::Record,
         "resume.auth.integrity_failed.record",
         "IntegrityFailed/record (phase 1)"},
        {PayloadStoreError::IntegrityFailed,
         AdmissionPhase::Slot,
         "resume.auth.integrity_failed.slot",
         "IntegrityFailed/slot (phase 2)"},
        {PayloadStoreError::KeyIdMismatch,
         AdmissionPhase::Record,
         "resume.auth.key_id_mismatch",
         "KeyIdMismatch/record"},
        {PayloadStoreError::KeyIdMismatch,
         AdmissionPhase::Slot,
         "resume.auth.key_id_mismatch",
         "KeyIdMismatch/slot"},
        {PayloadStoreError::GenerationMismatch,
         AdmissionPhase::Record,
         "resume.store.generation_mismatch",
         "GenerationMismatch/record"},
        {PayloadStoreError::GenerationMismatch,
         AdmissionPhase::Slot,
         "resume.store.generation_mismatch",
         "GenerationMismatch/slot"},
        {PayloadStoreError::StateMismatch,
         AdmissionPhase::Record,
         "resume.store.state_mismatch",
         "StateMismatch"},
        {PayloadStoreError::SizeCapExceeded,
         AdmissionPhase::Record,
         "resume.store.size_cap_exceeded",
         "SizeCapExceeded"},
        {PayloadStoreError::Truncated,
         AdmissionPhase::Record,
         "resume.store.artifact_malformed",
         "Truncated"},
        {PayloadStoreError::TrailingBytes,
         AdmissionPhase::Record,
         "resume.store.artifact_malformed",
         "TrailingBytes"},
        {PayloadStoreError::Malformed,
         AdmissionPhase::Slot,
         "resume.store.artifact_malformed",
         "Malformed"},
        {PayloadStoreError::SlotSetMismatch,
         AdmissionPhase::Slot,
         "resume.slots.set_mismatch",
         "SlotSetMismatch"},
        {PayloadStoreError::CrossCheckpointRejected,
         AdmissionPhase::Record,
         "resume.store.cross_checkpoint_rejected",
         "CrossCheckpointRejected"},
        {PayloadStoreError::WriteFailed,
         AdmissionPhase::Slot,
         "resume.store.commit_failed",
         "WriteFailed"},
        {PayloadStoreError::CommitInterrupted,
         AdmissionPhase::Slot,
         "resume.store.commit_failed",
         "CommitInterrupted"},
        {PayloadStoreError::UnsupportedPlatform,
         AdmissionPhase::Record,
         "resume.store.unsupported_platform",
         "UnsupportedPlatform"},
        {PayloadStoreError::UnsupportedFilesystem,
         AdmissionPhase::Slot,
         "resume.store.unsupported_filesystem",
         "UnsupportedFilesystem"},
    };

    for (const StoreCase &c : kStoreCases) {
        check_code(
            host_code_for(c.error, c.phase), c.expected, std::string{"store:"}.append(c.name));
    }

    // The two integrity phase codes are DISTINCT and both carry the locked
    // auth prefix; they are never collapsed to one code.
    check(host_code_for(PayloadStoreError::IntegrityFailed, AdmissionPhase::Record) !=
              host_code_for(PayloadStoreError::IntegrityFailed, AdmissionPhase::Slot),
          "integrity phase codes are distinct");

    // ---- 8 ResumePrepareReason arms ---------------------------------------
    constexpr PrepareCase kPrepareCases[] = {
        {ResumePrepareReason::ModuleDigestMismatch,
         "resume.digest.module_mismatch",
         "ModuleDigestMismatch"},
        {ResumePrepareReason::WireSchemaDigestMismatch,
         "resume.digest.wire_schema_mismatch",
         "WireSchemaDigestMismatch"},
        {ResumePrepareReason::ExecManifestDigestMismatch,
         "resume.digest.exec_manifest_mismatch",
         "ExecManifestDigestMismatch"},
        {ResumePrepareReason::CoordinateMismatch,
         "resume.coordinate.mismatch",
         "CoordinateMismatch"},
        {ResumePrepareReason::PayloadSchemaInvalid,
         "resume.payload.schema_invalid",
         "PayloadSchemaInvalid"},
        {ResumePrepareReason::TransitionInvalid, "resume.transition.invalid", "TransitionInvalid"},
        {ResumePrepareReason::Unbounded, "resume.preflight.unbounded", "Unbounded"},
        {ResumePrepareReason::ResourceExhausted,
         "resume.preflight.resource_exhausted",
         "ResourceExhausted"},
    };
    for (const PrepareCase &c : kPrepareCases) {
        check_code(host_code_for(c.reason), c.expected, std::string{"prepare:"}.append(c.name));
    }

    // ---- 6 ResumeStepReason arms ------------------------------------------
    constexpr StepCase kStepCases[] = {
        {ResumeStepReason::CoordinateMismatch, "resume.coordinate.mismatch", "CoordinateMismatch"},
        {ResumeStepReason::EventMalformed, "resume.event.malformed", "EventMalformed"},
        {ResumeStepReason::PayloadSchemaInvalid,
         "resume.payload.schema_invalid",
         "PayloadSchemaInvalid"},
        {ResumeStepReason::TransitionInvalid, "resume.transition.invalid", "TransitionInvalid"},
        {ResumeStepReason::ModuleError, "resume.module.error", "ModuleError"},
        {ResumeStepReason::ModuleTrap, "resume.module.trap", "ModuleTrap"},
    };
    for (const StepCase &c : kStepCases) {
        check_code(host_code_for(c.reason), c.expected, std::string{"step:"}.append(c.name));
    }

    // ---- cross-catalogue invariants ---------------------------------------
    // Coordinate / schema / transition are shared by prepare and step and
    // MUST report the identical stable code from both enums.
    check(host_code_for(ResumePrepareReason::CoordinateMismatch) ==
              host_code_for(ResumeStepReason::CoordinateMismatch),
          "coordinate code is shared by prepare and step");
    check(host_code_for(ResumePrepareReason::PayloadSchemaInvalid) ==
              host_code_for(ResumeStepReason::PayloadSchemaInvalid),
          "payload-schema code is shared by prepare and step");
    check(host_code_for(ResumePrepareReason::TransitionInvalid) ==
              host_code_for(ResumeStepReason::TransitionInvalid),
          "transition code is shared by prepare and step");

    // The host preflight code is NEVER the compile-time wasm status token.
    check(host_code_for(ResumePrepareReason::ResourceExhausted) != "wasm.RESOURCE_EXHAUSTED",
          "host resource_exhausted never reuses wasm.RESOURCE_EXHAUSTED");

    // Every code is range-less: it carries no ':' marker or trailing data.
    for (const StoreCase &c : kStoreCases) {
        check(c.expected.find(':') == std::string_view::npos, "store code is range-less");
    }

    if (g_failures == 0) {
        std::cout << "all " << g_checks << " resume host-code catalogue KAT checks passed\n";
        return 0;
    }
    std::cerr << g_failures << " of " << g_checks
              << " resume host-code catalogue check(s) failed\n";
    return 1;
}
