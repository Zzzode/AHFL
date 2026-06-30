#include "verification/formal/counterexample_json.hpp"

#include "base/support/json.hpp"

#include <sstream>

namespace ahfl::formal {

namespace {

const char *contract_kind_name(ViolatedContractKind kind) {
    switch (kind) {
    case ViolatedContractKind::Invariant:
        return "invariant";
    case ViolatedContractKind::Ensures:
        return "ensures";
    case ViolatedContractKind::Custom:
        return "custom";
    case ViolatedContractKind::Unknown:
        return "unknown";
    }
    return "unknown";
}

class CounterexampleJsonWriter : public ahfl::PrettyJsonWriter {
  public:
    explicit CounterexampleJsonWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    void write(const CounterexampleTrace &trace, const ViolationExplanation &explanation) {
        print_object(0, [&](auto field) {
            field("status", [&]() { write_string("failed"); });
            field("violated_specification", [&]() { write_string(trace.violated_spec); });
            field("explanation", [&]() { write_explanation(explanation, 1); });
            field("trace", [&]() { write_trace(trace, 1); });
            // ============== h-12 QW-3 4-dim fields ==============
            write_4dim_fields(trace, explanation, field);
        });
        out_ << '\n';
    }

  private:
    template <typename FieldFn>
    void write_4dim_fields(const CounterexampleTrace &trace,
                           const ViolationExplanation &explanation,
                           FieldFn &&field) {
        // D4: violated_contract
        field("violated_contract", [&]() { write_violated_contract(explanation, 1); });
        // D1: state_transitions (length = states.size() - 1 or 0)
        field("state_transitions", [&]() {
            print_array(1, [&](auto item) {
                for (const auto &step : trace.state_transitions) {
                    item([&]() {
                        print_array(2, [&](auto row_item) {
                            for (const auto &row : step) {
                                row_item([&]() { write_state_transition(row, 3); });
                            }
                        });
                    });
                }
            });
        });
        // D2: trigger_input
        field("trigger_input", [&]() {
            print_array(1, [&](auto item) {
                for (const auto &v : trace.trigger_input) {
                    item([&]() { write_projected_variable(v, 2, /*include_initial=*/false); });
                }
            });
        });
        // D3: faulty_ctx_fields
        field("faulty_ctx_fields", [&]() {
            print_array(1, [&](auto item) {
                for (const auto &v : trace.faulty_ctx_fields) {
                    item([&]() { write_projected_variable(v, 2, /*include_initial=*/true); });
                }
            });
        });
        // D5: action_trace (parallel to state_transitions)
        field("action_trace", [&]() {
            print_array(1, [&](auto step_item) {
                for (const auto &step : trace.action_trace) {
                    step_item([&]() {
                        print_array(2, [&](auto act_item) {
                            for (const auto &act : step) {
                                act_item([&]() { write_projected_action(act, 3); });
                            }
                        });
                    });
                }
            });
        });
    }

    void write_explanation(const ViolationExplanation &explanation, int indent) {
        print_object(indent, [&](auto field) {
            field("summary", [&]() { write_string(explanation.summary); });
            field("steps", [&]() {
                print_array(indent + 1, [&](auto item) {
                    for (const auto &step : explanation.steps) {
                        item([&]() { write_string(step); });
                    }
                });
            });
            field("violated_property", [&]() { write_string(explanation.violated_property); });
            // D6 natural-language expansion
            field("natural_language_summary",
                  [&]() { write_string(explanation.natural_language_summary); });
        });
    }

    void write_violated_contract(const ViolationExplanation &explanation, int indent) {
        print_object(indent, [&](auto field) {
            field("kind", [&]() { write_string(contract_kind_name(explanation.violated_contract.kind)); });
            field("description", [&]() { write_string(explanation.violated_contract.description); });
            field("raw_spec", [&]() { write_string(explanation.violated_contract.raw_spec); });
            if (!explanation.violated_contract.name.empty()) {
                field("name", [&]() { write_string(explanation.violated_contract.name); });
            }
        });
    }

