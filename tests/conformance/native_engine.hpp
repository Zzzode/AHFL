#pragma once

// KR6.8 WH-5 (RFC 0026): native embedded-host conformance census.
//
// `run_native_scenario` drives the wasm3-backed facade
// (WasmWorkflowRuntime / WasmAgentRunner) directly in-process and produces
// the canonical observation document (ahfl.evaluator-observation.v1,
// historical schema name), so the differential comparator
// (observation_compare.hpp) can diff blessing-vs-native without a Node
// subprocess.
//
// This is the NATIVE lane: it compiles the AHFL-IR program through the facade
// (which links the wasm backend), drives the wasm3 engine, and records the
// same observation dimensions (status, state_sequence, capability_sequence,
// capability_arguments, output_json) the comparator consumes. There is no
// SKIP_RETURN_CODE 77: the lane is pure in-process and never depends on an
// external embedding.

#include <string>

#include "conformance/conformance_case.hpp"

namespace ahfl::conformance {

struct NativeScenarioResult {
    bool ok{false};
    std::string observation_json;
    std::string error;
};

/// Drives one scenario through the wasm3-backed facade and returns the
/// canonical observation document. Fails (ok=false) when the facade cannot
/// compile or run the case; the error string carries the diagnostic.
[[nodiscard]] NativeScenarioResult
run_native_scenario(const LoadedConformanceCase &loaded,
                    const ConformanceScenario &scenario);

} // namespace ahfl::conformance
