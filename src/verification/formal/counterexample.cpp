#include "verification/formal/counterexample.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
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

[[nodiscard]] std::string_view ltrim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    return value;
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
    explanation.summary = "Verification failed: property " + spec_abbrev + " violated after " +
                          std::to_string(trace.states.size()) + " execution steps";

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
                const auto &desc = assignment.mapping.has_value() ? assignment.mapping->description
                                                                  : assignment.variable;
                changes.push_back(desc + " changed to " + assignment.value);
            } else if (prev_it->second != assignment.value) {
                // Value changed
                const auto &desc = assignment.mapping.has_value() ? assignment.mapping->description
                                                                  : assignment.variable;
                changes.push_back(desc + " changed from " + prev_it->second + " to " +
                                  assignment.value);
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

// ============================================================================
// h-12 (QW-3): 4-dim counterexample mapping helpers
// ============================================================================

namespace {

/// Strip the AHFL smv-mangling underscore-prefix convention and turn the
/// remaining `__` separators into `.` so downstream LSP/CLI consumers see
/// a normal AHFL qualified path.
///
/// Examples:
///   - `agent__MyAgent__state`          → "agent.MyAgent.state"
///   - `workflow__Pipe__node__n1__phase`→ "workflow.Pipe.node.n1.phase"
///   - `input__retry_count`             → "input.retry_count"
///   - `context__balance`               → "context.balance"
[[nodiscard]] std::string smv_symbol_to_logical_path(std::string_view smv_symbol) {
    std::string out;
    out.reserve(smv_symbol.size());
    for (std::size_t i = 0; i < smv_symbol.size(); ++i) {
        if (smv_symbol[i] == '_' && i + 1 < smv_symbol.size() && smv_symbol[i + 1] == '_') {
            out.push_back('.');
            ++i;
        } else {
            out.push_back(smv_symbol[i]);
        }
    }
    return out;
}

/// Infer the D1 role + owner_name from the mangled smv_symbol.
/// Returns (role, owner_name).  Unrecognized patterns get role = "other"
/// and owner_name = the full logical path.
[[nodiscard]] std::pair<std::string, std::string>
d1_analyze_symbol(std::string_view smv_symbol, std::string_view logical_path) {
    // agent__<AgentName>__state
    constexpr std::string_view agent_prefix = "agent__";
    constexpr std::string_view agent_suffix = "__state";
    if (smv_symbol.size() > agent_prefix.size() + agent_suffix.size() &&
        smv_symbol.substr(0, agent_prefix.size()) == agent_prefix &&
        smv_symbol.substr(smv_symbol.size() - agent_suffix.size()) == agent_suffix) {
        const auto agent_name =
            smv_symbol.substr(agent_prefix.size(),
                              smv_symbol.size() - agent_prefix.size() - agent_suffix.size());
        return {"agent_state", smv_symbol_to_logical_path(agent_name)};
    }
    // workflow__<WfName>__node__<Node>__phase
    constexpr std::string_view wf_prefix = "workflow__";
    constexpr std::string_view wf_suffix = "__phase";
    if (smv_symbol.size() > wf_prefix.size() + wf_suffix.size() &&
        smv_symbol.substr(0, wf_prefix.size()) == wf_prefix &&
        smv_symbol.substr(smv_symbol.size() - wf_suffix.size()) == wf_suffix) {
        // wf_inner = <WfName>__node__<Node>
        const auto wf_inner = smv_symbol.substr(
            wf_prefix.size(), smv_symbol.size() - wf_prefix.size() - wf_suffix.size());
        // Turn "Pipeline__node__dispatch" → "Pipeline.node.dispatch".
        return {"workflow_phase", smv_symbol_to_logical_path(wf_inner)};
    }
    return {"other", std::string(logical_path)};
}

/// Build a flat (variable → value) lookup for a given trace state.
[[nodiscard]] std::unordered_map<std::string, const CounterexampleAssignment *>
build_state_lookup(const CounterexampleState &state) {
    std::unordered_map<std::string, const CounterexampleAssignment *> out;
    for (const auto &a : state.assignments) {
        out.emplace(a.variable, &a);
    }
    return out;
}

/// D1: build the state transition source list for step `prev_idx → prev_idx+1`.
[[nodiscard]] std::vector<StateTransitionSource>
build_step_transitions(const CounterexampleState &prev, const CounterexampleState &curr) {
    const auto prev_lookup = build_state_lookup(prev);
    std::vector<StateTransitionSource> out;
    for (const auto &curr_a : curr.assignments) {
        const auto it = prev_lookup.find(curr_a.variable);
        if (it == prev_lookup.end()) {
            continue; // newly-appearing vars are not D1 material
        }
        if (it->second->value == curr_a.value) {
            continue;
        }
        // We only emit a D1 row when the symbol has an AHFL_MAP source
        // attached — otherwise we would generate a lot of low-value
        // "other" entries for the smv-model-internal `state` variable.
        if (!curr_a.mapping.has_value()) {
            continue;
        }
        const auto logical = smv_symbol_to_logical_path(curr_a.variable);
        const auto [role, owner] = d1_analyze_symbol(curr_a.variable, logical);
        StateTransitionSource row;
        row.role = role;
        row.owner_name = owner;
        row.value_from = it->second->value;
        row.value_to = curr_a.value;
        row.source_path = curr_a.mapping->source_path;
        row.begin_offset = curr_a.mapping->begin_offset;
        row.end_offset = curr_a.mapping->end_offset;
        out.push_back(std::move(row));
    }
    return out;
}

/// D2/D3 common: construct a ProjectedVariable from a single CE assignment
/// whose smv_symbol prefix matches.
[[nodiscard]] ProjectedVariable
make_projected(const CounterexampleAssignment &a, std::string initial_value = std::string{}) {
    ProjectedVariable p;
    p.logical_path = smv_symbol_to_logical_path(a.variable);
    p.value = a.value;
    p.initial_value = std::move(initial_value);
    if (a.mapping.has_value()) {
        p.source_path = a.mapping->source_path;
        p.begin_offset = a.mapping->begin_offset;
        p.end_offset = a.mapping->end_offset;
    }
    return p;
}

// ========================================================================
// h-12 D5 helpers
// ========================================================================

/// Parse a `__ahfl_transition__<wf>__<node>__<label>_fired` smv variable
/// name (or its `_guard` sibling).  On success returns
///   (logical_path, action_name, is_guard_or_fired).
/// Returns `std::nullopt` for names that don't match the pattern.
struct D5TransitionParts {
    std::string logical_path; // e.g. "workflow.Checkout.node.pay"
    std::string action_name;  // e.g. "authorize"
};
[[nodiscard]] std::optional<D5TransitionParts>
d5_parse_transition_name(std::string_view smv_symbol) {
    constexpr std::string_view prefix = "__ahfl_transition__";
    if (smv_symbol.size() <= prefix.size()) return std::nullopt;
    if (smv_symbol.substr(0, prefix.size()) != prefix) return std::nullopt;

    auto rest = smv_symbol.substr(prefix.size());
    // Drop trailing "_fired" or "_guard".
    std::string_view suffix_to_strip;
    if (rest.size() > 6 && rest.substr(rest.size() - 6) == "_fired") {
        suffix_to_strip = "_fired";
    } else if (rest.size() > 6 && rest.substr(rest.size() - 6) == "_guard") {
        suffix_to_strip = "_guard";
    } else {
        return std::nullopt;
    }
    rest = rest.substr(0, rest.size() - suffix_to_strip.size());

    // Expect at least `<wf>__<node>__<label>`.
    const auto first_sep = rest.find("__");
    if (first_sep == std::string_view::npos) return std::nullopt;
    const auto second_sep = rest.find("__", first_sep + 2);
    if (second_sep == std::string_view::npos) return std::nullopt;
    const auto last_sep = rest.rfind("__");
    if (last_sep == first_sep || last_sep == second_sep) return std::nullopt;

    // Everything up to the last `__` becomes the logical_path (with `__`→`.`
    // mangling), and the part after the last `__` is the action label.
    const auto path_view = rest.substr(0, last_sep);
    const auto action_view = rest.substr(last_sep + 2);
    if (path_view.empty() || action_view.empty()) return std::nullopt;

    D5TransitionParts out;
    // Use standard mangling; prefix with "workflow." for consistency with
    // the AHFL_MAP `workflow__Wf__node__N__phase` convention.
    out.logical_path = "workflow." + smv_symbol_to_logical_path(path_view);
    out.action_name = std::string(action_view);
    return out;
}

/// Return the boolean assignment value, or `fallback` when not parseable.
[[nodiscard]] bool parse_bool_value(std::string_view value, bool fallback = false) {
    const auto v = trim(value);
    if (v == "TRUE" || v == "true" || v == "1") return true;
    if (v == "FALSE" || v == "false" || v == "0") return false;
    return fallback;
}

/// Build a single step's ProjectedAction list (for step prev→curr).
/// Walks `curr`'s assignments, matches fired variables, and cross-references
/// matching `_guard` variables in the same state for guard_value.  Uses
/// AHFL_MAP metadata (if attached to either the `_fired` or the `_guard`
/// smv symbol) for the SourceRange.
[[nodiscard]] std::vector<ProjectedAction>
build_step_action_trace(const CounterexampleState &prev, const CounterexampleState &curr) {
    // Build curr lookup for guard cross-reference.
    std::unordered_map<std::string, const CounterexampleAssignment *> curr_lookup;
    for (const auto &a : curr.assignments) {
        curr_lookup.emplace(a.variable, &a);
    }
    // Also include prev assignments because guard may be emitted in either.
    for (const auto &a : prev.assignments) {
        curr_lookup.try_emplace(a.variable, &a);
    }

    std::vector<ProjectedAction> out;
    for (const auto &a : curr.assignments) {
        const auto parts = d5_parse_transition_name(a.variable);
        if (!parts.has_value()) continue;
        // Only _fired=TRUE entries count as "the action fired this step".
        // _guard siblings are handled via sibling lookup below — never
        // emit a ProjectedAction for a _guard-only match.
        if (!a.variable.ends_with("_fired")) continue;
        if (!parse_bool_value(a.value, false)) continue;

        ProjectedAction row;
        row.logical_path = parts->logical_path;
        row.action_name = parts->action_name;
        row.guard_value = false;

        // Look up the sibling _guard variable to populate guard_value.
        // Build the _guard sibling name from the _fired one.
        const auto sibling_guard = [&]() -> std::string {
            // strip "_fired" (len 6), append "_guard".
            const auto base = a.variable.substr(0, a.variable.size() - 6);
            return std::string(base) + "_guard";
        }();
        if (const auto git = curr_lookup.find(sibling_guard); git != curr_lookup.end()) {
            row.guard_value = parse_bool_value(git->second->value, false);
            if (git->second->mapping.has_value()) {
                row.source.source_path = git->second->mapping->source_path;
                row.source.begin_offset = git->second->mapping->begin_offset;
                row.source.end_offset = git->second->mapping->end_offset;
            }
        }
        if (row.source.empty() && a.mapping.has_value()) {
            row.source.source_path = a.mapping->source_path;
            row.source.begin_offset = a.mapping->begin_offset;
            row.source.end_offset = a.mapping->end_offset;
        }

        out.push_back(std::move(row));
    }
    return out;
}

// ========================================================================
// h-12 D6 helpers
// ========================================================================

/// Return a "step K" (1-based) label for the violating step.  For invariant
/// kinds we use the final trace state (the step whose state exhibits the
/// BAD_STATE); for ensures / custom we use the first step or the final
/// state; fall back to 1 when the trace is empty.
[[nodiscard]] std::size_t d6_pick_violating_step(const CounterexampleTrace &trace,
                                                 ViolatedContractKind kind) {
    const auto n = trace.states.size();
    if (n == 0) return 1;
    switch (kind) {
    case ViolatedContractKind::Invariant:
    case ViolatedContractKind::Unknown:
    default:
        // Last state is the violating one.
        return n;
    case ViolatedContractKind::Ensures:
        // Eventuality failed for all states — last is the final step.
        return n;
    case ViolatedContractKind::Custom:
        return n;
    }
}

/// Build an "X changed from A to B" style line covering state step-1→step.
/// Empty when nothing changed.
[[nodiscard]] std::string d6_var_change_line(const CounterexampleTrace &trace,
                                             std::size_t violating_step_1based) {
    const auto n = trace.states.size();
    if (violating_step_1based < 2 || violating_step_1based > n) return {};
    const auto &prev = trace.states[violating_step_1based - 2];
    const auto &curr = trace.states[violating_step_1based - 1];
    auto prev_lookup = build_state_lookup(prev);
    std::vector<std::string> changes;
    for (const auto &a : curr.assignments) {
        // Skip internal __ahfl_transition__ booleans to keep the line small.
        if (a.variable.starts_with("__ahfl_transition__")) continue;
        const auto it = prev_lookup.find(a.variable);
        if (it != prev_lookup.end() && it->second->value == a.value) continue;
        const auto &desc = a.mapping.has_value() ? a.mapping->description
                                                 : smv_symbol_to_logical_path(a.variable);
        const auto from =
            (it != prev_lookup.end()) ? it->second->value : std::string{"<unset>"};
        changes.push_back(desc + "=" + a.value + " (was " + from + ")");
    }
    if (changes.empty()) return {};
    std::string out = "State changes between step " +
                      std::to_string(violating_step_1based - 1) + " and step " +
                      std::to_string(violating_step_1based) + ": ";
    constexpr std::size_t k_max_changes = 3;
    for (std::size_t i = 0; i < std::min(changes.size(), k_max_changes); ++i) {
        if (i != 0) out += "; ";
        out += changes[i];
    }
    if (changes.size() > k_max_changes) {
        out += "; ... (+" + std::to_string(changes.size() - k_max_changes) + " more)";
    }
    return out;
}

/// Heuristic D6 summary.  Always returns a non-empty string (at least the
/// D4 description as a last resort).  Lines separated by '\n'; at most 4.
[[nodiscard]] std::string make_natural_summary(const CounterexampleTrace &trace,
                                               const ViolationExplanation &explanation) {
    const auto &contract = explanation.violated_contract;
    const auto step = d6_pick_violating_step(trace, contract.kind);
    const auto inv_name =
        contract.name.empty() ? std::string{"<unnamed>"} : contract.name;

    // Line 1 is kind-specific.
    std::vector<std::string> lines;
    switch (contract.kind) {
    case ViolatedContractKind::Invariant: {
        std::string line = "At step " + std::to_string(step) +
                           ", the invariant [" + inv_name +
                           "] was violated because the system reached a "
                           "disallowed state.";
        lines.push_back(std::move(line));
        // Line 2: if we have faulty_ctx_fields include a concrete bound
        // violation mentioning the first context variable.
        if (!trace.faulty_ctx_fields.empty()) {
            const auto &f = trace.faulty_ctx_fields.front();
            lines.push_back("Context variable " + f.logical_path + "=" + f.value +
                            " violates the expected bounds.");
        } else if (!trace.trigger_input.empty()) {
            const auto &t = trace.trigger_input.front();
            lines.push_back("Trigger input " + t.logical_path + "=" + t.value +
                            " contributed to the violation.");
        }
        break;
    }
    case ViolatedContractKind::Ensures: {
        std::string line = "At step " + std::to_string(step) +
                           " (final state), the post-condition [" + inv_name +
                           "] failed because the required goal state was "
                           "never reached.";
        lines.push_back(std::move(line));
        break;
    }
    case ViolatedContractKind::Custom: {
        lines.push_back("At step " + std::to_string(step) +
                        ", a custom LTL/CTL property failed.");
        if (!contract.description.empty()) {
            lines.push_back("Details: " + contract.description);
        }
        break;
    }
    case ViolatedContractKind::Unknown:
    default:
        lines.push_back("Verification failed at step " + std::to_string(step) + ".");
        break;
    }

    // Line 3/4: include the violated_contract description line if not already
    // present, then a state-change diff line.
    if (lines.size() < 4 && !contract.description.empty()) {
        // Only add if not roughly equal to what we already wrote.
        bool already = false;
        for (const auto &l : lines) {
            if (l.find(contract.description) != std::string::npos) already = true;
        }
        if (!already) lines.push_back(contract.description);
    }
    if (lines.size() < 4) {
        const auto diff = d6_var_change_line(trace, step);
        if (!diff.empty()) lines.push_back(diff);
    }

    // Clamp to 4 lines.
    if (lines.size() > 4) lines.resize(4);
    if (lines.empty()) {
        lines.push_back(contract.description.empty()
                            ? std::string{"Verification failed."}
                            : contract.description);
    }

    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) out.push_back('\n');
        out += lines[i];
    }
    return out;
}

} // namespace

