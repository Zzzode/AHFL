#pragma once

// KR6.7 (RFC 0026 P7): tree-walking evaluator engine adapter for engine-
// independent conformance case manifests.
//
// `run_evaluator_scenario` takes one loaded conformance case + one of its
// scenarios and runs the standard parse -> resolve -> typecheck -> validate ->
// lower pipeline against the case source, builds a CapabilityRegistry from the
// manifest's mock table via register_function + value_from_json (the canonical
// native value path, NOT the CLI's string-wrapping LLM-tool seam), and drives
// the in-process engine (WorkflowRuntime for `kind: workflow`, AgentRuntime for
// `kind: agent`) with:
//   * an injected fixed monotonic clock, so observations never carry wall time;
//   * a state_entered hook capturing the declaration-order (agent, state)
//     sequence;
//   * a capability_invoked hook capturing the canonical capability-name
//     sequence;
//   * the scenario input decoded from canonical wire JSON.
//
// It returns one canonical OBSERVATION DOCUMENT, the exact JSON shape every
// future engine adapter (orchestration wasm, computation wasm) must emit:
//
//   {
//     "schema": "ahfl.evaluator-observation.v1",
//     "case":   <repo-relative source>,
//     "scenario": <scenario name>,
//     "status": "completed" | "suspended" | "failed",
//     "state_sequence": [{"agent": ..., "state": ...}, ...],
//     "capability_sequence": ["module::Cap", ...],
//     "output_json": <canonical wire value, absent when there is no output>
//   }
//
// Only the canonical observation bytes are returned (plus a human-readable
// error string on engine/setup failure); the runner on disk is responsible for
// blessing and byte comparison.

#include <filesystem>
#include <string>

#include "conformance/conformance_case.hpp"

namespace ahfl::conformance {

struct EvaluatorScenarioResult {
    /// True when the pipeline compiled and the engine ran (regardless of the
    /// observed run status). False on a compile/setup failure; `error` then
    /// carries the rendered diagnostics.
    bool ok{false};
    /// Canonical observation document bytes; populated iff `ok`.
    std::string observation_json;
    /// Human-readable failure detail; populated iff not `ok`.
    std::string error;
};

/// Compiles the case source and executes one scenario on the in-process
/// evaluator engine, producing a canonical observation document. The case is
/// run from `loaded.source_path`; case.source is recorded verbatim in the
/// observation.
[[nodiscard]] EvaluatorScenarioResult
run_evaluator_scenario(const LoadedConformanceCase &loaded,
                       const ConformanceScenario &scenario);

/// Cross-checks a canonical observation document against the manifest's
/// declared expectations: terminal status, canonical capability-name order,
/// the declaration-order state-name sequence (for agent cases), and the
/// canonical output wire bytes. This keeps the manifest `expect` load-bearing
/// independently of the byte-blessed document, so a blessing and a manifest
/// can never silently disagree. Returns an empty optional when they agree, or
/// a human-readable reason naming the first divergence.
[[nodiscard]] std::optional<std::string>
observation_matches_expectations(const ConformanceCase &manifest,
                                 const ConformanceScenario &scenario,
                                 std::string_view observation_json);

} // namespace ahfl::conformance
