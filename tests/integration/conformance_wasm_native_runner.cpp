// KR6.8 WH-5 (RFC 0026): native embedded-host conformance census runner.
//
// One binary drives, for every committed conformance case the wasm3 facade can
// execute:
//   1. the native wasm3-backed facade (WasmWorkflowRuntime / WasmAgentRunner);
//   2. the differential comparator on status + state_sequence +
//      capability_sequence + capability_arguments + output_json against the
//      checked-in blessing (or the manifest's blessed expectation for the
//      closure constructs that have no blessing).
//
// Unlike the Node differential lane (conformance_wasm_node_runner.cpp), this
// lane is PURE IN-PROCESS: it compiles the AHFL-IR program through the facade
// and drives the wasm3 engine directly, with no Node subprocess, no artifact
// staging, and no SKIP_RETURN_CODE 77. The facade compiles every workflow in
// the program to wasm in its constructor; a compile failure is a hard failure
// (the case must be orchestration-eligible), never a silent skip.
//
// Data-driven blessing existence replaces the retired
// evaluator-surface skip marker: a case with a checked-in blessing
// compares the native observation against it; a case without one (the closure
// constructs) compares against the manifest's blessed expectation.
//
// Modes:
//   verify <repo-root> <cases-dir> [stem ...]
//   blessing-determinism <repo-root> <cases-dir> [stem ...]
//   mutation <repo-root> <cases-dir> <stem>
//
// Exit: 0 agreement, 1 divergence/infra failure, 2 usage.

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "base/json/json_value.hpp"
#include "conformance/conformance_case.hpp"
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
// the wasm observation agrees with the blessing: 68/0 -> 69/0.
// WH-5c.4 (GAP 2): the construct-capability-final case
// (wh5c4_construct_cap_final) constructs the capability argument in-module
// and the wasm observation agrees with the blessing: 69/0 -> 70/0.
// WH-5c.4 P0-3: the opaque-upstream construct-terminal case
// (wh5c4_p03_opaque_upstream) carries BOTH a JSON_TO_P4D input crossing
// and a P4D_TO_JSON self-transcode on one node; the two-slot scheduler
// table keeps both, and the P6-frame opaque-node state collection
// (workflow_session.cpp collect_opaque_node_states) recovers the opaque
// producer's state transitions so the native observation matches the
// blessing: 70/0 -> 71/0.
// WH-5c.5: the GAP 4 stash-parity case (wh5c5_gap4_stash_parity) joins
// the agreed set (three-node identity+capability+identity workflow; the
// per-node output stash table makes every node output host-observable):
// 71/0 -> 72/0.
// WH-5c.7: the 9-shape rich wire-type matrix (Int/String/Decimal/Duration/
// Set/Map/Option/Unit/Float) round-trips through the P6 frame packer/reader
// and the computed-final + workflow-level materializers: 72/0 -> 73/0.
// KR6.6 P6 f64 arithmetic ladder: four scenarios across two cases
// (p6_f64_arith: add/sub/mul/div/neg; p6_f64_compare: f64.gt goto):
// 73/0 -> 77/0.
// RFC 0026 FB-3b fix-forward: two closure-capture cases
// (fb3_string_capture: String PtrLen + Int env-layout coupling;
// fb3_f64_capture: F64 env store/load + f64 functype params):
// 77/0 -> 79/0.
constexpr int kExpectedNativeAgreed = 79;
constexpr int kExpectedNativeSkipped = 0;

int g_failures = 0;
int g_compared = 0;
int g_skipped = 0;

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

