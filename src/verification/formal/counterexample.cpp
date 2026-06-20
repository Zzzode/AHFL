#include "verification/formal/counterexample.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <string_view>

namespace ahfl::formal {
namespace {

[[nodiscard]] std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] std::string trim_to_string(std::string_view value) {
    return std::string(trim(value));
}

/// Split text into lines, handling both \n and \r\n.
[[nodiscard]] std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        if (newline == std::string_view::npos) {
            if (!text.empty()) {
                lines.push_back(text);
            }
            break;
        }
        auto line = text.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        text.remove_prefix(newline + 1);
    }
    return lines;
}

/// Parse the source location portion: "path:begin-end" with possible trailing text.
/// Returns true if successfully parsed.
[[nodiscard]] bool parse_source_location(std::string_view location,
                                         std::string &out_path,
                                         std::size_t &out_begin,
                                         std::size_t &out_end) {
    // Find the last colon to separate path from "begin-end [trailing]"
    const auto last_colon = location.rfind(':');
    if (last_colon == std::string_view::npos || last_colon == 0) {
        return false;
    }

    const auto path_part = location.substr(0, last_colon);
    const auto offset_part = location.substr(last_colon + 1);

    // Parse begin offset (sequence of digits)
    std::size_t begin_val = 0;
    auto [ptr_begin, ec_begin] =
        std::from_chars(offset_part.data(), offset_part.data() + offset_part.size(), begin_val);
    if (ec_begin != std::errc{}) {
        return false;
    }

    // Expect a dash after begin
    if (ptr_begin >= offset_part.data() + offset_part.size() || *ptr_begin != '-') {
        return false;
    }
    ++ptr_begin;

    // Parse end offset
    std::size_t end_val = 0;
    auto [ptr_end, ec_end] =
        std::from_chars(ptr_begin, offset_part.data() + offset_part.size(), end_val);
    if (ec_end != std::errc{}) {
        return false;
    }

    out_path = std::string(trim(path_part));
    out_begin = begin_val;
    out_end = end_val;
    return true;
}

/// Abbreviate a specification string for summary display.
[[nodiscard]] std::string abbreviate_spec(std::string_view spec, std::size_t max_length = 60) {
    const auto trimmed = trim(spec);
    if (trimmed.size() <= max_length) {
        return std::string(trimmed);
    }
    return std::string(trimmed.substr(0, max_length - 3)) + "...";
}

} // namespace

std::unordered_map<std::string, SourceMapping>
parse_structured_symbol_mappings(std::string_view model) {
    constexpr std::string_view prefix = "-- AHFL_MAP ";
    constexpr std::string_view separator = " => ";
    constexpr std::string_view at_separator = " @ ";

    std::unordered_map<std::string, SourceMapping> mappings;

    for (const auto line : split_lines(model)) {
        if (!line.starts_with(prefix)) {
            continue;
        }

        const auto payload = line.substr(prefix.size());
        const auto separator_pos = payload.find(separator);
        if (separator_pos == std::string_view::npos) {
            continue;
        }

        SourceMapping mapping;
        mapping.smv_symbol = trim_to_string(payload.substr(0, separator_pos));

        const auto after_separator = payload.substr(separator_pos + separator.size());

        // Find the last " @ " to separate description from source location.
        const auto at_pos = after_separator.rfind(at_separator);
        if (at_pos == std::string_view::npos) {
            // No source location, description only.
            mapping.description = trim_to_string(after_separator);
        } else {
            mapping.description = trim_to_string(after_separator.substr(0, at_pos));
            const auto location_part = after_separator.substr(at_pos + at_separator.size());

            // Try to parse source location. If it fails, treat entire payload as description.
            if (!parse_source_location(
                    location_part, mapping.source_path, mapping.begin_offset, mapping.end_offset)) {
                // Fallback: include the @ part in the description.
                mapping.description = trim_to_string(after_separator);
            }
        }

        mappings.emplace(mapping.smv_symbol, std::move(mapping));
    }

    return mappings;
}

