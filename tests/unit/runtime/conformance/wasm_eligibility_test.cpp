// KR6.7 (RFC 0026 P7): WASM eligibility classifier unit test. Hand-rolled
// check()/main(), mirroring the other RFC 0026 slice tests.
//
// This test is the reason the classifier can be trusted as the skip-list
// authority: it does not merely assert a verdict, it asserts that the verdict
// CAME FROM the real compiler. Every rejection case names the blocking
// construct the emit path actually raised (the same strings the probes print),
// and the runnable case carries a non-zero artifact, proving the emit path ran
// to completion.
//
// Coverage (per slice spec):
//   (a) the pure code -> verdict table is total and classifies each diagnostic
//       family into its own bucket, including the layout family's two
//       spellings and an unknown code's fail-closed default;
//   (b) the five tests/golden/wasm orchestration cases classify
//       runnable_orchestration via a real lower+layout+emit with a non-empty
//       artifact and an empty diagnostic code;
//   (c) e2e_multi_agent, enum_variant_e2e and if_let_e2e all classify
//       runnable (V2-D computed-runner workflow packaging, including the
//       half-2 in-handler bridge / constructed SummaryInput lane), proven by
//       actually invoking lower_ahfl_to_core + compute_core_layouts +
//       emit_core_wasm;
//   (d) manifest-vs-computed agreement in both directions, and that a tampered
//       manifest marking a blocked case wasm-eligible is rejected while one
//       wrongly skipping a runnable case is rejected too.

#include "conformance/wasm_eligibility.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifndef AHFL_SOURCE_DIR
#error "AHFL_SOURCE_DIR must be defined by the test target"
#endif

