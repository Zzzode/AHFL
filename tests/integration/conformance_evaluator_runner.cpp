// KR6.7 (RFC 0026 P7): generic in-process evaluator conformance runner.
//
// One binary drives the evaluator engine adapter over the committed case
// catalogue. It replaced the three bespoke end-to-end drivers
// (e2e_workflow / enum_variant_e2e / if_let_e2e), which each hand-compiled
// their fixture, hand-registered mocks, and hand-asserted struct fields.
//
// Modes:
//   verify <repo-root> <cases-dir> <observations-dir> [stem ...]
//       Runs every evaluator-enabled scenario of the selected cases (all of
//       them when no stem is given) and byte-compares the canonical
//       observation against the blessed <observations-dir>/<stem>.<scenario>.json.
//
//   determinism <repo-root> <cases-dir> [stem ...]
//       Runs every selected scenario twice and requires byte-identical
//       observations.
//
//   mutation <repo-root> <cases-dir> <observations-dir> <scratch-dir> <stem>
//       Copies one blessed observation into the scratch dir, mutates its
//       terminal status, and proves the verify gate detects the divergence
//       (the engine's actual observation matches the pristine blessing but
//       not the mutated copy).
//
//   bless <repo-root> <cases-dir> <observations-dir> [stem ...]
//       Regenerates every blessed observation from the engine. A blessing is
//       reviewed and checked in by a human; verify is the CI gate.
//
// Exit status: 0 success, 1 conformance failure, 2 usage / infrastructure
// error.

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/json/json_value.hpp"
#include "conformance/conformance_case.hpp"
#include "conformance/evaluator_engine.hpp"

