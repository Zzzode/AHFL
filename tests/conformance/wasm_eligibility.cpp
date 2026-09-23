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

namespace layout_diag = ir::core::layout;

// The ORCHESTRATION lane's fail-closed seams. Each of these is raised by
// `emit_core_wasm` exactly where the P5 subset runs out of expressiveness, and
// each is precisely what the KR6.6 computation lane (P6-1..P6-6) exists to
// lift: an out-of-subset handler body, a capability frame shape the
// orchestration line cannot forward, a workflow region it cannot package, or a
// reachable import whose ABI cannot be wire-projected.
//
// Every one of these IS reachable through `emit_core_wasm`:
//   * `wasm.UNSUPPORTED_ORCHESTRATION` is the DEFAULT `AgentPlanPolicy::
//     unsupported_code`, so it is what every non-capability P6 fail-closed
//     reject raises (the `: policy.unsupported_code` arm of the capability
//     ternaries, plus every `P6ComputationHandlerBuilder::reject`).
//   * the other three are the capability/workflow arms of those same ternaries
//     and the explicit workflow-packaging policy.
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

// The ONE verdict -> manifest-lane projection. It is the cross-field constraint
// the manifest schema cannot state on its own: only a KR6.6 computation boundary
// is liftable by the KR6.6 lane, so only it may be declared `computation`;
// everything else is outside the wasm contract entirely (`none`), which is what
// stops a `none` case from silently carrying a KR6.6 skip reason.
WasmEligibility required_wasm_lane(WasmEligibilityVerdict verdict) noexcept {
    switch (verdict) {
    case WasmEligibilityVerdict::RunnableOrchestration:
        return WasmEligibility::Orchestration;
    case WasmEligibilityVerdict::BlockedComputation:
        return WasmEligibility::Computation;
    case WasmEligibilityVerdict::BlockedLayout:
    case WasmEligibilityVerdict::BlockedUnsupportedOrchestration:
        return WasmEligibility::None;
    }
    return WasmEligibility::None;
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

[[nodiscard]] WasmEligibilityClassification runnable(
    std::size_t artifact_bytes, ahfl::backends::CoreWasmFrameContract frame_contract) {
    WasmEligibilityClassification classification;
    classification.verdict = WasmEligibilityVerdict::RunnableOrchestration;
    classification.artifact_bytes = artifact_bytes;
    classification.frame_contract = frame_contract;
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
    const auto frame_contract = emitted.descriptor.has_value()
                                    ? emitted.descriptor->frame_contract
                                    : ahfl::backends::CoreWasmFrameContract::WireJson;
    return runnable(emitted.artifact->bytes.size(), frame_contract);
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

    // Pinned Node-observation skip expectation vs the computed emit facts,
    // checked independently of (and in addition to) the lane equality below.
    // This is the no-Node-required half of the p6-7 skip-set pin: the manifest
    // must declare EXACTLY the skip the compiler produces, in both directions.
    using Skip = WasmNodeObservationSkip;
    const Skip required_skip = [&] {
        if (verdict == WasmEligibilityVerdict::RunnableOrchestration) {
            return computed.frame_contract == ahfl::backends::CoreWasmFrameContract::RawP6Frame
                       ? Skip::RawP6FrameAwaitsP67
                       : Skip::None;
        }
        if (verdict == WasmEligibilityVerdict::BlockedComputation) {
            return Skip::BlockedOnKr66;
        }
        return Skip::None;
    }();
    // RFC 0026 FB-3b: a case that RUNS on the wasm orchestration lane but whose
    // surfaced construct (user-defined pure fn calls / first-class closures)
    // the in-process evaluator does not yet execute is a deliberate,
    // manifest-declared node-only observation (KR6.8 retires that evaluator
    // surface). The compiler-derived emit facts cannot detect the evaluator
    // gap, so this one skip is the manifest's honest claim; it is still
    // validated below as being attached to a genuinely RUNNABLE artifact (a
    // blocked case may not claim it), and the Node runner enforces the exact
    // set via its pinned census. Every other skip must equal the computed one.
    const bool declared_node_only =
        declared.node_observation_skip == Skip::EvaluatorSurfaceAwaitsKr68;
    if (declared_node_only) {
        if (verdict != WasmEligibilityVerdict::RunnableOrchestration) {
            return "manifest declares engines.wasm.node_observation_skip="
                   "'evaluator_surface_awaits_kr68' but the module does not emit on the "
                   "orchestration lane (" +
                   std::string{wasm_eligibility_verdict_name(verdict)} +
                   ": " + computed.reason +
                   ") -- a node-only observation requires a runnable artifact";
        }
    } else if (declared.node_observation_skip != required_skip) {
        return "manifest's engines.wasm.node_observation_skip declaration does not match the "
               "computed emit outcome (verdict " +
               std::string{wasm_eligibility_verdict_name(verdict)} + ", frame_contract " +
               (computed.frame_contract == ahfl::backends::CoreWasmFrameContract::RawP6Frame
                    ? "raw_p6_frame"
                    : "wire_json") +
               ") -- the Node differential skip set is pinned by this declaration";
    }

    // TOTAL lane equality. `computation` and `none` are distinct claims and are
    // never interchangeable, so a `none` lane on a KR6.6-blocked case (or the
    // converse) is a divergence in its own right -- the skip reason must name
    // the lane the block actually belongs to.
    const WasmEligibility required = required_wasm_lane(verdict);
    if (declared.eligibility == required) {
        return std::nullopt;
    }

    // Both divergence directions are errors. An overclaim advertises a lane
    // the compiler rejects; a stale skip pins a case to a blocked lane the
    // emit path no longer takes, so the skip reason is a lie.
    const std::string detail = "manifest declares engines.wasm.eligible='" +
                               std::string{lane(declared.eligibility)} + "' (reason: '" +
                               declared.reason + "')";
    if (verdict == WasmEligibilityVerdict::RunnableOrchestration) {
        return "manifest marks the case wasm-ineligible but the compiler emits it "
               "(" + std::to_string(computed.artifact_bytes) + " bytes); " + detail +
               " -- the skip reason is stale";
    }
    if (required == WasmEligibility::None && declared.eligibility == WasmEligibility::Computation) {
        return "manifest marks the case as needing the KR6.6 computation lane but the "
               "compiler blocks it outside that lane (" +
               std::string{wasm_eligibility_verdict_name(verdict)} + ": " + computed.reason +
               "); " + detail +
               " -- the case is permanently host-side, so the KR6.6 skip reason is wrong";
    }
    return "manifest's declared wasm lane does not match the computed verdict (" +
           std::string{wasm_eligibility_verdict_name(verdict)} + ": " + computed.reason +
           "); " + detail + " -- the manifest overclaims wasm eligibility";
}

} // namespace ahfl::conformance
