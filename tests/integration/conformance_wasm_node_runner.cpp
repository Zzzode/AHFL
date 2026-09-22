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
#include <string_view>
#include <sys/select.h>
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
using ahfl::conformance::WasmNodeObservationSkip;
using ahfl::conformance::WasmProduceResult;
using ahfl::conformance::WasmProduceSkip;
using ahfl::conformance::detail::is_conformance_case_sidecar;

// Pinned census of the committed catalogue's Node-observation outcomes. The
// skip set is manifest-declared per case AND counted here, so neither a
// codegen flag flip nor a manifest-only drift can silently move a case between
// the compared and skipped sets while ctest stays green.
constexpr int kExpectedAgreed = 13;
constexpr int kExpectedSkipped = 7;

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
// ...` with stdout AND stderr captured. exit 77 means the Node WebAssembly
// embedding is unavailable (environment skip).
[[nodiscard]] NodeRun run_node_host(const std::string &node,
                                    const fs::path &host_script,
                                    const fs::path &descriptor,
                                    const fs::path &module,
                                    const std::string &scenario,
                                    const fs::path &observation) {
    NodeRun summary;
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        summary.stderr_text = "pipe() failed";
        return summary;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        summary.stderr_text = "fork() failed";
        return summary;
    }

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
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
        [[maybe_unused]] ssize_t written = write(STDERR_FILENO, message, 24);
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);

    // Drain both streams concurrently so a verbose child can never block on a
    // full pipe while we read the other one exclusively.
    std::string captured_out;
    std::string captured_err;
    bool out_open = true;
    bool err_open = true;
    char buffer[4096];
    while (out_open || err_open) {
        fd_set readfds;
        FD_ZERO(&readfds);
        if (out_open) {
            FD_SET(out_pipe[0], &readfds);
        }
        if (err_open) {
            FD_SET(err_pipe[0], &readfds);
        }
        const int max_fd = std::max(out_pipe[0], err_pipe[0]);
        int ready = select(max_fd + 1, &readfds, nullptr, nullptr, nullptr);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            break;
        }
        if (out_open && FD_ISSET(out_pipe[0], &readfds)) {
            const ssize_t n = read(out_pipe[0], buffer, sizeof(buffer));
            if (n > 0) {
                captured_out.append(buffer, static_cast<std::size_t>(n));
            } else {
                out_open = false;
            }
        }
        if (err_open && FD_ISSET(err_pipe[0], &readfds)) {
            const ssize_t n = read(err_pipe[0], buffer, sizeof(buffer));
            if (n > 0) {
                captured_err.append(buffer, static_cast<std::size_t>(n));
            } else {
                err_open = false;
            }
        }
    }
    close(out_pipe[0]);
    close(err_pipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    summary.launched = true;
    summary.stdout_text = std::move(captured_out);
    summary.stderr_text = std::move(captured_err);
    if (WIFEXITED(status)) {
        summary.exit_status = WEXITSTATUS(status);
    } else {
        summary.exit_status = -1;
    }
    if (summary.exit_status == 127 &&
        (summary.stdout_text.find("AHFL_NODE_LAUNCH_FAILED") != std::string::npos ||
         summary.stderr_text.find("AHFL_NODE_LAUNCH_FAILED") != std::string::npos)) {
        summary.launched = false;
    }
    return summary;
}