    void write_trace(const CounterexampleTrace &trace, int indent) {
        print_object(indent, [&](auto field) {
            field("description", [&]() { write_string(trace.trace_description); });
            field("type", [&]() { write_string(trace.trace_type); });
            if (trace.loop_start_index.has_value()) {
                field("loop_start_index", [&]() { out_ << *trace.loop_start_index; });
            }
            field("states", [&]() {
                print_array(indent + 1, [&](auto item) {
                    for (const auto &state : trace.states) {
                        item([&]() { write_state(state, indent + 2); });
                    }
                });
            });
        });
    }

    void write_state(const CounterexampleState &state, int indent) {
        print_object(indent, [&](auto field) {
            field("label", [&]() { write_string(state.label); });
            field("assignments", [&]() {
                print_array(indent + 1, [&](auto item) {
                    for (const auto &assignment : state.assignments) {
                        item([&]() { write_assignment(assignment, indent + 2); });
                    }
                });
            });
        });
    }

    void write_assignment(const CounterexampleAssignment &assignment, int indent) {
        print_object(indent, [&](auto field) {
            field("variable", [&]() { write_string(assignment.variable); });
            field("value", [&]() { write_string(assignment.value); });
            if (assignment.mapping.has_value()) {
                field("description", [&]() { write_string(assignment.mapping->description); });
                if (!assignment.mapping->source_path.empty()) {
                    field("source",
                          [&]() { write_source_location(*assignment.mapping, indent + 1); });
                }
            }
        });
    }

    void write_source_location(const SourceMapping &mapping, int indent) {
        print_object(indent, [&](auto field) {
            field("path", [&]() { write_string(mapping.source_path); });
            field("begin", [&]() { out_ << mapping.begin_offset; });
            field("end", [&]() { out_ << mapping.end_offset; });
        });
    }

    void write_state_transition(const StateTransitionSource &row, int indent) {
        print_object(indent, [&](auto field) {
            field("role", [&]() { write_string(row.role); });
            field("owner", [&]() { write_string(row.owner_name); });
            field("from", [&]() { write_string(row.value_from); });
            field("to", [&]() { write_string(row.value_to); });
            if (!row.source_path.empty()) {
                field("source", [&]() {
                    print_object(indent + 1, [&](auto f) {
                        f("path", [&]() { write_string(row.source_path); });
                        f("begin", [&]() { out_ << row.begin_offset; });
                        f("end", [&]() { out_ << row.end_offset; });
                    });
                });
            }
        });
    }

    void write_projected_variable(const ProjectedVariable &v,
                                  int indent,
                                  bool include_initial) {
        print_object(indent, [&](auto field) {
            field("path", [&]() { write_string(v.logical_path); });
            field("value", [&]() { write_string(v.value); });
            if (include_initial) {
                field("initial_value", [&]() { write_string(v.initial_value); });
            }
            if (!v.source_path.empty()) {
                field("source", [&]() {
                    print_object(indent + 1, [&](auto f) {
                        f("path", [&]() { write_string(v.source_path); });
                        f("begin", [&]() { out_ << v.begin_offset; });
                        f("end", [&]() { out_ << v.end_offset; });
                    });
                });
            }
        });
    }

    void write_source_range(const SourceRange &r, int indent) {
        print_object(indent, [&](auto f) {
            f("path", [&]() { write_string(r.source_path); });
            f("begin", [&]() { out_ << r.begin_offset; });
            f("end", [&]() { out_ << r.end_offset; });
        });
    }

    void write_projected_action(const ProjectedAction &a, int indent) {
        print_object(indent, [&](auto field) {
            field("logical_path", [&]() { write_string(a.logical_path); });
            field("action_name", [&]() { write_string(a.action_name); });
            field("guard_value", [&]() { out_ << (a.guard_value ? "true" : "false"); });
            if (!a.source.empty()) {
                field("source", [&]() { write_source_range(a.source, indent + 1); });
            }
        });
    }
};

} // namespace

void write_counterexample_json(const CounterexampleTrace &trace,
                               const ViolationExplanation &explanation,
                               std::ostream &out) {
    CounterexampleJsonWriter writer(out);
    writer.write(trace, explanation);
}

std::string counterexample_to_json(const CounterexampleTrace &trace,
                                   const ViolationExplanation &explanation) {
    std::ostringstream oss;
    write_counterexample_json(trace, explanation, oss);
    return oss.str();
}

} // namespace ahfl::formal