namespace {

using ahfl::conformance::classify_wasm_diagnostic;
using ahfl::conformance::ConformanceCase;
using ahfl::conformance::load_conformance_case;
using ahfl::conformance::WasmEligibility;
using ahfl::conformance::WasmEligibilityClassification;
using ahfl::conformance::WasmEligibilityVerdict;
using ahfl::conformance::wasm_eligibility_divergence;
using ahfl::conformance::wasm_eligibility_verdict_name;
using ahfl::backends::CoreWasmFrameContract;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

[[nodiscard]] WasmEligibilityClassification classify_case(const std::filesystem::path &repo_root,
                                                          std::string_view sidecar_name) {
    const auto cases_dir = repo_root / "tests" / "conformance" / "cases";
    auto loaded = load_conformance_case(cases_dir / std::string{sidecar_name}, repo_root);
    if (loaded.has_errors() || !loaded.conformance_case.has_value()) {
        std::cerr << "ERROR: cannot load committed case " << sidecar_name << "\n";
        loaded.diagnostics.render(std::cerr);
        check(false, "committed case loads: " + std::string{sidecar_name});
        return {};
    }
    return ahfl::conformance::classify_wasm_eligibility(*loaded.conformance_case);
}

[[nodiscard]] std::optional<ConformanceCase> load_manifest(const std::filesystem::path &repo_root,
                                                           std::string_view sidecar_name) {
    const auto cases_dir = repo_root / "tests" / "conformance" / "cases";
    auto loaded = load_conformance_case(cases_dir / std::string{sidecar_name}, repo_root);
    if (loaded.has_errors() || !loaded.conformance_case.has_value()) {
        return std::nullopt;
    }
    return loaded.conformance_case->manifest;
}

// ---------------------------------------------------------------------------
// (a) code -> verdict table
// ---------------------------------------------------------------------------

void test_diagnostic_table() {
    using Verdict = WasmEligibilityVerdict;

    // Every orchestration-lane seam is a KR6.6 computation-lane boundary.
    for (const auto *code : {"wasm.UNSUPPORTED_ORCHESTRATION", "wasm.UNSUPPORTED_CAPABILITY_FRAME",
                             "wasm.UNSUPPORTED_WORKFLOW_FRAME", "wasm.INVALID_CAPABILITY_ABI"}) {
        check(classify_wasm_diagnostic(code) == Verdict::BlockedComputation,
              std::string{"orchestration seam is a computation boundary: "} + code);
    }

    // The P4-D layout family, in both its spellings.
    for (const auto *code : {"wasm.INVALID_LAYOUT", "core.layout.UNSUPPORTED", "core.layout.UNBOUNDED",
                             "core.layout.OVERFLOW", "core.layout.INFINITE_RECURSION",
                             "core.layout.INVALID"}) {
        check(classify_wasm_diagnostic(code) == Verdict::BlockedLayout,
              std::string{"layout code maps to blocked_layout: "} + code);
    }

    // Everything else fails closed into the catch-all bucket, keeping its code.
    for (const auto *code : {"wasm.INVALID_CORE", "wasm.ENTRY_AMBIGUOUS", "wasm.ENTRY_NOT_FOUND",
                             "wasm.UNSUPPORTED_TARGET", "core.UNLOWERED_STATEMENT", ""}) {
        check(classify_wasm_diagnostic(code) == Verdict::BlockedUnsupportedOrchestration,
              std::string{"unknown code fails closed: "} + code);
    }

    // The verdict spelling is the pinned wire name.
    check(wasm_eligibility_verdict_name(Verdict::RunnableOrchestration) == "runnable_orchestration",
          "runnable verdict spelling");
    check(wasm_eligibility_verdict_name(Verdict::BlockedComputation) == "blocked_computation",
          "computation verdict spelling");
    check(wasm_eligibility_verdict_name(Verdict::BlockedLayout) == "blocked_layout",
          "layout verdict spelling");
    check(wasm_eligibility_verdict_name(Verdict::BlockedUnsupportedOrchestration) ==
              "blocked_unsupported_orchestration",
          "unsupported verdict spelling");

    // The verdict -> manifest-lane projection is TOTAL and keeps `computation`
    // and `none` distinct: only a KR6.6 seam is liftable by the KR6.6 lane.
    check(ahfl::conformance::required_wasm_lane(Verdict::RunnableOrchestration) ==
              WasmEligibility::Orchestration,
          "runnable requires the orchestration lane");
    check(ahfl::conformance::required_wasm_lane(Verdict::BlockedComputation) ==
              WasmEligibility::Computation,
          "a KR6.6 boundary requires the computation lane");
    check(ahfl::conformance::required_wasm_lane(Verdict::BlockedLayout) ==
              WasmEligibility::None,
          "a layout block is not liftable by the KR6.6 lane");
    check(ahfl::conformance::required_wasm_lane(Verdict::BlockedUnsupportedOrchestration) ==
              WasmEligibility::None,
          "an out-of-contract block is never liftable by the KR6.6 lane");
}

// ---------------------------------------------------------------------------
// (b) + (c) the committed catalogue, classified by the real emit path
// ---------------------------------------------------------------------------

// Every committed sidecar, discovered from the on-disk case directory rather
// than a hand-maintained list, so a newly committed case can never be silently
// omitted from the machine-verified skip-list cross-check.
[[nodiscard]] std::vector<std::string>
discover_case_sidecars(const std::filesystem::path &repo_root) {
    const auto cases_dir = repo_root / "tests" / "conformance" / "cases";
    std::vector<std::string> discovered;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(cases_dir, ec)) {
        if (entry.is_regular_file() &&
            ahfl::conformance::detail::is_conformance_case_sidecar(entry.path())) {
            discovered.push_back(entry.path().filename().string());
        }
    }
    check(!ec, "conformance cases directory scanned without error");
    std::sort(discovered.begin(), discovered.end());
    return discovered;
}

struct CaseVerdict {
    std::string_view sidecar;
    WasmEligibilityVerdict verdict;
    std::string_view code; // empty for runnable
    std::string_view reason_substring;
    // The exact frame contract the emitted descriptor must carry. Pins the
    // p6-frame SET independently of the Node runner: only the raw P4-D frame
    // handlers (aggregate/collection) may be P6Frame; every other runnable
    // case must be WireJson.
    CoreWasmFrameContract frame_contract{CoreWasmFrameContract::WireJson};
};

