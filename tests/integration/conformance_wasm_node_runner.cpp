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
#include <array>
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
// RFC 0026 P6-7 rung E: the two raw P4-D frame cases (p6_aggregate /
// p6_collection) now pack input, invoke runv, and encode the output frame with
// real differential agreement, so the census moved 14/7 -> 16/5. The fix-forward
// two-container overlap regression (p6_frame_two_containers) drives the generic
// Node pack/runv/encode walker through TWO same-typed input containers with
// distinct live elements and moved 16/5 -> 17/5. Frame-bridge v2 rung V2-C
// (v2c_multi_arg_bridge) lands the first direct agent that invokes a
// MULTI-ARGUMENT capability from a non-final handler through the additive
// (i32)->(i32,i32) control-block bridge: the host walks the dense P4-D spans
// into the multi-argument wire envelope, packs the aggregate result into the
// call site's disjoint placement, and a computed final materializes a String
// projected from that result through ctx, moving 17/5 -> 18/5. The V2-C
// fix-forward adds three regression agents and a per-call argument-envelope
// differential dimension (18/5 -> 21/5):
//   * v2c_bridge_chain: TWO computed non-final handlers chain behind a plain
//     goto and each invokes the same capability; the two dense call sites
//     share one import ordinal, so reachability must see the planned computed
//     successors and the host must resolve each site from its control-block
//     address (never ordinal -> single site);
//   * v2c_route_then_bridge: a producing pattern-arena match followed by an
//     ordered bridge statement OUTSIDE the match arms (route then call);
//   * v2c_single_enum_bridge: one tag-only-enum bridge argument pins the SSOT
//     single-argument envelope ({"value":{...}} for a non-Struct, never the
//     bare enum object) via the new capability_arguments comparison.
// Frame-bridge v2 rung V2-D RETURN (emission half 1) packages a workflow whose
// RETURN is a PROJECTED or CONSTRUCTED P4-D frame: the computed runner is
// re-emitted with its fixed regions/globals relocated onto per-instance
// I/C/scratch/O node blocks and a merged rodata Data region, the tuple runner
// walks the plain-goto chain and invokes the relocated computed-final handler,
// and the in-module scheduler materializes node inputs and the workflow return
// with zero-copy PtrLen shares. A host-packed INLINE entry frame is rewritten
// into module pointer-tree form before the entry runner, admitting an enum
// payload projection off `input`. This moves enum_variant_e2e and if_let_e2e
// (two scenarios) from the skip set to differential agreement: 21/5 -> 24/2.
// Frame-bridge v2 rung V2-D emission half 2 (scalar capability-result ->
// context stores + constructed SummaryInput) lands e2e_multi_agent (two
// scenarios): packaged runners now dispatch EVERY state through relocated
// handlers on a bounded fuel ladder (a computed-goto if-routing handler takes
// a real successor, an ordered multi-argument capability bridge fires inside
// the workflow module, and a projected tag-enum/Bool capability-result field
// stores into context under the P6 scalar-kind agreement rule), the scheduler
// zero-fills C_k and materializes a bare-forward non-entry node frame and the
// CONSTRUCTED SummaryInput node input word-by-word, and a fixed state-entry
// trace ring records the real runtime (runner, state) evidence the host reads.
// 24/2 -> 26/0: the blocked_kr66 skip set is empty.
// KR6.7 corpus widening FB-5 slice A: sixteen P6 expression/control-flow
// companions (the p6 expression-match family match_expr/guard/or/fallthrough/
// binding_payload/result_i64, the nested-if/cascade path companions
// cascade_high/nested_depth3_taken/nested_elseless[_taken]/nested_taken_high/
// elseless_taken/elseless_fallthrough, the nested-aggregate projection
// nested_projection, unwrap_some, and the implies boolean fixture) move from
// bespoke Node probe coverage to manifest-driven cases that AGREE under both
// engines with identity-final borrowed frames: 26/0 -> 42/0.
// FB-5 slice B: the five V2-A computed-final fixtures (computed_scalar,
// computed_aggregate, computed_enum with two scenarios, computed_if_let_return
// with three scenarios, computed_payload_enum with two scenarios) move from
// bespoke probe-only coverage to nine differential scenarios whose finals
// construct output frames (scalar materialization, nested aggregate pointer
// expansion, tag-only and payload-bearing enum ladders): 42/0 -> 51/0.
// FB-5 slice C: the V2-B String/computed-final agents (string_passthrough 1,
// computed_string 2, bounded_string 2 scenarios), the two bounded-list input
// arena agents (list_string_arena, list_nested_string_arena), and the
// bounded-String durable-resume capability workflow (e3_capability_workflow_
// resume) add eight differential scenarios: 51/0 -> 59/0. v2b_enum_string is
// excluded: the evaluator misroutes its String tuple-variant enum construction
// through the capability invoker while the wasm run is correct.
// FB-5 slice D: five node-only FB-1/FB-3 cases (fb1_direct_call,
// fb1_aggregate_direct_call, fb3_byvalue_capture, fb3_nested_activation,
// fb3_nested_lambda_flow) land on the evaluator_surface_awaits_kr68 lane:
// the modules emit and the Node observations are blessed directly, adding
// five compared scenarios without an evaluator reference: 59/0 -> 64/0.
// The two bounded-list recursion fixtures (fb2_bounded_recursion,
// fb3_bounded_collection) stay out: their wire_json-classified modules
// physically read the fixed P4-D input frame and only the bespoke
// hand-packing hosts can drive them; the generic descriptor-driven host has
// no frame_lane to pack from.
// FB-5 slice E: fb4_effect_clause_pure_body adds one node-only pure-fn
// scenario and fb4_cross_agent_capability_leak adds one full differential
// scenario (the capability-free PureAgent compiled from a two-agent module,
// proving zero imports): 64/0 -> 66/0. fb4_construct_only_closure (dormant
// declared import trips the host ABI normalization matrix) and
// fb4_effectful_fn (an in-fn capability call sends a constructed frame over
// the opaque lane, which only accepts the borrowed input bytes) stay on
// bespoke hosts.
constexpr int kExpectedAgreed = 68;
constexpr int kExpectedSkipped = 0;