// Extracts and validates the case stem from a sidecar filename. The stem is a
// directory-entry NAME (untrusted filesystem data, not a manifest field), and
// it is later concatenated into scratch paths, so it must satisfy the same
// path-safe charset as a manifest scenario name: rejects empty, overlong,
// path-separator, and "."/".." traversal stems.
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
    const WasmNodeObservationSkip declared_skip = manifest.engines.wasm.node_observation_skip;

    // Produce first: a raw P4-D frame (P6-7 output-frame gate) has no canonical
    // wire output to diff, so it skips before any engine reference is needed.
    WasmProduceResult produced = produce_conformance_wasm(entry.loaded, scenario);
    if (!produced.ok) {
        if (produced.skip == WasmProduceSkip::RawP6FrameAwaitsP67) {
            if (declared_skip != WasmNodeObservationSkip::RawP6FrameAwaitsP67) {
                std::cerr << "FAIL: " << label
                          << " projects a raw P6-FRAME (p6-7 skip) but its manifest does not "
                             "declare engines.wasm.node_observation_skip="
                             "'raw_p6_frame_awaits_p67'; an unpinned skip is forbidden\n";
                ++g_failures;
                return 1;
            }
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
            if (declared_skip != WasmNodeObservationSkip::BlockedOnKr66) {
                std::cerr << "FAIL: " << label
                          << " does not emit on the orchestration lane (" << produced.code
                          << ") but its manifest does not declare "
                             "engines.wasm.node_observation_skip='blocked_kr66'; "
                             "an unpinned skip is forbidden\n";
                ++g_failures;
                return 1;
            }
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

    // The module emitted. A manifest that declares a Node-observation skip for a
    // case that actually produces a comparable module is a stale skip claim --
    // fail before running the Node host so neither outcome can mask it.
    if (declared_skip != WasmNodeObservationSkip::None) {
        std::cerr << "FAIL: " << label
                  << " emits a comparable module but its manifest declares a "
                     "engines.wasm.node_observation_skip expectation; the skip did not "
                     "occur (stale declaration)\n";
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
                  << "\n--- stdout ---\n"
                  << node_run.stdout_text << "\n--- stderr ---\n"
                  << node_run.stderr_text << "\n";
        ++g_failures;
        return 1;
    }
    if (!node_run.stderr_text.empty()) {
        // A successful host is silent on stderr (its evidence goes to the
        // observation file); surface unexpected diagnostics before the diff.
        std::cerr << "NOTE: Node embedded host wrote to stderr for " << label << ":\n"
                  << node_run.stderr_text << "\n";
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
                  << *node_observation;
        if (!node_run.stderr_text.empty()) {
            std::cerr << "\n--- node stderr ---\n" << node_run.stderr_text;
        }
        std::cerr << "\n";
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
    // Exact-set census: the pin must move deliberately whenever the catalogue
    // changes. This catches the dangerous direction (a compared case silently
    // starts skipping) that the 0-compare tripwire alone misses. Enforced only
    // on a full-catalogue run; an explicit stem filter runs a requested subset.
    if (stems.empty()) {
        if (g_compared != kExpectedAgreed) {
            std::cerr << "FAIL: agreed count " << g_compared << " != pinned " << kExpectedAgreed
                      << " (a case moved between the compared and skipped sets?)\n";
            ++g_failures;
        }
        if (g_skipped != kExpectedSkipped) {
            std::cerr << "FAIL: skipped count " << g_skipped << " != pinned " << kExpectedSkipped
                      << " (a case moved between the compared and skipped sets?)\n";
            ++g_failures;
        }
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

    // Mutation matrix on the pristine observation: each dimension the
    // comparator is responsible for must independently fail closed.
    const auto evaluator = run_evaluator_scenario(entry.loaded, scenario);
    check(evaluator.ok, "evaluator reference re-runs for the mutation lane");
    if (!evaluator.ok) {
        return 1;
    }

    // (1) Flip the terminal status.
    {
        auto dom = ahfl::json::parse_json(*node_observation);
        check(dom.has_value() && *dom && (*dom)->is_object(), "node observation parses");
        auto *status = (*dom)->get_mut("status");
        check(status != nullptr, "node observation carries a status");
        if (status == nullptr) {
            return 1;
        }
        status->string_val = status->string_val == "failed" ? "completed" : "failed";
        const std::string mutated = ahfl::json::serialize_json(**dom);
        const auto divergence =
            ahfl::conformance::observations_agree(evaluator.observation_json, mutated);
        check(divergence.has_value(), "comparator FAILS on the deliberately mutated status");
        if (divergence.has_value()) {
            std::cout << "OK: comparator detected mutated expectation: " << *divergence << "\n";
        }
    }

    // (2) Corrupt a state_sequence element (a dimension other than status).
    {
        auto dom = ahfl::json::parse_json(*node_observation);
        check(dom.has_value() && *dom && (*dom)->is_object(), "node observation re-parses");
        auto *states = (*dom)->get_mut("state_sequence");
        check(states != nullptr && states->is_array() && !states->array_items.empty(),
              "node observation carries a non-empty state_sequence");
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
            ahfl::conformance::observations_agree(evaluator.observation_json, mutated);
        check(divergence.has_value(),
              "comparator FAILS on a deliberately mutated state_sequence element");
        if (divergence.has_value()) {
            std::cout << "OK: comparator detected mutated expectation: " << *divergence << "\n";
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
