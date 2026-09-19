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
//   (c) enum_variant_e2e / if_let_e2e / e2e_multi_agent classify blocked,
//       asserting the blocking construct, proven by actually invoking
//       lower_ahfl_to_core + compute_core_layouts + emit_core_wasm;
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
        {"float_output_e2e.case.json", WasmEligibilityVerdict::RunnableOrchestration, "", ""},
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
    }

    // These three are genuinely blocked TODAY, each at a distinct construct.
    // The codes and messages are the compiler's own; pinning them keeps the
    // classifier honest about WHY a case is skipped instead of hand-curating a
    // skip list.
    const std::vector<CaseVerdict> blocked = {
        // A match over a struct-payload enum in the agent body: the flow
        // carries a real pattern arena, so the orchestration lane bails before
        // it ever reaches the handler.
        {"enum_variant_e2e.case.json",
         WasmEligibilityVerdict::BlockedComputation,
         "wasm.UNSUPPORTED_WORKFLOW_FRAME",
         "hidden pattern arena"},
        // An if-let over a generic enum payload: the handler's first statement
        // is not the canonical unprojected input let the orchestration line
        // forwards.
        {"if_let_e2e.case.json",
         WasmEligibilityVerdict::BlockedComputation,
         "wasm.UNSUPPORTED_WORKFLOW_FRAME",
         "unprojected canonical input frame"},
        // A multi-agent DAG whose non-final handlers carry context stores and
        // an enum-equality branch: neither is a single bare goto.
        {"e2e_multi_agent.case.json",
         WasmEligibilityVerdict::BlockedComputation,
         "wasm.UNSUPPORTED_CAPABILITY_FRAME",
         "non-final handler to contain exactly one goto"},
    };
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

    // Tampering: a manifest marking a blocked case wasm-eligible is rejected.
    // This is the exact overclaim the slice exists to catch.
    const auto blocked_manifest = load_manifest(repo_root, "enum_variant_e2e.case.json");
    check(blocked_manifest.has_value(), "enum_variant manifest loads for the overclaim probe");
    if (blocked_manifest.has_value()) {
        ConformanceCase tampered = *blocked_manifest;
        tampered.engines.wasm.eligibility = WasmEligibility::Orchestration;
        tampered.engines.wasm.reason = "hand-curated claim that wasm can run this";
        const auto classification = classify_case(repo_root, "enum_variant_e2e.case.json");
        const auto divergence = wasm_eligibility_divergence(tampered, classification);
        check(divergence.has_value(), "wasm-eligible overclaim of a blocked case is rejected");
        if (divergence.has_value()) {
            check(divergence->find("overclaims") != std::string::npos,
                  "overclaim divergence names the overclaim");
            check(divergence->find("wasm.UNSUPPORTED_WORKFLOW_FRAME") != std::string::npos,
                  "overclaim divergence cites the blocking code");
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

    // The lanes are NOT interchangeable: a case blocked at a KR6.6 seam claims
    // `computation` (only that lane lifts the block), so a `none` lane on the
    // SAME case is a lie -- and, symmetrically, `computation` on a case the
    // compiler blocks outside that lane is a lie too. Without this the skip
    // list could not tell "needs the computation lane" from "permanently
    // host-side".
    const auto blocked_manifest_none = load_manifest(repo_root, "enum_variant_e2e.case.json");
    check(blocked_manifest_none.has_value(),
          "enum_variant manifest loads for the lane-projection probe");
    if (blocked_manifest_none.has_value()) {
        const auto classification = classify_case(repo_root, "enum_variant_e2e.case.json");
        check(classification.verdict == WasmEligibilityVerdict::BlockedComputation,
              "enum_variant is blocked at a KR6.6 computation seam");
        ConformanceCase host_only = *blocked_manifest_none;
        host_only.engines.wasm.eligibility = WasmEligibility::None;
        host_only.engines.wasm.reason = "permanently host-side";
        check(wasm_eligibility_divergence(host_only, classification).has_value(),
              "a `none` lane on a KR6.6-blocked case is rejected as a lane mismatch");
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