ViolatedContractInfo classify_violated_contract(std::string_view violated_spec) {
    const auto trimmed = trim(violated_spec);
    ViolatedContractInfo info;
    info.raw_spec = std::string(trimmed);

    if (trimmed.empty()) {
        info.description = "backend did not report a violated specification";
        return info;
    }

    // never(BAD_STATE) / reachable(TARGET) — the two AHFL contract-level
    // patterns nuxmv_backend.cpp:221-230 emits directly.
    if (trimmed.size() > 6 && trimmed.substr(0, 6) == "never(") {
        info.kind = ViolatedContractKind::Invariant;
        const auto body = trimmed.substr(6, trimmed.size() - 7 /* drop trailing ')' */);
        info.description =
            std::string("system invariant violated: state ") + std::string(trim(body)) +
            " was reached but must never occur";
        return info;
    }
    if (trimmed.size() > 10 && trimmed.substr(0, 10) == "reachable(") {
        info.kind = ViolatedContractKind::Ensures;
        const auto body = trimmed.substr(10, trimmed.size() - 11);
        info.description =
            std::string("progress obligation violated: state ") + std::string(trim(body)) +
            " was required to be reachable but never is";
        return info;
    }

    // Broad LTL heuristics for user-authored raw properties.
    const auto starts_with_G = [&]() {
        // e.g. "G (...",  "G("
        auto rest = ltrim(trimmed);
        if (rest.empty() || rest[0] != 'G') return false;
        rest.remove_prefix(1);
        rest = ltrim(rest);
        return !rest.empty() && rest[0] == '(';
    };
    if (starts_with_G()) {
        info.kind = ViolatedContractKind::Invariant;
        info.description =
            std::string("safety (invariant) property violated: ") + abbreviate_spec(trimmed);
        return info;
    }
    auto rest = ltrim(trimmed);
    if (!rest.empty() && rest[0] == 'F') {
        info.kind = ViolatedContractKind::Ensures;
        info.description =
            std::string("progress (eventuality) property violated: ") + abbreviate_spec(trimmed);
        return info;
    }

    info.kind = ViolatedContractKind::Custom;
    info.description = std::string("custom LTL/CTL property violated: ") + abbreviate_spec(trimmed);
    return info;
}

