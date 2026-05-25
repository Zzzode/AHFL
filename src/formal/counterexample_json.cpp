#include "formal/counterexample_json.hpp"

#include "support/json.hpp"

#include <sstream>

namespace ahfl::formal {

namespace {

class CounterexampleJsonWriter : public ahfl::PrettyJsonWriter {
  public:
    explicit CounterexampleJsonWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    void write(const CounterexampleTrace &trace, const ViolationExplanation &explanation) {
        print_object(0, [&](auto field) {
            field("status", [&]() { write_string("failed"); });
            field("violated_specification", [&]() { write_string(trace.violated_spec); });
            field("explanation", [&]() { write_explanation(explanation, 1); });
            field("trace", [&]() { write_trace(trace, 1); });
        });
        out_ << '\n';
    }

  private:
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
        });
    }

    void write_trace(const CounterexampleTrace &trace, int indent) {
        print_object(indent, [&](auto field) {
            field("description", [&]() { write_string(trace.trace_description); });
            field("type", [&]() { write_string(trace.trace_type); });
            if (trace.loop_start_index.has_value()) {
                field("loop_start_index", [&]() {
                    out_ << *trace.loop_start_index;
                });
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
                    field("source", [&]() { write_source_location(*assignment.mapping, indent + 1); });
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
