// KR6.8 WH-5 (RFC 0026): native embedded-host conformance census runner.
//
// One binary drives, for every committed conformance case the wasm3 facade can
// execute:
//   1. the in-process evaluator adapter (canonical evaluator observation);
//   2. the native wasm3-backed facade (WasmWorkflowRuntime / WasmAgentRunner);
//   3. the differential comparator on status + state_sequence +
//      capability_sequence + capability_arguments + output_json.
//
// Unlike the Node differential lane (conformance_wasm_node_runner.cpp), this
// lane is PURE IN-PROCESS: it compiles the AHFL-IR program through the facade
// and drives the wasm3 engine directly, with no Node subprocess, no artifact
// staging, and no SKIP_RETURN_CODE 77. The facade compiles every workflow in
// the program to wasm in its constructor; a compile failure is a hard failure
// (the case must be orchestration-eligible), never a silent skip.
//
// The 7 evaluator_surface_awaits_kr68 (node-only) cases have no in-process
// evaluator reference; the native observation is compared directly against the
// manifest's blessed expectation, exactly as the Node lane does.
//
// Modes:
//   verify <repo-root> <cases-dir> [stem ...]
//   blessing-determinism <repo-root> <cases-dir> [stem ...]
//   mutation <repo-root> <cases-dir> <stem>
//
// Exit: 0 agreement, 1 divergence/infra failure, 2 usage.

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "base/json/json_value.hpp"
#include "conformance/conformance_case.hpp"
#include "conformance/evaluator_engine.hpp"
#include "conformance/native_engine.hpp"
#include "conformance/observation_compare.hpp"