[[nodiscard]] std::optional<std::string> read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Runs, compares, and returns the native observation for one scenario.
// Returns:
//   0 differential agreed (or expectation matched);
//   1 a failure (and records g_failures);
//   77 this scenario skips (blocked_kr66).
int run_one(const CaseEntry &entry, const ConformanceScenario &scenario,
            const fs::path &observations_dir) {
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

    // Drive the wasm3 facade directly in-process.
    const NativeScenarioResult native = run_native_scenario(entry.loaded, scenario);
    if (!native.ok) {
        std::cerr << "FAIL: native engine run failed for " << label << ":\n"
                  << native.error << "\n";
        ++g_failures;
        return 1;
    }

    // Data-driven blessing existence replaces the retired
    // evaluator-surface skip marker: a case with a checked-in blessing
    // compares the native observation against it; a case without one (the
    // closure constructs) compares against the manifest's blessed expectation.
    const fs::path blessing_path =
        observations_dir / (stem + "." + scenario.name + ".json");
    if (fs::exists(blessing_path)) {
        const auto blessing = read_file(blessing_path);
        if (!blessing.has_value()) {
            std::cerr << "FAIL: cannot open blessing " << blessing_path << "\n";
            ++g_failures;
            return 1;
        }
        const auto divergence = observations_agree(*blessing, native.observation_json);
        if (divergence.has_value()) {
            std::cerr << "FAIL: blessing-vs-native differential for " << label << ":\n  "
                      << *divergence << "\n--- blessing ---\n" << *blessing
                      << "\n--- native ---\n" << native.observation_json << "\n";
            ++g_failures;
            return 1;
        }
        ++g_compared;
        std::cout << "OK: " << label << " native observation agreed with blessing\n";
        return 0;
    }

    // No blessing: compare against the manifest's blessed expectation.
    const auto divergence =
        node_observation_matches_expectation(scenario.expect, native.observation_json);
    if (divergence.has_value()) {
        std::cerr << "FAIL: expectation mismatch for " << label << ":\n  "
                  << *divergence << "\n--- native ---\n" << native.observation_json << "\n";
        ++g_failures;
        return 1;
    }
    ++g_compared;
    std::cout << "OK: " << label
              << " native observation matched the blessed manifest expectation\n";
    return 0;
}

int mode_verify(const fs::path &repo_root, const fs::path &cases_dir,
                const std::unordered_set<std::string> &stems) {
    const auto cases = discover_cases(repo_root, cases_dir, stems);
    if (g_failures != 0) {
        return 1;
    }

    const fs::path observations_dir = cases_dir.parent_path() / "observations";

    for (const auto &entry : cases) {
        for (const auto &scenario : entry.loaded.manifest.scenarios) {
            run_one(entry, scenario, observations_dir);
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

    const fs::path observations_dir = cases_dir.parent_path() / "observations";
    const std::string stem = case_stem(entry.sidecar);
    const fs::path blessing_path =
        observations_dir / (stem + "." + scenario.name + ".json");
    const bool has_blessing = fs::exists(blessing_path);

    if (!has_blessing) {
        // Expectation lane: the comparator under test is
        // node_observation_matches_expectation. Exercise its status /
        // state_sequence / output_json branches directly.
        // (N1) Flip the terminal status.
        {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            check(dom.has_value() && *dom && (*dom)->is_object(),
                  "expectation-lane observation parses");
            auto *status = (*dom)->get_mut("status");
            check(status != nullptr && status->as_string().has_value(),
                  "expectation-lane observation carries a status");
            if (status == nullptr || !status->as_string().has_value()) {
                return 1;
            }
            status->string_val =
                status->string_val == "failed" ? "completed" : "failed";
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                node_observation_matches_expectation(scenario.expect, mutated);
            check(divergence.has_value(),
                  "expectation-lane comparator FAILS on a deliberately mutated status");
            if (divergence.has_value()) {
                std::cout << "OK: expectation-lane comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N2) Corrupt a state_sequence element.
        {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            auto *states = (*dom)->get_mut("state_sequence");
            check(states != nullptr && states->is_array() && !states->array_items.empty(),
                  "expectation-lane observation carries a non-empty state_sequence");
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
                  "expectation-lane comparator FAILS on a deliberately mutated state_sequence element");
            if (divergence.has_value()) {
                std::cout << "OK: expectation-lane comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N3) Corrupt the output JSON (only when the case blesses one).
        if (scenario.expect.output_json.has_value()) {
            auto dom = ahfl::json::parse_json(pristine.observation_json);
            auto *output = (*dom)->get_mut("output_json");
            check(output != nullptr && output->is_object(),
                  "expectation-lane observation carries an output_json object");
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
            check(tampered, "the expectation-lane output_json has a tamperable scalar field");
            if (!tampered) {
                return 1;
            }
            const std::string mutated = ahfl::json::serialize_json(**dom);
            const auto divergence =
                node_observation_matches_expectation(scenario.expect, mutated);
            check(divergence.has_value(),
                  "expectation-lane comparator FAILS on deliberately mutated output_json");
            if (divergence.has_value()) {
                std::cout << "OK: expectation-lane comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        return g_failures == 0 ? 0 : 1;
    }

    // Blessing lane: the comparator under test is observations_agree.
    const auto blessing = read_file(blessing_path);
    check(blessing.has_value(), "blessing loads for the mutation lane");
    if (!blessing.has_value()) {
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
            observations_agree(*blessing, mutated);
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
            observations_agree(*blessing, mutated);
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
                observations_agree(*blessing, mutated);
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
                observations_agree(*blessing, mutated);
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
