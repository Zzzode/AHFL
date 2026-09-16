#pragma once

// RFC 0026 KR6.5 E4-B2-D2a (F2): the stable `resume.*` host-code catalogue
// mapper -- a typed reason -> host `std::string_view` total mapping.
//
// The catalogue is a COMPLETE, LOCKED table in
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md section 5 ("Error SSOT
// and priority", lines 1244-1279). The landed D1b replay controller emits NO
// `resume.*` string: it returns a typed `ResumePrepareReason` /
// `ResumeStepReason` or carries a verbatim `payload_store::PayloadStoreError`.
// This authority is the host-INDEPENDENT half of the future D2a production host
// boundary: a PURE decision-only mapper from those closed enums to the stable,
// range-less, no-echo host code the production host reports. There is no VM, no
// store, no diagnostic engine, and no allocation: the returned view names
// static storage valid for the whole program lifetime. Diagnostic strings are
// the Principle-2-allowed use of strings (user-facing error identity); the
// canonical identity on the way IN stays a numeric enum.
//
// TOTALITY IS COMPILE-ENFORCED. Every mapper is an exhaustive switch over a
// closed enum with no `default`, so under -Wall -Wextra -Werror (-Wswitch) a
// newly added enumerator with no catalogue entry is a hard build failure.
//
// `PayloadStoreError::IntegrityFailed` is the one phase-sensitive arm. The
// seam mandates TWO distinct codes rather than a single "top of admission"
// failure: the phase-1 control-record HMAC (record integrity) and the phase-2
// payload-slot HMAC (slot integrity) are separate staged-admission steps. The
// caller MUST state which phase observed the mismatch.

#include <cstdint>
#include <string_view>

#include "runtime/engine/core_wasm_resume_controller.hpp" // ResumePrepareReason, ResumeStepReason
#include "runtime/engine/payload_store_codec.hpp"         // payload_store::PayloadStoreError

namespace ahfl::runtime::core_wasm_resume_host_codes {

// The staged-admission step that observed a failure. D1a's two-phase
// `ResumeSnapshot` authenticates the control record / pointer / manifest in
// phase 1 (record integrity) and the admitted exact payload-slot set in
// phase 2 (slot integrity). Only `PayloadStoreError::IntegrityFailed` is
// phase-sensitive; every other store arm ignores the phase.
enum class AdmissionPhase : std::uint8_t {
    Record, // phase 1: control-record / pointer / manifest integrity
    Slot,   // phase 2: admitted payload-slot integrity
};

// Map one of the 16 `PayloadStoreError` variants to its locked host code.
// `integrity_phase` selects the record (phase 1) vs slot (phase 2) integrity
// code for `IntegrityFailed`; it is a required argument so the phase
// distinction can never be silently defaulted.
[[nodiscard]] std::string_view host_code_for(payload_store::PayloadStoreError error,
                                             AdmissionPhase integrity_phase) noexcept;

// Map one of the 8 prepare-phase reasons to its locked host code.
[[nodiscard]] std::string_view
host_code_for(core_wasm_resume_controller::ResumePrepareReason reason) noexcept;

// Map one of the 6 step-phase reasons to its locked host code.
[[nodiscard]] std::string_view
host_code_for(core_wasm_resume_controller::ResumeStepReason reason) noexcept;

} // namespace ahfl::runtime::core_wasm_resume_host_codes