std::optional<CounterexampleTrace>
parse_counterexample_trace(std::string_view checker_output,
                           const std::unordered_map<std::string, SourceMapping> &mappings) {
    const auto lines = split_lines(checker_output);

    CounterexampleTrace trace;
    bool found_counterexample = false;
    bool loop_mark_pending = false;

    for (const auto line : lines) {
        const auto trimmed = trim(line);

        // Extract violated specification: line containing " is false"
        if (const auto is_false_pos = trimmed.find(" is false");
            is_false_pos != std::string_view::npos && trace.violated_spec.empty()) {
            // Look for "specification" keyword to extract the spec text
            constexpr std::string_view spec_prefix = "specification";
            const auto spec_start = trimmed.find(spec_prefix);
            if (spec_start != std::string_view::npos) {
                const auto after_spec = trimmed.substr(spec_start + spec_prefix.size());
                const auto spec_end = after_spec.find(" is false");
                if (spec_end != std::string_view::npos) {
                    trace.violated_spec = trim_to_string(after_spec.substr(0, spec_end));
                }
            }
            found_counterexample = true;
        }

        // Extract trace description
        if (constexpr std::string_view td_prefix = "Trace Description:";
            trimmed.starts_with(td_prefix)) {
            trace.trace_description = trim_to_string(trimmed.substr(td_prefix.size()));
            found_counterexample = true;
        }

        // Extract trace type
        if (constexpr std::string_view tt_prefix = "Trace Type:"; trimmed.starts_with(tt_prefix)) {
            trace.trace_type = trim_to_string(trimmed.substr(tt_prefix.size()));
            found_counterexample = true;
        }

        // Detect loop marker
        if (trimmed.find("Loop starts here") != std::string_view::npos) {
            loop_mark_pending = true;
            continue;
        }

        // Parse state markers: "-> State: X.Y <-"
        if (trimmed.starts_with("-> State:") && trimmed.ends_with("<-")) {
            found_counterexample = true;
            auto label_part = trimmed.substr(9); // skip "-> State:"
            label_part.remove_suffix(2);         // remove "<-"
            auto label = trim_to_string(label_part);

            if (loop_mark_pending) {
                trace.loop_start_index = trace.states.size();
                loop_mark_pending = false;
            }

            trace.states.push_back(CounterexampleState{
                .label = std::move(label),
                .assignments = {},
            });
            continue;
        }

        // Parse assignments within a state (lines with " = " pattern)
        if (!trace.states.empty() && !trimmed.empty() && !trimmed.starts_with("--")) {
            const auto eq_pos = trimmed.find(" = ");
            if (eq_pos != std::string_view::npos) {
                auto variable = trim_to_string(trimmed.substr(0, eq_pos));
                auto value = trim_to_string(trimmed.substr(eq_pos + 3));

                std::optional<SourceMapping> mapping;
                if (const auto it = mappings.find(variable); it != mappings.end()) {
                    mapping = it->second;
                }

                trace.states.back().assignments.push_back(CounterexampleAssignment{
                    .variable = std::move(variable),
                    .value = std::move(value),
                    .mapping = std::move(mapping),
                });
            }
        }
    }

    if (!found_counterexample) {
        return std::nullopt;
    }

    return trace;
}

ViolationExplanation explain_counterexample(const CounterexampleTrace &trace) {
    ViolationExplanation explanation;
    explanation.violated_property = trace.violated_spec;

    // Generate summary
    const auto spec_abbrev = abbreviate_spec(trace.violated_spec);
    explanation.summary = "Verification failed: property " + spec_abbrev +
                          " violated after " + std::to_string(trace.states.size()) +
                          " execution steps";

    // Generate steps
    for (std::size_t i = 0; i < trace.states.size(); ++i) {
        const auto &state = trace.states[i];

        if (i == 0) {
            // First state: list initial values
            std::string step = "Step 1 (State " + state.label + "): initial state";
            bool has_values = false;
            for (const auto &assignment : state.assignments) {
                const auto &desc = assignment.mapping.has_value() ? assignment.mapping->description
                                                                  : assignment.variable;
                if (has_values) {
                    step += ", ";
                } else {
                    step += " — ";
                    has_values = true;
                }
                step += desc + " = " + assignment.value;
            }
            explanation.steps.push_back(std::move(step));
            continue;
        }

        // Subsequent states: find changed variables
        const auto &prev_state = trace.states[i - 1];

        // Build lookup of previous state values
        std::unordered_map<std::string, std::string> prev_values;
        for (const auto &assignment : prev_state.assignments) {
            prev_values[assignment.variable] = assignment.value;
        }

        std::vector<std::string> changes;
        for (const auto &assignment : state.assignments) {
            const auto prev_it = prev_values.find(assignment.variable);
            if (prev_it == prev_values.end()) {
                // New variable appeared — treat as a change from unknown
                const auto &desc = assignment.mapping.has_value()
                                       ? assignment.mapping->description
                                       : assignment.variable;
                changes.push_back(desc + " changed to " + assignment.value);
            } else if (prev_it->second != assignment.value) {
                // Value changed
                const auto &desc = assignment.mapping.has_value()
                                       ? assignment.mapping->description
                                       : assignment.variable;
                changes.push_back(desc + " changed from " + prev_it->second + " to " + assignment.value);
            }
        }

        const auto step_num = std::to_string(i + 1);
        std::string step = "Step " + step_num + " (State " + state.label + "): ";
        if (changes.empty()) {
            step += "no change";
        } else {
            for (std::size_t c = 0; c < changes.size(); ++c) {
                if (c > 0) {
                    step += "; ";
                }
                step += changes[c];
            }
        }
        explanation.steps.push_back(std::move(step));
    }

    // If a loop was detected, add a note
    if (trace.loop_start_index.has_value()) {
        const auto loop_idx = *trace.loop_start_index;
        std::string loop_note = "Note: an infinite loop starts from State ";
        if (loop_idx < trace.states.size()) {
            loop_note += trace.states[loop_idx].label;
        } else {
            loop_note += std::to_string(loop_idx + 1);
        }
        loop_note += "; the system will never satisfy this property";
        explanation.steps.push_back(std::move(loop_note));
    }

    return explanation;
}

} // namespace ahfl::formal
