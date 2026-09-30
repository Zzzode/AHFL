#pragma once

// KR6.7 (RFC 0026 P7) / KR6.8 WH-5: the shared canonical observation-document
// renderer for conformance engine adapters.
//
// Serializes one engine run into the canonical observation DOM that the
// differential comparator (observation_compare.hpp) consumes:
//
//   {"schema":"ahfl.evaluator-observation.v1","case":...,"scenario":...,
//    "status":"completed"|"suspended"|"failed",
//    "state_sequence":[{"agent":...,"state":...},...],
//    "capability_sequence":["module::Cap",...],
//    "capability_arguments":[<canonical wire envelope per call>,...],
//    "output_json":<canonical wire value, absent when no output>}
//
// Extracted from evaluator_engine.cpp (anonymous namespace) so the native
// engine adapter (native_engine.cpp) produces the SAME canonical bytes the
// evaluator engine produces, ensuring the comparator diffs apples to apples.

#include <string>
#include <utility>
#include <vector>

#include "conformance/conformance_case.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value.hpp"

namespace ahfl::conformance {

/// Maps a WorkflowStatus to the canonical observation status string.
[[nodiscard]] const char *status_name_workflow(ahfl::runtime::WorkflowStatus status);

/// Renders one engine run into the canonical observation document. `states`
/// are declaration-order (agent, state) pairs; `capabilities` the canonical
/// capability-name sequence; `argument_envelopes` the per-call canonical wire
/// argument envelope (serialize_args_for_wire_json SSOT) in the same order.
[[nodiscard]] std::string
render_observation(const ConformanceCase &manifest,
                   const ConformanceScenario &scenario, const char *status,
                   const std::vector<std::pair<std::string, std::string>> &states,
                   const std::vector<std::string> &capabilities,
                   const std::vector<std::string> &argument_envelopes,
                   const ahfl::runtime::Value *output);

} // namespace ahfl::conformance
