// KR6.7 (RFC 0026 P7): manifest-driven Node embedded-engine differential runner.
//
// One binary drives, for every committed conformance case the compiler emits on
// the orchestration wasm lane:
//   1. the in-process evaluator adapter (canonical evaluator observation);
//   2. the manifest-driven Core-Wasm module producer (module + descriptor);
//   3. the generic Node embedded host (canonical node observation);
//   4. the differential comparator on status + state_sequence +
//      capability_sequence + output_json.
//
// Cases the emit path rejects, or whose handler projects the raw P4-D input
// frame (P6-7 output-frame gate), are SKIPPED with a structured code
// (kr6.6 / p6-7 / the compiler diagnostic) -- never faked. When the Node
// embedding is unavailable the whole lane SKIPs (77) so ctest reports it
// visibly.
//
// Modes:
//   verify   <repo-root> <cases-dir> <scratch-dir> [stem ...]
//   mutation <repo-root> <cases-dir> <scratch-dir> <stem>
//
// Exit: 0 agreement, 1 divergence/infra failure, 2 usage, 77 nothing could run
// (Node absent).

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

#include "base/json/json_value.hpp"
#include "conformance/conformance_case.hpp"
#include "conformance/evaluator_engine.hpp"
#include "conformance/observation_compare.hpp"
#include "conformance/wasm_engine.hpp"