namespace {

namespace fs = std::filesystem;
using ahfl::conformance::ConformanceCase;
using ahfl::conformance::ConformanceScenario;
using ahfl::conformance::EvaluatorScenarioResult;
using ahfl::conformance::LoadedConformanceCase;
using ahfl::conformance::detail::is_conformance_case_sidecar;
using ahfl::conformance::load_conformance_case;
using ahfl::conformance::run_evaluator_scenario;
namespace json = ahfl::json;

int g_failures = 0;
// When true, failure diagnostics are suppressed (the mutation lane runs the
// verify gate deliberately against a tampered blessing and only inspects its
// exit status).
bool g_quiet = false;

void check(bool condition, std::string_view name) {
    if (!condition) {
        ++g_failures;
        if (!g_quiet) {
            std::cerr << "FAIL: " << name << "\n";
        }
    }
}

struct LoadedCase {
    fs::path sidecar_path;
    LoadedConformanceCase loaded;
};

[[nodiscard]] std::vector<LoadedCase>
discover_cases(const fs::path &repo_root,
               const fs::path &cases_dir,
               const std::unordered_set<std::string> &stem_filter) {
    std::vector<fs::path> sidecars;
    std::error_code ec;
    for (const auto &entry : fs::directory_iterator(cases_dir, ec)) {
        if (entry.is_regular_file() && is_conformance_case_sidecar(entry.path())) {
            sidecars.push_back(entry.path());
        }
    }
    if (ec) {
        std::cerr << "ERROR: cannot scan cases directory " << cases_dir << "\n";
        return {};
    }
    std::sort(sidecars.begin(), sidecars.end());

    std::vector<LoadedCase> cases;
    for (const auto &sidecar : sidecars) {
        const std::string stem = sidecar.filename().string();
        const auto suffix = std::string{ahfl::conformance::detail::kConformanceCaseFileSuffix};
        const std::string stem_without_suffix =
            stem.size() >= suffix.size() ? stem.substr(0, stem.size() - suffix.size()) : stem;
        if (!stem_filter.empty() && !stem_filter.contains(stem_without_suffix)) {
            continue;
        }
        auto result = load_conformance_case(sidecar, repo_root);
        if (result.has_errors() || !result.conformance_case.has_value()) {
            std::cerr << "ERROR: failed to load case " << sidecar << "\n";
            result.diagnostics.render(std::cerr);
            ++g_failures;
            continue;
        }
        if (!result.conformance_case->manifest.engines.evaluator) {
            continue;
        }
        cases.push_back(LoadedCase{.sidecar_path = sidecar,
                                   .loaded = std::move(*result.conformance_case)});
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

bool write_file(const fs::path &path, std::string_view contents) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(out);
}

// Resolves the blessing path for one scenario. The scenario name is validated
// to the path-safe charset [A-Za-z0-9_-]+ by the manifest parser; the extra
// lexical-normalization check below is defense in depth so a future parser
// regression can never let a manifest data field select a write target outside
// `observations_dir` (path traversal). Returns nullopt when the joined path
// escapes the directory.
[[nodiscard]] std::optional<fs::path> observation_path(const fs::path &observations_dir,
                                                       std::string_view stem,
                                                       const ConformanceScenario &scenario) {
    const fs::path candidate =
        observations_dir / (std::string{stem} + "." + scenario.name + ".json");
    const fs::path base = observations_dir.lexically_normal();
    const fs::path normalized = candidate.lexically_normal();
    const auto base_str = base.generic_string();
    const auto candidate_str = normalized.generic_string();
    if (candidate_str != base_str && !candidate_str.starts_with(base_str + "/")) {
        return std::nullopt;
    }
    return normalized;
}

[[nodiscard]] std::string case_stem(const LoadedCase &candidate) {
    auto name = candidate.sidecar_path.filename().string();
    const auto suffix = std::string{ahfl::conformance::detail::kConformanceCaseFileSuffix};
    if (name.size() >= suffix.size()) {
        name.resize(name.size() - suffix.size());
    }
    return name;
}

// Runs one scenario, cross-checks the observation against the manifest's
// declared expectations, and returns the canonical observation (or logs the
// failure). The expectation gate runs in every lane so the manifest's
// `expect` block is load-bearing independently of the byte blessing.
[[nodiscard]] std::optional<std::string>
observe(const LoadedCase &candidate, const ConformanceScenario &scenario) {
    const EvaluatorScenarioResult result = run_evaluator_scenario(candidate.loaded, scenario);
    if (!result.ok) {
        std::cerr << "ERROR: engine run failed for " << candidate.loaded.manifest.source << " / "
                  << scenario.name << ":\n"
                  << result.error << "\n";
        ++g_failures;
        return std::nullopt;
    }
    const auto divergence = observation_matches_expectations(
        candidate.loaded.manifest, scenario, result.observation_json);
    if (divergence.has_value()) {
        std::cerr << "FAIL: manifest expectation mismatch in "
                  << case_stem(candidate) << "/" << scenario.name << ": " << *divergence << "\n";
        ++g_failures;
        return std::nullopt;
    }
    return result.observation_json;
}

// Runs the engine for one scenario and byte-compares it against the blessing
// on disk. Returns:
//   * nullopt when the blessing file is missing or the engine failed to run
//     (g_failures already records the run failure);
//   * true when the bytes match;
//   * false when they diverge (the two documents are printed only when
//     `print_divergence` is set — verify mode wants the diagnostic, the
//     mutation lane treats divergence as the expected verdict).
[[nodiscard]] std::optional<bool>
verify_observation(const LoadedCase &candidate,
                   const ConformanceScenario &scenario,
                   const fs::path &blessed_path,
                   std::string_view label,
                   bool print_divergence = true) {
    const auto observed = observe(candidate, scenario);
    if (!observed.has_value()) {
        return std::nullopt;
    }
    const auto blessed = read_file(blessed_path);
    if (!blessed.has_value()) {
        return std::nullopt;
    }
    if (*observed != *blessed) {
        if (print_divergence && !g_quiet) {
            std::cerr << "observation divergence for " << label << "\n--- observed ---\n"
                      << *observed << "\n--- blessed ---\n" << *blessed << "\n";
        }
        return false;
    }
    return true;
}

int mode_verify(const fs::path &repo_root,
                const fs::path &cases_dir,
                const fs::path &observations_dir,
                const std::unordered_set<std::string> &stem_filter) {
    const auto cases = discover_cases(repo_root, cases_dir, stem_filter);
    if (g_failures != 0) {
        return 1;
    }
    check(!cases.empty(), "at least one evaluator case selected");

    for (const auto &candidate : cases) {
        const std::string stem = case_stem(candidate);
        for (const auto &scenario : candidate.loaded.manifest.scenarios) {
            const std::string label = stem + "/" + scenario.name;
            const auto blessed_path = observation_path(observations_dir, stem, scenario);
            if (!blessed_path.has_value()) {
                check(false, "scenario name is path-safe: " + label);
                continue;
            }
            const auto verdict = verify_observation(candidate, scenario, *blessed_path, label);
            if (!verdict.has_value()) {
                check(false, "blessed observation exists and runs: " + blessed_path->string());
                continue;
            }
            check(*verdict, "observation byte-matches blessing: " + label);
        }
    }
    return g_failures == 0 ? 0 : 1;
}

int mode_determinism(const fs::path &repo_root,
                     const fs::path &cases_dir,
                     const std::unordered_set<std::string> &stem_filter) {
    const auto cases = discover_cases(repo_root, cases_dir, stem_filter);
    if (g_failures != 0) {
        return 1;
    }
    check(!cases.empty(), "at least one evaluator case selected");

    for (const auto &candidate : cases) {
        const std::string stem = case_stem(candidate);
        for (const auto &scenario : candidate.loaded.manifest.scenarios) {
            const auto first = observe(candidate, scenario);
            const auto second = observe(candidate, scenario);
            if (!first.has_value() || !second.has_value()) {
                continue;
            }
            check(*first == *second,
                  "scenario is deterministic across two runs: " + stem + "/" + scenario.name);
        }
    }
    return g_failures == 0 ? 0 : 1;
}

// Mutates the "status" string of a canonical observation DOM in place while
// preserving the surrounding canonical bytes.
[[nodiscard]] std::optional<std::string> mutate_status(const std::string &observation) {
    auto dom = json::parse_json(observation);
    if (!dom.has_value() || !*dom || !(*dom)->is_object()) {
        return std::nullopt;
    }
    auto *status = (*dom)->get_mut("status");
    if (status == nullptr) {
        return std::nullopt;
    }
    status->string_val = status->string_val == "failed" ? "completed" : "failed";
    const std::string mutated = json::serialize_json(**dom);
    if (mutated.empty() || mutated == observation) {
        return std::nullopt;
    }
    return mutated;
}

int mode_bless(const fs::path &repo_root,
               const fs::path &cases_dir,
               const fs::path &observations_dir,
               const std::unordered_set<std::string> &stem_filter) {
    const auto cases = discover_cases(repo_root, cases_dir, stem_filter);
    if (g_failures != 0) {
        return 1;
    }
    check(!cases.empty(), "at least one evaluator case selected");

    std::error_code ec;
    fs::create_directories(observations_dir, ec);
    check(!ec, "observations directory created");

    for (const auto &candidate : cases) {
        const std::string stem = case_stem(candidate);
        for (const auto &scenario : candidate.loaded.manifest.scenarios) {
            const auto observed = observe(candidate, scenario);
            if (!observed.has_value()) {
                continue;
            }
            const auto path = observation_path(observations_dir, stem, scenario);
            if (!path.has_value()) {
                check(false, "scenario name is path-safe: " + stem + "/" + scenario.name);
                continue;
            }
            check(write_file(*path, *observed), "blessing written: " + path->string());
        }
    }
    return g_failures == 0 ? 0 : 1;
}

int mode_mutation(const fs::path &repo_root,
                  const fs::path &cases_dir,
                  const fs::path &observations_dir,
                  const fs::path &scratch_dir,
                  const std::string &target_stem) {
    const auto cases = discover_cases(repo_root, cases_dir, {target_stem});
    if (g_failures != 0 || cases.size() != 1) {
        std::cerr << "ERROR: mutation mode needs exactly one case '" << target_stem << "'\n";
        return 1;
    }
    const auto &candidate = cases.front();
    const std::string stem = case_stem(candidate);
    check(!candidate.loaded.manifest.scenarios.empty(), "mutation case has a scenario");
    const auto &scenario = candidate.loaded.manifest.scenarios.front();

    const auto blessed_path = observation_path(observations_dir, stem, scenario);
    check(blessed_path.has_value(), "mutation scenario name is path-safe");
    if (!blessed_path.has_value()) {
        return 1;
    }

    // The pristine blessing passes the shared verify comparison.
    const auto pristine_verdict =
        verify_observation(candidate, scenario, *blessed_path, stem + "/" + scenario.name);
    check(pristine_verdict.has_value(), "pristine blessing present and scenario runs");
    check(pristine_verdict.value_or(false), "pristine blessing byte-matches before mutation");

    const auto blessed = read_file(*blessed_path);
    if (!blessed.has_value()) {
        return 1;
    }
    const auto mutated = mutate_status(*blessed);
    check(mutated.has_value(), "blessing parsed and status mutated");
    if (!mutated.has_value()) {
        return 1;
    }
    check(*mutated != *blessed, "mutation actually changed the blessing bytes");

    // End-to-end proof the verify GATE rejects a mutated blessing: stage the
    // mutated document as the scratch observations directory, then run the
    // actual verify mode against it. It must exit non-zero (conformance
    // failure). The one divergence dump it prints is the expected evidence.
    std::error_code ec;
    fs::create_directories(scratch_dir, ec);
    check(!ec, "mutation scratch directory created");
    const auto mutated_path = observation_path(scratch_dir, stem, scenario);
    check(mutated_path.has_value(), "mutation scratch path is path-safe");
    if (!mutated_path.has_value()) {
        return 1;
    }
    check(write_file(*mutated_path, *mutated), "mutated blessing staged");

    // Save and restore the global failure counter so the deliberate gate
    // failure does not leak into this lane's own pass/fail accounting. The
    // nested verify run is quiet: the only assertion here is its exit status.
    const int prior_failures = g_failures;
    g_failures = 0;
    g_quiet = true;
    const int gate_status = mode_verify(repo_root, cases_dir, scratch_dir, {stem});
    const int gate_failures = g_failures;
    g_quiet = false;
    g_failures = prior_failures;
    check(gate_status == 1, "verify gate FAILS (exit 1) on the mutated blessing");
    check(gate_failures > 0, "verify gate recorded the mutated blessing divergence");

    fs::remove(*mutated_path, ec);

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
              << "  " << argv0
              << " verify <repo-root> <cases-dir> <observations-dir> [stem ...]\n"
              << "  " << argv0
              << " bless <repo-root> <cases-dir> <observations-dir> [stem ...]\n"
              << "  " << argv0 << " determinism <repo-root> <cases-dir> [stem ...]\n"
              << "  " << argv0
              << " mutation <repo-root> <cases-dir> <observations-dir> <scratch-dir> <stem>\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 4) {
        usage(argv[0]);
        return 2;
    }
    const std::string mode{argv[1]};

    if (mode == "verify" && argc >= 5) {
        return mode_verify(fs::path{argv[2]}, fs::path{argv[3]}, fs::path{argv[4]},
                           stem_set(argv + 5, argv + argc));
    }
    if (mode == "bless" && argc >= 5) {
        return mode_bless(fs::path{argv[2]}, fs::path{argv[3]}, fs::path{argv[4]},
                          stem_set(argv + 5, argv + argc));
    }
    if (mode == "determinism" && argc >= 4) {
        return mode_determinism(fs::path{argv[2]}, fs::path{argv[3]},
                                stem_set(argv + 4, argv + argc));
    }
    if (mode == "mutation" && argc == 7) {
        return mode_mutation(fs::path{argv[2]}, fs::path{argv[3]}, fs::path{argv[4]},
                             fs::path{argv[5]}, std::string{argv[6]});
    }

    usage(argv[0]);
    return 2;
}