void enhance_counterexample_mapping(CounterexampleTrace &trace,
                                    ViolationExplanation &explanation) {
    // D4 always runs, even on traces with 0 states (no transitions).
    explanation.violated_contract = classify_violated_contract(trace.violated_spec);

    const auto n = trace.states.size();

    // D1 transitions (n-1 entries, one per step).
    trace.state_transitions.clear();
    trace.state_transitions.reserve(n == 0 ? 0 : n - 1);
    for (std::size_t i = 1; i < n; ++i) {
        trace.state_transitions.push_back(
            build_step_transitions(trace.states[i - 1], trace.states[i]));
    }

    // D2 trigger_input: collect input__* from initial state.
    constexpr std::string_view input_prefix = "input__";
    trace.trigger_input.clear();
    if (!trace.states.empty()) {
        for (const auto &a : trace.states.front().assignments) {
            if (a.variable.size() > input_prefix.size() &&
                std::string_view(a.variable).substr(0, input_prefix.size()) == input_prefix) {
                trace.trigger_input.push_back(make_projected(a));
            }
        }
    }

    // D3 faulty_ctx_fields: collect context__* where initial != final.
    constexpr std::string_view ctx_prefix = "context__";
    trace.faulty_ctx_fields.clear();
    if (n >= 2) {
        const auto initial_lookup = build_state_lookup(trace.states.front());
        for (const auto &a : trace.states.back().assignments) {
            if (!(a.variable.size() > ctx_prefix.size() &&
                  std::string_view(a.variable).substr(0, ctx_prefix.size()) == ctx_prefix)) {
                continue;
            }
            const auto it = initial_lookup.find(a.variable);
            const std::string initial_val =
                (it != initial_lookup.end()) ? it->second->value : std::string{};
            if (initial_val.empty() || initial_val != a.value) {
                trace.faulty_ctx_fields.push_back(make_projected(a, initial_val));
            }
        }
    }

    // D5 action_trace: collect fired transitions per step (aligned with
    // state_transitions, i.e. n == 0 ? 0 : n - 1 entries).
    trace.action_trace.clear();
    trace.action_trace.reserve(n == 0 ? 0 : n - 1);
    for (std::size_t i = 1; i < n; ++i) {
        trace.action_trace.push_back(
            build_step_action_trace(trace.states[i - 1], trace.states[i]));
    }

    // D6 natural_language_summary: always runs (fallback for empty traces).
    explanation.natural_language_summary = make_natural_summary(trace, explanation);
}

} // namespace ahfl::formal