namespace {

namespace fs = std::filesystem;
using ahfl::conformance::ConformanceScenario;
using ahfl::conformance::load_conformance_case;
using ahfl::conformance::LoadedConformanceCase;
using ahfl::conformance::produce_conformance_wasm;
using ahfl::conformance::run_evaluator_scenario;
using ahfl::conformance::WasmProduceResult;
using ahfl::conformance::WasmProduceSkip;
using ahfl::conformance::detail::is_conformance_case_sidecar;

int g_failures = 0;
int g_compared = 0;
int g_skipped = 0;

void check(bool condition, std::string_view name) {
    if (!condition) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

[[nodiscard]] std::optional<std::string> read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_file(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool write_text(const fs::path &path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

// Resolves the Node executable: AHFL_NODE override, else `node` on PATH. Returns
// an empty string when no embedding is available.
[[nodiscard]] std::string resolve_node() {
    if (const char *explicit_node = std::getenv("AHFL_NODE");
        explicit_node != nullptr && *explicit_node != '\0') {
        return explicit_node;
    }
    return "node";
}

struct NodeRun {
    int exit_status{1};
    std::string stdout_text;
    std::string stderr_text;
    bool launched{false};
};

// Runs `node <host.mjs> --descriptor ... --module ... --scenario ... --output
// ...` with stdout captured. exit 77 means the Node WebAssembly embedding is
// unavailable (environment skip).
[[nodiscard]] NodeRun run_node_host(const std::string &node,
                                    const fs::path &host_script,
                                    const fs::path &descriptor,
                                    const fs::path &module,
                                    const std::string &scenario,
                                    const fs::path &observation) {
    NodeRun summary;
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        summary.stderr_text = "pipe() failed";
        return summary;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        summary.stderr_text = "fork() failed";
        return summary;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        const std::string descriptor_arg = descriptor.string();
        const std::string module_arg = module.string();
        const std::string observation_arg = observation.string();
        // --descriptor / --module / --scenario / --output
        execlp(node.c_str(),
               node.c_str(),
               host_script.string().c_str(),
               "--descriptor",
               descriptor_arg.c_str(),
               "--module",
               module_arg.c_str(),
               "--scenario",
               scenario.c_str(),
               "--output",
               observation_arg.c_str(),
               static_cast<char *>(nullptr));
        const char *message = "AHFL_NODE_LAUNCH_FAILED\n";
        [[maybe_unused]] ssize_t written = write(STDOUT_FILENO, message, 24);
        _exit(127);
    }

    close(pipefd[1]);
    std::string captured;
    char buffer[4096];
    while (true) {
        const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
        if (n > 0) {
            captured.append(buffer, static_cast<std::size_t>(n));
        } else if (n == 0) {
            break;
        } else if (errno != EINTR) {
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    summary.launched = true;
    summary.stdout_text = std::move(captured);
    if (WIFEXITED(status)) {
        summary.exit_status = WEXITSTATUS(status);
    } else {
        summary.exit_status = -1;
    }
    if (summary.exit_status == 127 &&
        summary.stdout_text.find("AHFL_NODE_LAUNCH_FAILED") != std::string::npos) {
        summary.launched = false;
    }
    return summary;
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

[[nodiscard]] std::vector<CaseEntry> discover_cases(const fs::path &repo_root,
                                                    const fs::path &cases_dir,
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

// Produces, runs, and compares one scenario. Returns:
//   0 differential agreed;
//   77 this scenario skips (blocked / p6-7);
//   1 a failure (and records g_failures).
int run_one(const CaseEntry &entry,
            const ConformanceScenario &scenario,
            const std::string &node,
            const fs::path &host_script,
            const fs::path &scratch_dir) {
    const auto &manifest = entry.loaded.manifest;
    const std::string stem = case_stem(entry.sidecar);
    const std::string label = stem + "/" + scenario.name;

    // Produce first: a raw P4-D frame (P6-7 output-frame gate) has no canonical
    // wire output to diff, so it skips before any engine reference is needed.
    WasmProduceResult produced = produce_conformance_wasm(entry.loaded, scenario);
    if (!produced.ok) {
        if (produced.skip == WasmProduceSkip::RawP6FrameAwaitsP67) {
            ++g_skipped;
            std::cout << "SKIP[77] " << label << ": " << produced.code << " -- " << produced.reason
                      << "\n";
            return 77;
        }
        // The eligibility classifier cross-checks the manifest lane already; a
        // case that does not emit here is a KR6.6/blocked skip, not a failure,
        // when the manifest itself declares a non-orchestration lane.
        if (manifest.engines.wasm.eligibility !=
            ahfl::conformance::WasmEligibility::Orchestration) {
            ++g_skipped;
            std::cout << "SKIP[77] " << label << ": kr6.6 (" << produced.code << ") -- "
                      << produced.reason << "\n";
            return 77;
        }
        std::cerr << "FAIL: orchestration-declared case did not emit for " << label << ": "
                  << produced.code << ": " << produced.reason << "\n";
        ++g_failures;
        return 1;
    }

    // The evaluator observation is the differential reference.
    const auto evaluator = run_evaluator_scenario(entry.loaded, scenario);
    if (!evaluator.ok) {
        std::cerr << "ERROR: evaluator run failed for " << label << ":\n"
                  << evaluator.error << "\n";
        ++g_failures;
        return 1;
    }

    const fs::path case_scratch = scratch_dir / stem;
    const fs::path module_path = case_scratch / (scenario.name + ".wasm");
    const fs::path descriptor_path = case_scratch / (scenario.name + ".descriptor.json");
    const fs::path observation_path = case_scratch / (scenario.name + ".node.json");
    if (!write_file(module_path, produced.artifact_bytes) ||
        !write_text(descriptor_path, produced.descriptor_json)) {
        std::cerr << "ERROR: failed to stage module/descriptor for " << label << "\n";
        ++g_failures;
        return 1;
    }

    const NodeRun node_run = run_node_host(
        node, host_script, descriptor_path, module_path, scenario.name, observation_path);
    if (!node_run.launched) {
        // Node itself is absent: the whole lane skips via the caller.
        std::cout << "SKIP[77] " << label
                  << ": Node embedded engine is unavailable (set AHFL_NODE)\n";
        return 78; // distinct "no node" code handled by the caller
    }
    if (node_run.exit_status == 77) {
        std::cout << "SKIP[77] " << label << ": Node WebAssembly embedding unavailable\n";
        return 78;
    }
    if (node_run.exit_status != 0) {
        std::cerr << "FAIL: Node embedded host exited " << node_run.exit_status << " for " << label
                  << "\n"
                  << node_run.stdout_text << "\n";
        ++g_failures;
        return 1;
    }

    const auto node_observation = read_file(observation_path);
    if (!node_observation.has_value()) {
        std::cerr << "FAIL: Node host wrote no observation for " << label << "\n";
        ++g_failures;
        return 1;
    }

    const auto divergence =
        ahfl::conformance::observations_agree(evaluator.observation_json, *node_observation);
    if (divergence.has_value()) {
        std::cerr << "FAIL: evaluator-vs-Node differential for " << label << ":\n  " << *divergence
                  << "\n--- evaluator ---\n"
                  << evaluator.observation_json << "\n--- node ---\n"
                  << *node_observation << "\n";
        ++g_failures;
        return 1;
    }

    ++g_compared;
    std::cout << "OK: " << label
              << " Node embedded-engine differential agreed (Node embedded-engine "
                 "evidence, NOT wasmtime evidence)\n";
    return 0;
}

int mode_verify(const fs::path &repo_root,
                const fs::path &cases_dir,
                const fs::path &scratch_dir,
                const std::unordered_set<std::string> &stems) {
    const std::string node = resolve_node();
    const fs::path host_script = repo_root / "tests" / "conformance" / "node_embedded_host.mjs";
    if (!fs::is_regular_file(host_script)) {
        std::cerr << "ERROR: missing Node embedded host script " << host_script << "\n";
        return 2;
    }

    const auto cases = discover_cases(repo_root, cases_dir, stems);
    if (g_failures != 0) {
        return 1;
    }

    bool node_seen = false;
    for (const auto &entry : cases) {
        for (const auto &scenario : entry.loaded.manifest.scenarios) {
            const int outcome = run_one(entry, scenario, node, host_script, scratch_dir);
            if (outcome == 78) {
                if (!node_seen) {
                    // Node is unavailable: emit the ctest-visible skip once.
                    std::cout << "SKIP: Node embedded engine is unavailable; "
                                 "the WASM differential lane did not run\n";
                    return 77;
                }
            }
            if (outcome != 77) {
                node_seen = true;
            }
        }
    }

    std::cout << "Node embedded-engine differential: " << g_compared << " agreed, " << g_skipped
              << " skipped\n";
    if (g_compared == 0 && g_failures == 0) {
        std::cout << "SKIP: no orchestration-eligible scenario was available\n";
        return 77;
    }
    return g_failures == 0 ? 0 : 1;
}

// Mutation: tamper with the Node observation of one case and prove the
// comparator REJECTS it (catches a mutated expectation).
int mode_mutation(const fs::path &repo_root,
                  const fs::path &cases_dir,
                  const fs::path &scratch_dir,
                  const std::string &target_stem) {
    const std::string node = resolve_node();
    const fs::path host_script = repo_root / "tests" / "conformance" / "node_embedded_host.mjs";
    const auto cases = discover_cases(repo_root, cases_dir, {target_stem});
    if (g_failures != 0 || cases.size() != 1) {
        std::cerr << "ERROR: mutation mode needs exactly one case '" << target_stem << "'\n";
        return 1;
    }
    const auto &entry = cases.front();
    check(!entry.loaded.manifest.scenarios.empty(), "mutation case has a scenario");
    const auto &scenario = entry.loaded.manifest.scenarios.front();

    const int pristine = run_one(entry, scenario, node, host_script, scratch_dir);
    if (pristine == 78) {
        std::cout << "SKIP: Node embedded engine is unavailable for the mutation lane\n";
        return 77;
    }
    check(pristine == 0, "pristine differential agrees before mutation");
    if (pristine != 0) {
        return 1;
    }

    const fs::path observation_path = scratch_dir / target_stem / (scenario.name + ".node.json");
    auto node_observation = read_file(observation_path);
    check(node_observation.has_value(), "pristine node observation is on disk");
    if (!node_observation.has_value()) {
        return 1;
    }

    // Flip the terminal status; the comparator must detect the divergence.
    auto dom = ahfl::json::parse_json(*node_observation);
    check(dom.has_value() && *dom && (*dom)->is_object(), "node observation parses");
    auto *status = (*dom)->get_mut("status");
    check(status != nullptr, "node observation carries a status");
    if (status == nullptr) {
        return 1;
    }
    status->string_val = status->string_val == "failed" ? "completed" : "failed";
    const std::string mutated = ahfl::json::serialize_json(**dom);

    const auto evaluator = run_evaluator_scenario(entry.loaded, scenario);
    check(evaluator.ok, "evaluator reference re-runs for the mutation lane");
    if (!evaluator.ok) {
        return 1;
    }
    const auto divergence =
        ahfl::conformance::observations_agree(evaluator.observation_json, mutated);
    check(divergence.has_value(), "comparator FAILS on the deliberately mutated status");
    if (divergence.has_value()) {
        std::cout << "OK: comparator detected mutated expectation: " << *divergence << "\n";
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
              << "  " << argv0 << " verify <repo-root> <cases-dir> <scratch-dir> [stem ...]\n"
              << "  " << argv0 << " mutation <repo-root> <cases-dir> <scratch-dir> <stem>\n";
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 2;
    }
    const std::string mode{argv[1]};
    const fs::path repo_root{argv[2]};
    const fs::path cases_dir{argv[3]};
    const fs::path scratch_dir{argv[4]};

    if (mode == "verify") {
        return mode_verify(repo_root, cases_dir, scratch_dir, stem_set(argv + 5, argv + argc));
    }
    if (mode == "mutation" && argc == 6) {
        return mode_mutation(repo_root, cases_dir, scratch_dir, std::string{argv[5]});
    }
    usage(argv[0]);
    return 2;
}
