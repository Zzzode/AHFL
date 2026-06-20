#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::formal {

/// Structured source mapping parsed from AHFL_MAP comments.
struct SourceMapping {
    std::string smv_symbol;
    std::string description;
    std::string source_path;
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
};

/// A single variable assignment within a counterexample state.
struct CounterexampleAssignment {
    std::string variable;
    std::string value;
    std::optional<SourceMapping> mapping;
};

/// A single state in the counterexample trace.
struct CounterexampleState {
    std::string label; // e.g. "1.1", "1.2"
    std::vector<CounterexampleAssignment> assignments;
};

/// A fully parsed counterexample trace from NuSMV output.
struct CounterexampleTrace {
    std::string trace_description;
    std::string trace_type;
    std::string violated_spec;
    std::vector<CounterexampleState> states;
    std::optional<std::size_t> loop_start_index; // index into states where loop begins
};

/// Human-readable explanation of why verification failed.
struct ViolationExplanation {
    std::string summary;
    std::vector<std::string> steps;
    std::string violated_property;
};

/// Parse AHFL_MAP comments from SMV model text into structured mappings.
[[nodiscard]] std::unordered_map<std::string, SourceMapping>
parse_structured_symbol_mappings(std::string_view model);

/// Parse NuSMV checker output into a structured counterexample trace.
/// Returns nullopt if no counterexample found in the output.
[[nodiscard]] std::optional<CounterexampleTrace>
parse_counterexample_trace(std::string_view checker_output,
                           const std::unordered_map<std::string, SourceMapping> &mappings);

/// Generate a human-readable explanation from a counterexample trace.
[[nodiscard]] ViolationExplanation explain_counterexample(const CounterexampleTrace &trace);

} // namespace ahfl::formal