namespace {

namespace fs = std::filesystem;
using ahfl::conformance::ConformanceScenario;
using ahfl::conformance::LoadedConformanceCase;
using ahfl::conformance::NativeScenarioResult;
using ahfl::conformance::WasmNodeObservationSkip;
using ahfl::conformance::detail::is_conformance_case_sidecar;
using ahfl::conformance::load_conformance_case;
using ahfl::conformance::node_observation_matches_expectation;
using ahfl::conformance::observations_agree;
using ahfl::conformance::run_evaluator_scenario;
using ahfl::conformance::run_native_scenario;

// Pinned census of the committed catalogue's native-observation outcomes.
// The native lane drives the wasm3 facade directly; every orchestration-
// eligible case must compile and agree. The skip set is pinned to 0: the
// facade never depends on an external embedding, so there is no legitimate
// skip path (a compile failure is a hard failure, not a skip).
// WH-5c.2 (GAP 1, Approach B): the fan-out case
// (wh5c_instance_reuse_fanout) reuses ONE packaged agent instance across
// TWO P6 workflow nodes. The per-instance D5 lifecycle gate rejected this
// shape; the per-node cardinality flip (one node-frame block + runner
// function per P6 NODE) lowers both nodes onto their own block/runner and
// the wasm observation agrees with the evaluator: 68/0 -> 69/0.
// WH-5c.4 (GAP 2): the construct-capability-final case
// (wh5c4_construct_cap_final) constructs the capability argument in-module
// and the wasm observation agrees with the evaluator: 69/0 -> 70/0.
// WH-5c.4 P0-3: the opaque-upstream construct-terminal case
// (wh5c4_p03_opaque_upstream) carries BOTH a JSON_TO_P4D input crossing
// and a P4D_TO_JSON self-transcode on one node; the two-slot scheduler
// table keeps both, and the P6-frame opaque-node state collection
// (workflow_session.cpp collect_opaque_node_states) recovers the opaque
// producer's state transitions so the native observation matches the
// evaluator: 70/0 -> 71/0.
// WH-5c.5: the GAP 4 stash-parity case (wh5c5_gap4_stash_parity) joins
// the agreed set (three-node identity+capability+identity workflow; the
// per-node output stash table makes every node output host-observable):
// 71/0 -> 72/0.
// WH-5c.7: the 9-shape rich wire-type matrix (Int/String/Decimal/Duration/
// Set/Map/Option/Unit/Float) round-trips through the P6 frame packer/reader
// and the computed-final + workflow-level materializers: 72/0 -> 73/0.
constexpr int kExpectedNativeAgreed = 73;
constexpr int kExpectedNativeSkipped = 0;

// Pinned STEM SET of cases allowed to declare
// engines.wasm.node_observation_skip='evaluator_surface_awaits_kr68' (the
// node-only lane). Same pin as the Node runner: a manifest edit that moves a
// comparable differential case onto the node-only lane while adding another
// comparable case would keep the 66/0 totals green; this exact-set pin catches
// that. Keep sorted; the runner compares the sorted observed set against it.
constexpr std::array<std::string_view, 7> kExpectedNodeOnlyStems{
    "fb1_aggregate_direct_call",
    "fb1_direct_call",
    "fb3_byvalue_capture",
    "fb3_higher_order",
    "fb3_nested_activation",
    "fb3_nested_lambda_flow",
    "fb4_effect_clause_pure_body",
};

int g_failures = 0;
int g_compared = 0;
int g_skipped = 0;
std::vector<std::string> g_node_only_stems;

// Internal sentinel for a structured per-case skip (a KR6.6-blocked case has
// no orchestration module). ctest never observes this code: the census pin
// (kExpectedNativeSkipped) expects g_skipped == 0, so a build that actually
// skips fails the pin before this value could propagate. It is distinct from
// the 0/1/2 process contract documented at the top of this file.
constexpr int kInternalNothingRanSentinel = 77;

void check(bool condition, std::string_view name) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

constexpr std::size_t kMaxCaseStemLength = 128;

[[nodiscard]] bool is_path_safe_stem(std::string_view stem) noexcept {
    if (stem.empty() || stem.size() > kMaxCaseStemLength) {
        return false;
    }
    if (stem == "." || stem == "..") {
        return false;
    }
    for (const char ch : stem) {
        const bool safe = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!safe) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string case_stem(const fs::path &sidecar) {
    auto name = sidecar.filename().string();
    const auto suffix = std::string{ahfl::conformance::detail::kConformanceCaseFileSuffix};
    if (name.size() >= suffix.size()) {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

struct CaseEntry {
    fs::path sidecar;
    LoadedConformanceCase loaded;
};

[[nodiscard]] std::vector<CaseEntry>
discover_cases(const fs::path &repo_root, const fs::path &cases_dir,
               const std::unordered_set<std::string> &stems) {
    std::vector<fs::path> sidecars;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(cases_dir, ec)) {
        if (entry.is_regular_file() && is_conformance_case_sidecar(entry.path())) {
            sidecars.push_back(entry.path());
        }
    }
    std::sort(sidecars.begin(), sidecars.end());

    std::vector<CaseEntry> cases;
    for (const auto &sidecar : sidecars) {
        const std::string stem = case_stem(sidecar);
        if (!is_path_safe_stem(stem)) {
            std::cerr << "ERROR: conformance case sidecar stem is not a path-safe "
                         "[A-Za-z0-9_-]+ name: "
                      << sidecar.filename() << "\n";
            ++g_failures;
            continue;
        }
        if (!stems.empty() && !stems.contains(stem)) {
            continue;
        }
        auto result = load_conformance_case(sidecar, repo_root);
        if (result.has_errors() || !result.conformance_case.has_value()) {
            std::cerr << "ERROR: failed to load case " << sidecar << "\n";
            result.diagnostics.render(std::cerr);
            ++g_failures;
            continue;
        }
        cases.push_back(
            CaseEntry{.sidecar = sidecar, .loaded = std::move(*result.conformance_case)});
    }
    return cases;
}

// Runs, compares, and returns the native observation for one scenario.
// Returns:
//   0 differential agreed (or node-only expectation matched);
//   1 a failure (and records g_failures);
//   77 this scenario skips (blocked_kr66).
int run_one(const CaseEntry &entry, const ConformanceScenario &scenario) {
    const auto &manifest = entry.loaded.manifest;
    const std::string stem = case_stem(entry.sidecar);
    const std::string label = stem + "/" + scenario.name;
    const WasmNodeObservationSkip declared_skip = manifest.engines.wasm.node_observation_skip;

    // A KR6.6 blocked case has no orchestration module; the facade cannot
    // compile it. This is a structured skip (the census pin expects 0).
    if (declared_skip == WasmNodeObservationSkip::BlockedOnKr66) {
        ++g_skipped;
        std::cout << "SKIP " << label << ": blocked_kr66\n";
        return kInternalNothingRanSentinel;
    }

    const bool node_only =
        declared_skip == WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68;

    // The evaluator observation is the differential reference, EXCEPT on the
    // node-only lane (the surfaced pure-fn/closure construct has no evaluator
    // yet); there the manifest's blessed expectation is the reference.
    std::optional<std::string> evaluator_observation;
    if (!node_only) {
        const auto evaluator = run_evaluator_scenario(entry.loaded, scenario);
        if (!evaluator.ok) {
            std::cerr << "ERROR: evaluator run failed for " << label << ":\n"
                      << evaluator.error << "\n";
            ++g_failures;
            return 1;
        }
        evaluator_observation = evaluator.observation_json;
    }

    // Drive the wasm3 facade directly in-process.
    const NativeScenarioResult native = run_native_scenario(entry.loaded, scenario);
    if (!native.ok) {
        std::cerr << "FAIL: native engine run failed for " << label << ":\n"
                  << native.error << "\n";
        ++g_failures;
        return 1;
    }

    if (node_only) {
        const auto divergence =
            node_observation_matches_expectation(scenario.expect, native.observation_json);
        if (divergence.has_value()) {
            std::cerr << "FAIL: node-only expectation mismatch for " << label << ":\n  "
                      << *divergence << "\n--- native ---\n" << native.observation_json << "\n";
            ++g_failures;
            return 1;
        }
        ++g_compared;
        g_node_only_stems.push_back(stem);
        std::cout << "OK: " << label
                  << " native observation matched the blessed manifest expectation "
                     "(node-only FB-3b lane; evaluator surface awaits KR6.8)\n";
        return 0;
    }

    const auto divergence =
        observations_agree(*evaluator_observation, native.observation_json);
    if (divergence.has_value()) {
        std::cerr << "FAIL: evaluator-vs-native differential for " << label << ":\n  "
                  << *divergence << "\n--- evaluator ---\n" << *evaluator_observation
                  << "\n--- native ---\n" << native.observation_json << "\n";
        ++g_failures;
        return 1;
    }

    ++g_compared;
    std::cout << "OK: " << label << " native differential agreed\n";
    return 0;
}

int mode_verify(const fs::path &repo_root, const fs::path &cases_dir,
                const std::unordered_set<std::string> &stems) {
    const auto cases = discover_cases(repo_root, cases_dir, stems);
    if (g_failures != 0) {
        return 1;
    }

    for (const auto &entry : cases) {
        for (const auto &scenario : entry.loaded.manifest.scenarios) {
            run_one(entry, scenario);
        }
    }

    std::cout << "Native embedded-engine differential: " << g_compared << " agreed, "
              << g_skipped << " skipped\n";
    if (g_compared == 0 && g_failures == 0) {
        std::cerr << "FAIL: no orchestration-eligible scenario was available\n";
        ++g_failures;
    }
    // Exact-set census: the pin must move deliberately whenever the catalogue
    // changes. Enforced only on a full-catalogue run; an explicit stem filter
    // runs a requested subset.
    if (stems.empty()) {
        if (g_compared != kExpectedNativeAgreed) {
            std::cerr << "FAIL: agreed count " << g_compared << " != pinned "
                      << kExpectedNativeAgreed
                      << " (a case moved between the compared and skipped sets?)\n";
            ++g_failures;
        }
        if (g_skipped != kExpectedNativeSkipped) {
            std::cerr << "FAIL: skipped count " << g_skipped << " != pinned "
                      << kExpectedNativeSkipped
                      << " (a case moved between the compared and skipped sets?)\n";
            ++g_failures;
        }
        std::sort(g_node_only_stems.begin(), g_node_only_stems.end());
        g_node_only_stems.erase(
            std::unique(g_node_only_stems.begin(), g_node_only_stems.end()),
            g_node_only_stems.end());
        std::vector<std::string> expected_stems;
        expected_stems.reserve(kExpectedNodeOnlyStems.size());
        for (const std::string_view s : kExpectedNodeOnlyStems) {
            expected_stems.emplace_back(s);
        }
        if (g_node_only_stems != expected_stems) {
            std::cerr << "FAIL: node-only (evaluator_surface_awaits_kr68) stem set is {";
            for (std::size_t i = 0; i < g_node_only_stems.size(); ++i) {
                std::cerr << (i ? ", " : "") << g_node_only_stems[i];
            }
            std::cerr << "} but the pinned set is {";
            for (std::size_t i = 0; i < expected_stems.size(); ++i) {
                std::cerr << (i ? ", " : "") << expected_stems[i];
            }
            std::cerr << "} (an un-reviewed case moved onto the node-only lane?)\n";
            ++g_failures;
        }
    }
    return g_failures == 0 ? 0 : 1;
}

// Blessing-determinism: run every selected scenario twice through the native
// engine and require byte-identical observations. A non-deterministic engine
// (wall time, pointer addresses, unordered iteration) would diverge here.
int mode_blessing_determinism(const fs::path &repo_root, const fs::path &cases_dir,
                               const std::unordered_set<std::string> &stems) {
    const auto cases = discover_cases(repo_root, cases_dir, stems);
    if (g_failures != 0) {
        return 1;
    }
    check(!cases.empty(), "at least one case selected");

    for (const auto &entry : cases) {
        const std::string stem = case_stem(entry.sidecar);
        for (const auto &scenario : entry.loaded.manifest.scenarios) {
            const std::string label = stem + "/" + scenario.name;
            const auto first = run_native_scenario(entry.loaded, scenario);
            const auto second = run_native_scenario(entry.loaded, scenario);
            check(first.ok, "first native run succeeds: " + label);
            check(second.ok, "second native run succeeds: " + label);
            if (!first.ok || !second.ok) {
                continue;
            }
            check(first.observation_json == second.observation_json,
                  "native observation is deterministic across two runs: " + label);
        }
    }
    if (g_failures == 0) {
        std::cout << "native observation is deterministic\n";
    }
    return g_failures == 0 ? 0 : 1;
}

// Mutation: tamper with the native observation of one case and prove the
// comparator REJECTS it (catches a mutated expectation).
int mode_mutation(const fs::path &repo_root, const fs::path &cases_dir,
                  const std::string &target_stem) {
    const auto cases = discover_cases(repo_root, cases_dir, {target_stem});
    if (g_failures != 0 || cases.size() != 1) {
        std::cerr << "ERROR: mutation mode needs exactly one case '" << target_stem << "'\n";
        return 1;
    }
    const auto &entry = cases.front();
    check(!entry.loaded.manifest.scenarios.empty(), "mutation case has a scenario");
    const auto &scenario = entry.loaded.manifest.scenarios.front();

    const NativeScenarioResult pristine = run_native_scenario(entry.loaded, scenario);
    check(pristine.ok, "pristine native run succeeds");
    if (!pristine.ok) {
        std::cerr << "  error: " << pristine.error << "\n";
        return 1;
    }

    const bool node_only =
        entry.loaded.manifest.engines.wasm.node_observation_skip ==
        WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68;

    if (node_only) {
        // Node-only lane: the comparator under test is
        // node_observation_matches_expectation. Exercise its status /
        // state_sequence / output_json branches directly.
        // (N1) Flip the terminal status.
        {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            check(dom.has_value() && *dom && (*dom)->is_object(),
                  "node-only observation parses");
            auto *status = (*dom)->get_mut("status");
            check(status != nullptr && status->as_string().has_value(),
                  "node-only observation carries a status");
            if (status == nullptr || !status->as_string().has_value()) {
                return 1;
            }
            status->string_val =
                status->string_val == "failed" ? "completed" : "failed";
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                node_observation_matches_expectation(scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on a deliberately mutated status");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N2) Corrupt a state_sequence element.
        {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            auto *states = (*dom)->get_mut("state_sequence");
            check(states != nullptr && states->is_array() && !states->array_items.empty(),
                  "node-only observation carries a non-empty state_sequence");
            if (states == nullptr || !states->is_array() || states->array_items.empty()) {
                return 1;
            }
            auto *entry0 = states->array_items.front()->get_mut("state");
            check(entry0 != nullptr && entry0->as_string().has_value(),
                  "state_sequence[0] carries a state name");
            if (entry0 == nullptr || !entry0->as_string().has_value()) {
                return 1;
            }
            entry0->string_val = "mutated-unexpected-state";
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                node_observation_matches_expectation(scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on a deliberately mutated state_sequence element");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N3) Corrupt the output JSON (only when the case blesses one).
        if (scenario.expect.output_json.has_value()) {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            auto *output = (*dom)->get_mut("output_json");
            check(output != nullptr && output->is_object(),
                  "node-only observation carries an output_json object");
            if (output == nullptr || !output->is_object()) {
                return 1;
            }
            bool tampered = false;
            for (auto &[key, value] : output->object_fields) {
                static_cast<void>(key);
                if (value->as_int().has_value()) {
                    value->int_val = *value->as_int() + 1;
                    tampered = true;
                    break;
                }
                if (value->as_string().has_value()) {
                    value->string_val = "mutated-unexpected-output";
                    tampered = true;
                    break;
                }
            }
            check(tampered, "the node-only output_json has a tamperable scalar field");
            if (!tampered) {
                return 1;
            }
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                node_observation_matches_expectation(scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on deliberately mutated output_json");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        return g_failures == 0 ? 0 : 1;
    }

    // Differential lane: the comparator under test is observations_agree.
    const auto evaluator = run_evaluator_scenario(entry.loaded, scenario);
    check(evaluator.ok, "evaluator reference re-runs for the mutation lane");
    if (!evaluator.ok) {
        return 1;
    }

    // (1) Flip the terminal status.
    {
        auto dom = ahfl::json::parse_json(pristine.observation_json);
        check(dom.has_value() && *dom && (*dom)->is_object(), "native observation parses");
        auto *status = (*dom)->get_mut("status");
        check(status != nullptr, "native observation carries a status");
        if (status == nullptr) {
            return 1;
        }
        status->string_val = status->string_val == "failed" ? "completed" : "failed";
        const std::string mutated = ahfl::json::serialize_json(**dom);
        const auto divergence =
            observations_agree(evaluator.observation_json, mutated);
        check(divergence.has_value(), "comparator FAILS on the deliberately mutated status");
        if (divergence.has_value()) {
            std::cout << "OK: comparator detected mutated expectation: " << *divergence << "\n";
        }
    }

    // (2) Corrupt a state_sequence element (a dimension other than status).
    {
        auto dom = ahfl::json::parse_json(pristine.observation_json);
        check(dom.has_value() && *dom && (*dom)->is_object(), "native observation re-parses");
        auto *states = (*dom)->get_mut("state_sequence");
        check(states != nullptr && states->is_array() && !states->array_items.empty(),
              "native observation carries a non-empty state_sequence");
        if (states == nullptr || !states->is_array() || states->array_items.empty()) {
            return 1;
        }
        auto *entry0 = states->array_items.front()->get_mut("state");
        check(entry0 != nullptr && entry0->as_string().has_value(),
              "state_sequence[0] carries a state name");
        if (entry0 == nullptr || !entry0->as_string().has_value()) {
            return 1;
        }
        entry0->string_val = "mutated-unexpected-state";
        const std::string mutated = ahfl::json::serialize_json(**dom);
        const auto divergence =
            observations_agree(evaluator.observation_json, mutated);
        check(divergence.has_value(),
              "comparator FAILS on a deliberately mutated state_sequence element");
        if (divergence.has_value()) {
            std::cout << "OK: comparator detected mutated expectation: " << *divergence << "\n";
        }
    }

    // (3) Corrupt a capability_arguments envelope (a dimension other than
    // status / state_sequence). Only runs when the case actually records a
    // capability call; a capability-free case has nothing to tamper.
    {
        auto dom = ahfl::json::parse_json(pristine.observation_json);
        check(dom.has_value() && *dom && (*dom)->is_object(),
              "native observation re-parses for capability_arguments tamper");
        if (!dom.has_value() || !*dom || !(*dom)->is_object()) {
            return 1;
        }
        auto *cap_args = (*dom)->get_mut("capability_arguments");
        if (cap_args != nullptr && cap_args->is_array() &&
            !cap_args->array_items.empty()) {
            // Tamper the first envelope: re-serialize it with a spurious field
            // so the canonical wire bytes change without touching the
            // capability_sequence dimension.
            auto &first_env = cap_args->array_items.front();
            check(first_env != nullptr && first_env->is_object(),
                  "capability_arguments[0] is an object envelope");
            if (first_env == nullptr || !first_env->is_object()) {
                return 1;
            }
            first_env->set("__mutation__",
                           ahfl::json::JsonValue::make_string("mutated"));
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                observations_agree(evaluator.observation_json, mutated);
            check(divergence.has_value(),
                  "comparator FAILS on a deliberately mutated capability_arguments envelope");
            if (divergence.has_value()) {
                std::cout << "OK: comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }
    }

    // (4) Corrupt a capability_sequence element (the ordered canonical-name
    // dimension). Guarded like (3): a capability-free case has no element to
    // rename.
    {
        auto dom = ahfl::json::parse_json(pristine.observation_json);
        check(dom.has_value() && *dom && (*dom)->is_object(),
              "native observation re-parses for capability_sequence tamper");
        if (!dom.has_value() || !*dom || !(*dom)->is_object()) {
            return 1;
        }
        auto *cap_seq = (*dom)->get_mut("capability_sequence");
        if (cap_seq != nullptr && cap_seq->is_array() &&
            !cap_seq->array_items.empty()) {
            auto &first_name = cap_seq->array_items.front();
            check(first_name != nullptr && first_name->as_string().has_value(),
                  "capability_sequence[0] carries a canonical capability name");
            if (first_name == nullptr || !first_name->as_string().has_value()) {
                return 1;
            }
            first_name->string_val = "mutated::unexpected::capability";
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                observations_agree(evaluator.observation_json, mutated);
            check(divergence.has_value(),
                  "comparator FAILS on a deliberately mutated capability_sequence element");
            if (divergence.has_value()) {
                std::cout << "OK: comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }
    }

    return g_failures == 0 ? 0 : 1;
}

[[nodiscard]] std::unordered_set<std::string> stem_set(char **begin, char **end) {
    std::unordered_set<std::string> stems;
    for (char **it = begin; it != end; ++it) {
        stems.emplace(*it);
    }
    return stems;
}

void usage(const char *argv0) {
    std::cerr << "usage:\n"
              << "  " << argv0 << " verify <repo-root> <cases-dir> [stem ...]\n"
              << "  " << argv0 << " blessing-determinism <repo-root> <cases-dir> [stem ...]\n"
              << "  " << argv0 << " mutation <repo-root> <cases-dir> <stem>\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        usage(argv[0]);
        return 2;
    }
    const std::string mode{argv[1]};
    const fs::path repo_root{argv[2]};
    const fs::path cases_dir{argv[3]};

    if (mode == "verify") {
        return mode_verify(repo_root, cases_dir, stem_set(argv + 4, argv + argc));
    }
    if (mode == "blessing-determinism") {
        return mode_blessing_determinism(repo_root, cases_dir,
                                         stem_set(argv + 4, argv + argc));
    }
    if (mode == "mutation" && argc == 5) {
        return mode_mutation(repo_root, cases_dir, std::string{argv[4]});
    }
    usage(argv[0]);
    return 2;
}
