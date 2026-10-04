#pragma once

// RFC 0026 KR6.8 WH-4: the observation emitter for the wasm3 embedded host.
//
// Produces the canonical `ahfl.node-observation.v1` document that the
// conformance comparator (observation_compare.hpp) diffs against the
// engine observation. The field set, key order, and value formatting are
// pinned EXACTLY to the JS oracle's `emitObservation` in
// tests/conformance/node_embedded_host.mjs:
//
//   1. Fields are built as (key, raw-json-value) pairs.
//   2. Keys are sorted lexicographically (the JS `fields.sort`).
//   3. The document is `{key:value,key:value,...}` with no whitespace.
//
// The emitter is a pure string assembler: every value is already in its
// canonical wire form when it arrives. Capability argument envelopes come
// from `serialize_args_for_wire_json` (the SSOT the JS `bridgeEnvelope`
// mirrors); output JSON comes from `value_to_json`. The emitter never parses
// or re-serializes a value -- it only escapes the string-typed fields
// (case/scenario/status/agent/state/capability names) and concatenates.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host {

// One (agent, state) entry in the state_sequence array.
struct StateEntry {
    std::string agent;
    std::string state;

    [[nodiscard]] friend bool operator==(const StateEntry &,
                                         const StateEntry &) noexcept = default;
};

// The observation data collected during a wasm3 run. Every string field is
// already in its canonical wire form:
//   * `capability_arguments`  -- serialize_args_for_wire_json output per call
//   * `output_json`           -- value_to_json output (nullopt = absent)
//   * `status`                -- "completed" / "suspended" / "failed"
//   * `case_name`/`scenario_name` -- source-level names (emitter escapes)
struct ObservationData {
    std::string case_name;
    std::string scenario_name;
    std::string status;
    std::vector<StateEntry> states;
    std::vector<std::string> capabilities;
    std::vector<std::string> capability_arguments;
    std::optional<std::string> output_json;
    std::uint32_t transition_count{0};
    std::optional<std::uint32_t> workflow_completed_count;
};

// Emit the `ahfl.node-observation.v1` document with the exact JS field set
// and sorted keys. Pure and deterministic: the same ObservationData always
// produces the same bytes.
[[nodiscard]] std::string emit_observation(const ObservationData &data);

} // namespace ahfl::runtime::wasm_host