// Pinned STEM SET (not merely a census) of cases allowed to declare
// engines.wasm.node_observation_skip='evaluator_surface_awaits_kr68' (the
// FB-3b node-only lane). A manifest edit that moves a comparable differential
// case onto the node-only lane (dropping its evaluator reference) while adding
// another comparable case would keep the 18/5 totals green; this exact-set pin
// catches that. The pin moves deliberately when the KR6.8 evaluator surface
// retires or a reviewed node-only case lands. Keep sorted; the runner compares
// the sorted observed set against it.
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
// Stems that actually ran the node-only (evaluator_surface_awaits_kr68) lane in
// this invocation; compared against kExpectedNodeOnlyStems on a full run.
std::vector<std::string> g_node_only_stems;

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

    // Produce first. A p6-frame module carries its P4-D core-layout section;
    // the Node host packs the canonical input into the fixed frame, drives
    // runv, and encodes the output frame, so it produces a comparable
    // observation just like every other emitted case below. Only cases that do
    // not emit at all (the KR6.6 blocked lane) skip before an engine reference
    // is needed.
    WasmProduceResult produced = produce_conformance_wasm(entry.loaded, scenario);
    if (!produced.ok) {
        // A case that does not emit here is a KR6.6/blocked skip, not a failure,
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
    // fail before running the Node host so neither outcome can mask it. The one
    // exception is the FB-3b node-only lane (EvaluatorSurfaceAwaitsKr68): the
    // module runs, but there is no evaluator reference, so the Node observation
    // is compared directly against the manifest's blessed expectation below.
    const bool node_only =
        declared_skip == WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68;
    if (declared_skip != WasmNodeObservationSkip::None && !node_only) {
        std::cerr << "FAIL: " << label
                  << " emits a comparable module but its manifest declares a "
                     "engines.wasm.node_observation_skip expectation; the skip did not "
                     "occur (stale declaration)\n";
        ++g_failures;
        return 1;
    }
    if (node_only && produced.skip != WasmProduceSkip::EvaluatorSurfaceAwaitsKr68) {
        std::cerr << "FAIL: " << label
                  << " declares the node-only KR6.8 lane but the producer did not return that "
                     "structured skip\n";
        ++g_failures;
        return 1;
    }

    // The evaluator observation is the differential reference, EXCEPT on the
    // FB-3b node-only lane (the surfaced pure-fn/closure construct has no
    // evaluator yet); there the manifest's blessed expectation is the reference.
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

    if (node_only) {
        // No evaluator reference exists: assert the Node observation against
        // the manifest's blessed expectation (status + state/capability/output).
        const auto divergence = ahfl::conformance::node_observation_matches_expectation(
            scenario.expect, *node_observation);
        if (divergence.has_value()) {
            std::cerr << "FAIL: node-only expectation mismatch for " << label << ":\n  "
                      << *divergence << "\n--- node ---\n" << *node_observation << "\n";
            ++g_failures;
            return 1;
        }
        ++g_compared;
        g_node_only_stems.push_back(stem);
        std::cout << "OK: " << label
                  << " Node embedded-engine observation matched the blessed manifest expectation "
                     "(node-only FB-3b lane; evaluator surface awaits KR6.8; NOT wasmtime)\n";
        return 0;
    }

    const auto divergence =
        ahfl::conformance::observations_agree(*evaluator_observation, *node_observation);
    if (divergence.has_value()) {
        std::cerr << "FAIL: evaluator-vs-Node differential for " << label << ":\n  " << *divergence
                  << "\n--- evaluator ---\n"
                  << *evaluator_observation << "\n--- node ---\n"
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
        // Exact-set pin of the node-only (evaluator_surface_awaits_kr68) stems:
        // a manifest cannot silently move a differential case onto the
        // evaluator-free lane (or add an un-reviewed node-only case) while
        // holding the 14/7 census steady.
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

    // FB-3b node-only lane (evaluator_surface_awaits_kr68): there is no
    // in-process evaluator reference, so the comparator under test is
    // node_observation_matches_expectation against the manifest's blessed
    // expectation. Exercise its status / state_sequence / output_json branches
    // directly; the differential observations_agree path below needs an
    // evaluator and is skipped for this lane.
    const bool node_only =
        entry.loaded.manifest.engines.wasm.node_observation_skip ==
        WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68;
    if (node_only) {
        // (N1) Flip the terminal status.
        {
            auto dom = ahfl::json::parse_json(*node_observation);
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
            const auto divergence = ahfl::conformance::node_observation_matches_expectation(
                scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on a deliberately mutated status");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N2) Corrupt a state_sequence element.
        {
            auto dom = ahfl::json::parse_json(*node_observation);
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
            const auto divergence = ahfl::conformance::node_observation_matches_expectation(
                scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on a deliberately mutated state_sequence element");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        // (N3) Corrupt the output JSON (only when the case blesses one).
        if (scenario.expect.output_json.has_value()) {
            auto dom = ahfl::json::parse_json(*node_observation);
            auto *output = (*dom)->get_mut("output_json");
            check(output != nullptr && output->is_object(),
                  "node-only observation carries an output_json object");
            if (output == nullptr || !output->is_object()) {
                return 1;
            }
            // Flip an integer field when present, else a string field; the
            // blessed expectation differs either way.
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
            const auto divergence = ahfl::conformance::node_observation_matches_expectation(
                scenario.expect, mutated);
            check(divergence.has_value(),
                  "node-only comparator FAILS on deliberately mutated output_json");
            if (divergence.has_value()) {
                std::cout << "OK: node-only comparator detected mutated expectation: "
                          << *divergence << "\n";
            }
        }

        return g_failures == 0 ? 0 : 1;
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