void test_committed_catalogue(const std::filesystem::path &repo_root) {
    // The four E1-E3 orchestration cases plus the float-output identity
    // workflow run TODAY: they emit real bytes, so they must classify runnable
    // -- that is what makes the classifier a check on the compiler rather than
    // a restatement of the manifest.
    const std::vector<CaseVerdict> runnable = {
        {"e1_identity_agent.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"e2_capability_agent.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"e3_identity_workflow.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"e3_capability_workflow.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // WH-5c.5 GAP 4: three-node identity+capability+identity workflow; the
        // per-node output stash table makes every node output host-observable.
        {"wh5c5_gap4_stash_parity.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"float_output_e2e.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // KR6.7: landed KR6.6 (P6) computation slices emit cleanly on the agent
        // lane with an identity final forwarding the borrowed wire frame.
        {"p6_scalar_cond.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_neg_compare.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_cascade.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_enum.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_coerce.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_coerce_bounds.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_depth3.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_fallthrough.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // KR6.7 corpus widening (FB-5): the P6 expression/control-flow
        // companion fixtures previously exercised only by bespoke Node probes.
        // Every one emits cleanly on the orchestration lane with an identity
        // final forwarding the borrowed wire frame, and now differentially
        // compares under BOTH engines.
        {"p6_cascade_high.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_elseless_fallthrough.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_elseless_taken.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // KR6.6 P6 f64 arithmetic ladder: f64.const/add/sub/mul/div/neg and
        // f64.gt comparison through the dedicated F64 scalar kind, with the
        // computed-final materializer copying the Float result through the
        // P6 frame.
        {"p6_f64_arith.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"p6_f64_compare.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"p6_implies.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_binding_payload.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_expr.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_fallthrough.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_guard.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_or.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_match_result_i64.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_depth3_taken.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_elseless.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_elseless_taken.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_projection.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_nested_taken_high.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"p6_unwrap_some.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // KR6.7 corpus widening (FB-5): the V2-A computed-final fixtures
        // (scalar / nested-aggregate / tag-only-enum / if-let-return /
        // payload-bearing-enum materialization) now run as manifest-driven
        // p6-frame cases with real differential agreement, so each descriptor
        // MUST carry P6Frame.
        {"v2a_computed_aggregate.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2a_computed_enum.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2a_computed_if_let_return.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2a_computed_payload_enum.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2a_computed_scalar.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // KR6.7 corpus widening (FB-5): the V2-B String/computed-final and
        // bounded-arena fixtures now run as manifest-driven p6-frame cases
        // (String PtrLen projection / rodata literals / bounded list packing).
        // v2b_enum_string stays OUT of the catalogue: the retired evaluator
        // misrouted its String tuple-variant construction through the
        // capability invoker while the wasm run is correct; the fixture can
        // be re-added when a blessing is captured.
        {"v2b_bounded_string.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2b_computed_string.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2b_list_nested_string_arena.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2b_list_string_arena.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"v2b_string_passthrough.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // The bounded-String durable-resume replay workflow is the opaque
        // WireJson two-node capability DAG (same lane as e3_capability_workflow).
        {"e3_capability_workflow_resume.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         ""},
        // KR6.7 corpus widening (FB-5): expectation-lane FB-1/FB-3 cases whose
        // user pure-fn / first-class-closure constructs have no checked-in
        // blessing. The modules emit cleanly on the orchestration lane
        // (WireJson identity finals) and the Node observation compares
        // against the manifest's blessed expectation.
        {"fb1_aggregate_direct_call.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb1_direct_call.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb3_byvalue_capture.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb3_string_capture.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb3_f64_capture.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb3_nested_activation.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        {"fb3_nested_lambda_flow.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // KR6.7 corpus widening (FB-5) slice E: the FB-4 effect-classification
        // pure fn case (expectation-lane KR6.8 surface) and the least-privilege
        // capability-free agent compiled from a two-agent module (zero imports).
        {"fb4_cross_agent_capability_leak.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         ""},
        {"fb4_effect_clause_pure_body.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         ""},
        // P4-D input-frame handlers (P6-4 aggregate / P6-5 collection) emit
        // cleanly as P6-7 p6-frame modules: they carry the core-layout +
        // boundary wire-schema sections and export runv, so their canonical
        // output is observed by packing input, running runv, and encoding the
        // frame. Their descriptor MUST carry P6Frame -- this exact-set pin means
        // a codegen change that drops (or adds) the frame sections is caught
        // here even without Node installed.
        {"p6_aggregate.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", "",
         CoreWasmFrameContract::P6Frame},
        {"p6_collection.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", "",
         CoreWasmFrameContract::P6Frame},
        // Two same-typed input lists: the per-edge placement regression is a
        // p6-frame module like p6_collection (two disjoint backing placements).
        {"p6_frame_two_containers.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // Frame-bridge v2 rung V2-C: a direct agent invokes a multi-argument
        // capability from a non-final handler through the additive
        // (i32)->(i32,i32) control-block bridge, then materializes a computed
        // final carrying the capability-produced String. It emits as a
        // P6Frame module (dense control blocks + disjoint result placements).
        {"v2c_multi_arg_bridge.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // V2-C fix-forward: two computed non-final handlers each invoke the same
        // capability (two dense sites sharing one import ordinal); reachability
        // must keep both handlers and the module stays P6Frame.
        {"v2c_bridge_chain.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // V2-C fix-forward: a producing pattern-arena match followed by an
        // ordered bridge statement outside the arms (route then call).
        {"v2c_route_then_bridge.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // V2-C fix-forward: one tag-only-enum bridge argument; still a P6Frame
        // bridge module, with a single non-Struct argument wrapped {"value":..}.
        {"v2c_single_enum_bridge.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // RFC 0026 FB-3b: a higher-order lambda / call_indirect program emits
        // cleanly on the orchestration lane (funcref table + closure env). Its
        // expectation-lane observation (no checked-in blessing) is
        // cross-checked separately by the Node runner against the manifest's
        // blessed expectation.
        {"fb3_higher_order.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
        // V2-D emission half 1: a workflow packaging a computed-final node now
        // emits as a P6-frame module: the relocated frame handlers run on the
        // node blocks, the in-module scheduler materializes the projected
        // node input, and the projected/constructed workflow return is copied
        // into the fixed workflow output slot.
        {"enum_variant_e2e.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        {"if_let_e2e.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // V2-D emission half 2: a multi-agent DAG whose packaged runners carry
        // computed-goto routing, scalar/tag-enum capability-result -> context
        // stores, in-handler bridges and a constructed SummaryInput now emits
        // and runs on the P6 frame lane.
        {"e2e_multi_agent.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5b.2: hybrid P6-bridge + opaque workflow DAG. The module emits
        // P6Frame (3 nodes, 2 imports); the bridge PENDING arm suspends the
        // workflow at the first bridge node. Runnable on the orchestration
        // lane; the suspend outcome is pinned by the case scenario, not by
        // eligibility.
        {"wh5b_bridge_pending_suspend.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5c.1: P6-packaged agent whose non-final handler constructs an
        // aggregate with a String field from the input frame and passes it to
        // a bridge capability. The module emits P6Frame; the relocated
        // workflow-lane runner names the construct root scratch_base()+offset
        // so the host decodes the descriptor. Runnable on the orchestration
        // lane; the completed outcome is pinned by the case scenario.
        {"wh5c_construct_bridge.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5c.2: two P6 nodes reusing ONE packaged agent instance. The
        // module emits P6Frame; Approach B assigns one node-frame block +
        // runner per P6 node so the fan-out lowers and both nodes run.
        {"wh5c_instance_reuse_fanout.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5c.4 (GAP 2): an opaque-lane capability-final agent that
        // constructs the capability argument in-module. The module emits
        // P6Frame with a P4D_TO_JSON self-transcode for the constructed O_k
        // and a JSON_TO_P4D workflow-output crossing. Runnable on the
        // orchestration lane; the Node observation withholds on
        // host_transcode_awaits_node_port (the JS host lacks the ahfl_xcode
        // transcode adapter).
        {"wh5c4_construct_cap_final.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5c.4 P0-3: a construct-capability terminal whose upstream is an
        // opaque capability-final node. The construct node carries BOTH a
        // JSON_TO_P4D input crossing AND a P4D_TO_JSON self-transcode; the
        // two-slot scheduler table keeps both. Runnable on the orchestration
        // lane; the Node observation withholds on
        // host_transcode_awaits_node_port.
        {"wh5c4_p03_opaque_upstream.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
        // WH-5c.7: the 9-shape rich wire-type matrix (Int/String/Decimal/
        // Duration/Set/Map/Option/Unit/Float) round-trips through the P6
        // frame packer/reader and the computed-final + workflow-level
        // materializers.
        {"wh5c7_rich_input_matrix.case.json",
         WasmEligibilityVerdict::RunnableOrchestration,
         "",
         "",
         CoreWasmFrameContract::P6Frame},
    };
    for (const auto &expectation : runnable) {
        const auto classification = classify_case(repo_root, expectation.sidecar);
        check(classification.verdict == expectation.verdict,
              std::string{"runnable verdict: "} + std::string{expectation.sidecar});
        check(classification.code.empty(),
              std::string{"runnable carries no diagnostic code: "} + std::string{expectation.sidecar});
        check(classification.artifact_bytes > 0,
              std::string{"runnable emitted a non-empty artifact: "} +
                  std::string{expectation.sidecar});
        check(!classification.reason.empty(),
              std::string{"runnable carries a reason: "} + std::string{expectation.sidecar});
        check(classification.frame_contract == expectation.frame_contract,
              std::string{"runnable frame_contract pin (WireJson vs P6Frame): "} +
                  std::string{expectation.sidecar});
    }

    // No committed case is blocked on this lane: the last one
    // (e2e_multi_agent) moved to RunnableOrchestration with V2-D emission half
    // 2. The catalogue-coverage loop below still runs with an empty table.
    const std::vector<CaseVerdict> blocked = {};
    for (const auto &expectation : blocked) {
        const auto classification = classify_case(repo_root, expectation.sidecar);
        check(classification.verdict == expectation.verdict,
              std::string{"blocked verdict: "} + std::string{expectation.sidecar});
        check(classification.code == expectation.code,
              std::string{"blocked code: "} + std::string{expectation.sidecar} + " got '" +
                  classification.code + "'");
        check(classification.reason.find(expectation.reason_substring) != std::string::npos,
              std::string{"blocked reason names the construct: "} +
                  std::string{expectation.sidecar});
        check(classification.artifact_bytes == 0,
              std::string{"blocked emitted no artifact: "} + std::string{expectation.sidecar});
    }

    // The two tables above must cover the committed catalogue EXACTLY, so a
    // newly committed case cannot be classified by the divergence check while
    // silently escaping the "why is this blocked" pinning.
    std::vector<std::string> pinned;
    pinned.reserve(runnable.size() + blocked.size());
    for (const auto &expectation : runnable) {
        pinned.emplace_back(expectation.sidecar);
    }
    for (const auto &expectation : blocked) {
        pinned.emplace_back(expectation.sidecar);
    }
    std::sort(pinned.begin(), pinned.end());
    check(pinned == discover_case_sidecars(repo_root),
          "the pinned catalogue equals the discovered committed case set");
}

// ---------------------------------------------------------------------------
// (d) manifest-vs-computed agreement, both directions
// ---------------------------------------------------------------------------

void test_manifest_agreement(const std::filesystem::path &repo_root) {
    // Every committed manifest must agree with the computed verdict. This is
    // the machine-generated skip list: a stale reason or an overclaim fails
    // here even though no WASM engine is installed. The set is DISCOVERED, so
    // neither this table nor the tables above can quietly omit a case.
    const auto sidecars = discover_case_sidecars(repo_root);
    check(!sidecars.empty(), "at least one committed case sidecar was discovered");
    for (const auto &sidecar : sidecars) {
        const auto manifest = load_manifest(repo_root, sidecar);
        check(manifest.has_value(), std::string{"manifest loads: "} + sidecar);
        if (!manifest.has_value()) {
            continue;
        }
        const auto classification = classify_case(repo_root, sidecar);
        const auto divergence = wasm_eligibility_divergence(*manifest, classification);
        if (divergence.has_value()) {
            check(false, std::string{"committed manifest agrees with the compiler: "} + sidecar +
                             " -- " + *divergence);
        }
    }

    // Tampering: a manifest whose skip reason claims the KR6.6 lane for a case
    // the compiler now RUNS must be rejected (the skip is stale).
    const auto runnable_manifest = load_manifest(repo_root, "e1_identity_agent.case.json");
    check(runnable_manifest.has_value(), "e1 manifest loads for the stale-skip probe");
    if (runnable_manifest.has_value()) {
        ConformanceCase tampered = *runnable_manifest;
        tampered.engines.wasm.eligibility = WasmEligibility::Computation;
        tampered.engines.wasm.reason = "needs in-wasm expression evaluation (stale claim)";
        const auto classification = classify_case(repo_root, "e1_identity_agent.case.json");
        const auto divergence = wasm_eligibility_divergence(tampered, classification);
        check(divergence.has_value(), "stale skip of a runnable case is rejected");
        if (divergence.has_value()) {
            check(divergence->find("stale") != std::string::npos,
                  "stale-skip divergence names the staleness");
        }
    }

    // Tampering: a manifest claiming the KR6.6 computation lane for a case the
    // compiler RUNS on the orchestration lane is an overclaim the classifier
    // rejects. e2e_multi_agent now emits on the P6 workflow lane (V2-D
    // emission half 2), so flipping its manifest back to "computation" (the
    // stale blocked shape) must diverge.
    const auto now_runnable_manifest = load_manifest(repo_root, "e2e_multi_agent.case.json");
    check(now_runnable_manifest.has_value(),
          "e2e_multi_agent manifest loads for the overclaim probe");
    if (now_runnable_manifest.has_value()) {
        ConformanceCase tampered = *now_runnable_manifest;
        tampered.engines.wasm.eligibility = WasmEligibility::Computation;
        tampered.engines.wasm.reason =
            "stale hand-curated claim that a KR6.6 seam blocks this";
        const auto classification = classify_case(repo_root, "e2e_multi_agent.case.json");
        const auto divergence = wasm_eligibility_divergence(tampered, classification);
        check(divergence.has_value(),
              "a stale computation-lane claim on a runnable case is rejected");
        if (divergence.has_value()) {
            check(divergence->find("stale") != std::string::npos,
                  "overclaim divergence names the staleness");
        }
    }

    // A `none` (permanently host-side) manifest on a runnable case is also a
    // divergence: `none` claims wasm can never run it.
    const auto none_manifest = load_manifest(repo_root, "e1_identity_agent.case.json");
    if (none_manifest.has_value()) {
        ConformanceCase tampered = *none_manifest;
        tampered.engines.wasm.eligibility = WasmEligibility::None;
        const auto classification = classify_case(repo_root, "e1_identity_agent.case.json");
        check(wasm_eligibility_divergence(tampered, classification).has_value(),
              "host-only claim on a runnable case is rejected");
        // ... while the untouched manifest agrees.
        check(!wasm_eligibility_divergence(*none_manifest, classification).has_value(),
              "untouched orchestration manifest agrees");
    }

    // The lanes are NOT interchangeable: a `none` (permanently host-side)
    // claim on a case the compiler RUNS on the orchestration lane is a lie.
    // e2e_multi_agent now emits on the P6 workflow lane (V2-D emission half 2),
    // so it is the runnable case this lane-projection probe uses.
    const auto runnable_manifest_none = load_manifest(repo_root, "e2e_multi_agent.case.json");
    check(runnable_manifest_none.has_value(),
          "e2e_multi_agent manifest loads for the lane-projection probe");
    if (runnable_manifest_none.has_value()) {
        const auto classification = classify_case(repo_root, "e2e_multi_agent.case.json");
        check(classification.verdict == WasmEligibilityVerdict::RunnableOrchestration,
              "e2e_multi_agent runs on the orchestration P6 workflow lane");
        ConformanceCase host_only = *runnable_manifest_none;
        host_only.engines.wasm.eligibility = WasmEligibility::None;
        host_only.engines.wasm.reason = "permanently host-side";
        check(wasm_eligibility_divergence(host_only, classification).has_value(),
              "a `none` lane on an orchestration-runnable case is rejected as a lane mismatch");
    }
}

// A source that does not even compile is refused before any wasm statement is
// made (the reason code names the gate, never a wasm code).
void test_uncompilable_source(const std::filesystem::path &repo_root) {
    const auto load = [&](std::string_view sidecar) {
        const auto cases_dir = repo_root / "tests" / "conformance" / "cases";
        return load_conformance_case(cases_dir / std::string{sidecar}, repo_root);
    };

    auto loaded = load("e1_identity_agent.case.json");
    check(loaded.conformance_case.has_value(), "e1 case loads for the source-gate probe");
    if (!loaded.conformance_case.has_value()) {
        return;
    }
    // Point the loaded case at a source that is not valid AHFL.
    const auto broken = repo_root / "tests" / "conformance" / "cases" /
                        "does_not_exist_source.ahfl";
    loaded.conformance_case->source_path = broken;
    const auto classification = ahfl::conformance::classify_wasm_eligibility(*loaded.conformance_case);
    check(classification.verdict == WasmEligibilityVerdict::BlockedUnsupportedOrchestration,
          "uncompilable source fails closed into the catch-all bucket");
    check(classification.code == "conformance.ELIGIBILITY_SOURCE_REJECTED",
          "uncompilable source names the source gate, not a wasm code");
    check(classification.artifact_bytes == 0, "uncompilable source emits nothing");
}

} // namespace

int main(int argc, char **argv) {
    (void)argv;
    if (argc < 1) {
        return 2;
    }
    const std::filesystem::path repo_root{AHFL_SOURCE_DIR};

    test_diagnostic_table();
    test_committed_catalogue(repo_root);
    test_manifest_agreement(repo_root);
    test_uncompilable_source(repo_root);

    if (g_failures != 0) {
        std::cerr << g_failures << " wasm eligibility test(s) failed\n";
        return 1;
    }
    std::cout << "all wasm eligibility classifier tests passed\n";
    return 0;
}
