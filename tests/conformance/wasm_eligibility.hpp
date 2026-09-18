#pragma once

// KR6.7 (RFC 0026 P7): WASM eligibility classifier with structured skip
// reasons.
//
// A conformance case manifest declares which engines may run it
// (`engines.wasm.eligible` + `reason`). That declaration is a claim about the
// compiler, and a hand-maintained claim drifts: a case may still be skipped as
// "needs the KR6.6 computation lane" long after the KR6.6 lane learned to
// lower it, or (far worse) a case may be advertised as wasm-eligible when the
// real emit path rejects it.
//
// This classifier makes the claim machine-checkable. It runs the SAME pipeline
// the wasm backend runs -- canonical frontend -> `lower_ahfl_to_core` ->
// `compute_core_layouts` -> `resolve_core_wasm_entry` -> `emit_core_wasm` --
// and reduces the outcome to one typed verdict plus the stable diagnostic code
// that produced it. A rejection is DATA here, never a test failure: this is a
// classifier, not a runner.
//
// The verdict is derived from the diagnostic CODES the compiler already emits
// (never a second, parallel re-derivation of the codegen subset predicates --
// CLAUDE.md forbids a second SSOT):
//
//   runnable_orchestration            emits cleanly today on the P5
//                                     orchestration lane (E1-E3 + P6)
//   blocked_computation (KR6.6)       the orchestration lane bailed at one of
//                                     its three fail-closed seams, which is
//                                     exactly the seam the KR6.6 computation
//                                     lane exists to lift
//   blocked_layout (P4-D)             a physical-layout code (`core.layout.*`
//                                     or `wasm.INVALID_LAYOUT`)
//   blocked_unsupported_orchestration outside the wasm contract entirely
//                                     (invalid Core, unresolved entry, ...);
//                                     still carries its stable code
//
// The runner then cross-checks the manifest's declared lane against the
// computed verdict, so an overclaiming (or stale) manifest fails closed even
// when no WASM engine is installed.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "conformance/conformance_case.hpp"

namespace ahfl::conformance {

/// Why a conformance case can or cannot run on the wasm orchestration lane.
enum class WasmEligibilityVerdict {
    RunnableOrchestration,
    BlockedComputation,
    BlockedLayout,
    BlockedUnsupportedOrchestration,
};

/// The wire/CLI spelling of one verdict (used in diagnostics and pinned by
/// tests). Never a canonical identity -- the enum is.
[[nodiscard]] std::string_view
wasm_eligibility_verdict_name(WasmEligibilityVerdict verdict) noexcept;

/// One case's computed eligibility.
struct WasmEligibilityClassification {
    WasmEligibilityVerdict verdict{WasmEligibilityVerdict::BlockedUnsupportedOrchestration};
    /// Stable diagnostic code that produced the verdict. EMPTY iff the verdict
    /// is `RunnableOrchestration` (invariant pinned by the unit test).
    std::string code;
    /// Human-readable, always non-empty: the lane, or the exact blocking
    /// construct (`<code>: <compiler message>`).
    std::string reason;
    /// Size of the emitted module. Non-zero iff `RunnableOrchestration`; the
    /// classifier really did run the emit path rather than infer the verdict.
    std::size_t artifact_bytes{0};
};

namespace wasm_eligibility_diag {
/// The case source is not a valid AHFL program at all (parse / resolve /
/// typecheck / validate rejected it), so no statement about wasm eligibility
/// can be made.
inline constexpr std::string_view kSourceRejected = "conformance.ELIGIBILITY_SOURCE_REJECTED";
/// The manifest declares an entry identity that does not resolve inside the
/// compiled Core program.
inline constexpr std::string_view kEntryUnresolved = "conformance.ELIGIBILITY_ENTRY_UNRESOLVED";
} // namespace wasm_eligibility_diag

/// The single code -> verdict table. Pure, total, and the only place that
/// decides which diagnostic family owns which verdict.
[[nodiscard]] WasmEligibilityVerdict
classify_wasm_diagnostic(std::string_view code) noexcept;

/// Runs the real compile + emit pipeline for `loaded` and classifies the
/// outcome. Pure with respect to the repository (nothing is written); the
/// loaded case's `source_path` is the input.
[[nodiscard]] WasmEligibilityClassification
classify_wasm_eligibility(const LoadedConformanceCase &loaded);

/// Cross-checks a manifest's declared wasm lane against the computed verdict.
/// Returns an empty optional when they agree, else a human-readable divergence
/// naming both sides. Fail-closed in BOTH directions: an overclaim
/// (`orchestration` on a case the compiler rejects) and a stale skip
/// (`computation` on a case that now runs) are both errors, so the skip list
/// stays machine-verified rather than hand-curated.
[[nodiscard]] std::optional<std::string>
wasm_eligibility_divergence(const ConformanceCase &manifest,
                            const WasmEligibilityClassification &computed);

} // namespace ahfl::conformance
