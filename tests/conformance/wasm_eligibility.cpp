#include "conformance/wasm_eligibility.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "conformance/compile_source.hpp"

namespace ahfl::conformance {

namespace {

namespace core_diag = ir::core::diag;
namespace layout_diag = ir::core::layout;

// The ORCHESTRATION lane's fail-closed seams. Each of these is raised by
// `emit_core_wasm` exactly where the P5 subset runs out of expressiveness, and
// each is precisely what the KR6.6 computation lane (P6-1..P6-6) exists to
// lift: an out-of-subset handler body, a capability frame shape the
// orchestration line cannot forward, a workflow region it cannot package, or a
// reachable import whose ABI cannot be wire-projected.
//
// This is the ONE place that mapping lives. `classify_wasm_diagnostic` is pure
// and total over it, so adding a future seam means adding one table entry, not
// editing a branch chain.
constexpr std::string_view kOrchestrationSeams[] = {
    backends::core_wasm_diag::kUnsupportedOrchestration,
    backends::core_wasm_diag::kUnsupportedCapabilityFrame,
    backends::core_wasm_diag::kUnsupportedWorkflowFrame,
    backends::core_wasm_diag::kInvalidCapabilityAbi,
};

// Physical-layout failures: the P4-D builder refused to size a value
// (`core.layout.*`), or codegen found no finalized layout for a boundary type.
[[nodiscard]] bool is_layout_code(std::string_view code) noexcept {
    return code == backends::core_wasm_diag::kInvalidLayout ||
           code.starts_with("core.layout.");
}

} // namespace

std::string_view wasm_eligibility_verdict_name(WasmEligibilityVerdict verdict) noexcept {
    switch (verdict) {
    case WasmEligibilityVerdict::RunnableOrchestration:
        return "runnable_orchestration";
    case WasmEligibilityVerdict::BlockedComputation:
        return "blocked_computation";
    case WasmEligibilityVerdict::BlockedLayout:
        return "blocked_layout";
    case WasmEligibilityVerdict::BlockedUnsupportedOrchestration:
        return "blocked_unsupported_orchestration";
    }
    return "blocked_unsupported_orchestration";
}

WasmEligibilityVerdict classify_wasm_diagnostic(std::string_view code) noexcept {
    for (const auto seam : kOrchestrationSeams) {
        if (code == seam) {
            return WasmEligibilityVerdict::BlockedComputation;
        }
    }
    if (is_layout_code(code)) {
        return WasmEligibilityVerdict::BlockedLayout;
    }
    return WasmEligibilityVerdict::BlockedUnsupportedOrchestration;
}

namespace {

// A verdict is a rejection unless every pipeline stage succeeded; the shape
// carries the code and message so the reason string is always the compiler's
// own words rather than a paraphrase.
[[nodiscard]] WasmEligibilityClassification
blocked(std::string_view code, std::string message) {
    WasmEligibilityClassification classification;
    classification.verdict = classify_wasm_diagnostic(code);
    classification.code = std::string(code);
    classification.reason = std::string(code) + ": " + std::move(message);
    return classification;
}

[[nodiscard]] WasmEligibilityClassification runnable(std::size_t artifact_bytes) {
    WasmEligibilityClassification classification;
    classification.verdict = WasmEligibilityVerdict::RunnableOrchestration;
    classification.artifact_bytes = artifact_bytes;
    // The reason names the lane the case actually cleared. It is a stable,
    // case-independent string: the E1-E3 orchestration subset plus the landed
    // P6 computation slices are what "runs today" means, so the classifier
    // never needs to re-derive which subset feature a given case used.
    classification.reason =
        "emits cleanly on the wasm orchestration lane (E1-E3 + landed P6 computation slices)";
    return classification;
}

} // namespace

WasmEligibilityClassification classify_wasm_eligibility(const LoadedConformanceCase &loaded) {
    const ConformanceCase &manifest = loaded.manifest;

    std::string error;
    auto program = compile_conformance_source(loaded.source_path, error);
    if (!program.has_value()) {
        return blocked(wasm_eligibility_diag::kSourceRejected, std::move(error));
    }

    const auto core = ir::core::lower_ahfl_to_core(*program);
    if (!core.ok()) {
        const auto &first = core.diagnostics.front();
        return blocked(first.code, first.message);
    }

    const auto layouts = ir::core::compute_core_layouts(core.program);
    if (!layouts.ok() || !layouts.table.has_value()) {
        if (layouts.diagnostics.empty()) {
            return blocked(layout_diag::kInvalid,
                           "layout builder returned no table without a diagnostic");
        }
        const auto &first = layouts.diagnostics.front();
        return blocked(first.code, first.message);
    }

    // Resolve the manifest's canonical entry through the same typed seam the
    // CLI and the probes use -- never by re-parsing the entry string.
    handoff::PackageMetadata package_metadata;
    package_metadata.entry_target = handoff::ExecutableRef{
        manifest.kind == CaseKind::Agent ? handoff::ExecutableKind::Agent
                                         : handoff::ExecutableKind::Workflow,
        manifest.entry,
    };
    const auto entry = backends::resolve_core_wasm_entry(core.program, &package_metadata);
    if (!entry.has_value()) {
        return blocked(entry.error().code, entry.error().message);
    }

    const auto emitted = backends::emit_core_wasm(
        core.program, *layouts.table, {*entry, backends::WasmProfileKind::Wasi});
    if (!emitted.ok()) {
        // `ok()` is false either because an artifact is absent or because
        // diagnostics were raised; the landing invariant is that a rejection
        // has no partial artifact. Fail closed rather than trusting the
        // invariant.
        if (emitted.diagnostics.empty()) {
            return blocked(backends::core_wasm_diag::kInternalInvalid,
                           "emit_core_wasm rejected without a diagnostic");
        }
        const auto &first = emitted.diagnostics.front();
        return blocked(first.code, first.message);
    }
    return runnable(emitted.artifact->bytes.size());
}

std::optional<std::string>
wasm_eligibility_divergence(const ConformanceCase &manifest,
                            const WasmEligibilityClassification &computed) {
    const WasmEngineEligibility &declared = manifest.engines.wasm;
    const auto lane = [](WasmEligibility eligibility) -> std::string_view {
        switch (eligibility) {
        case WasmEligibility::Orchestration:
            return "orchestration";
        case WasmEligibility::Computation:
            return "computation";
        case WasmEligibility::None:
            return "none";
        }
        return "none";
    };

    const WasmEligibilityVerdict verdict = computed.verdict;
    const bool agrees =
        (verdict == WasmEligibilityVerdict::RunnableOrchestration &&
         declared.eligibility == WasmEligibility::Orchestration) ||
        (verdict != WasmEligibilityVerdict::RunnableOrchestration &&
         declared.eligibility != WasmEligibility::Orchestration);
    if (agrees) {
        return std::nullopt;
    }

    // Both divergence directions are errors. An overclaim advertises a lane
    // the compiler rejects; a stale skip pins a case to `computation` (or
    // `none`) that the emit path already accepts, so the skip reason is a lie.
    const std::string detail = "manifest declares engines.wasm.eligible='" +
                               std::string{lane(declared.eligibility)} + "' (reason: '" +
                               declared.reason + "')";
    if (verdict == WasmEligibilityVerdict::RunnableOrchestration) {
        return "manifest marks the case wasm-ineligible but the compiler emits it "
               "(" + std::to_string(computed.artifact_bytes) + " bytes); " + detail +
               " -- the skip reason is stale";
    }
    return "manifest marks the case wasm-orchestration-eligible but the compiler "
           "rejects it (" + std::string{wasm_eligibility_verdict_name(verdict)} + ": " +
           computed.reason + "); " + detail + " -- the manifest overclaims wasm eligibility";
}

} // namespace ahfl::conformance
