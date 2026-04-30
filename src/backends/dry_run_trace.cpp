#include "ahfl/backends/dry_run_trace.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class DryRunTraceJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DryRunTraceJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const dry_run::DryRunTrace &trace) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(trace.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(trace.source_execution_plan_format_version); });
            field("source_package_identity", [&]() {
                if (trace.source_package_identity.has_value()) {
                    print_package_identity(*trace.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(trace.workflow_canonical_name); });
            field("status", [&]() { write_string("completed"); });
            field("input_fixture", [&]() { write_string(trace.input_fixture); });
            field("run_id", [&]() {
                if (trace.run_id.has_value()) {
                    write_string(*trace.run_id);
                    return;
                }
                out_ << "null";
            });
            field("execution_order", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node_name : trace.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("node_traces", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : trace.node_traces) {
                        item([&]() { print_node_trace(node, 2); });
                    }
                });
            });
            field("return_summary",
                  [&]() { print_workflow_value_summary(trace.return_summary, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(identity.format_version); });
            field("name", [&]() { write_string(identity.name); });
            field("version", [&]() { write_string(identity.version); });
        });
    }

    void print_workflow_value_summary(const ir::WorkflowExprSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("reads", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &read : summary.reads) {
                        item([&]() {
                            print_object(indent_level + 2, [&](const auto &read_field) {
                                read_field("kind", [&]() {
                                    write_string(read.kind ==
                                                         ir::WorkflowValueSourceKind::WorkflowInput
                                                     ? "workflow_input"
                                                     : "workflow_node_output");
                                });
                                read_field("root_name", [&]() { write_string(read.root_name); });
                                read_field("members", [&]() {
                                    print_array(indent_level + 3, [&](const auto &member_item) {
                                        for (const auto &member : read.members) {
                                            member_item([&]() { write_string(member); });
                                        }
                                    });
                                });
                            });
                        });
                    }
                });
            });
        });
    }

    void print_lifecycle(const handoff::WorkflowNodeLifecycleSummary &lifecycle, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("start_condition", [&]() {
                write_string(lifecycle.start_condition ==
                                     handoff::WorkflowNodeStartConditionKind::Immediate
                                 ? "immediate"
                                 : "after_dependencies_completed");
            });
            field("completion_condition", [&]() { write_string("target_reached_final_state"); });
            field("completion_latched",
                  [&]() { out_ << (lifecycle.completion_latched ? "true" : "false"); });
            field("target_initial_state", [&]() { write_string(lifecycle.target_initial_state); });
            field("target_final_states", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &state : lifecycle.target_final_states) {
                        item([&]() { write_string(state); });
                    }
                });
            });
        });
    }

    void print_capability_binding_ref(const handoff::CapabilityBindingReference &binding,
                                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_name", [&]() { write_string(binding.capability_name); });
            field("binding_key", [&]() { write_string(binding.binding_key); });
        });
    }

    void print_capability_mock(const dry_run::CapabilityMock &mock, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_name", [&]() {
                if (mock.capability_name.has_value()) {
                    write_string(*mock.capability_name);
                    return;
                }
                out_ << "null";
            });
            field("binding_key", [&]() {
                if (mock.binding_key.has_value()) {
                    write_string(*mock.binding_key);
                    return;
                }
                out_ << "null";
            });
            field("result_fixture", [&]() { write_string(mock.result_fixture); });
            field("invocation_label", [&]() {
                if (mock.invocation_label.has_value()) {
                    write_string(*mock.invocation_label);
                    return;
                }
                out_ << "null";
            });
        });
    }

    void print_node_trace(const dry_run::DryRunNodeTrace &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("lifecycle", [&]() { print_lifecycle(node.lifecycle, indent_level + 1); });
            field("input_summary",
                  [&]() { print_workflow_value_summary(node.input_summary, indent_level + 1); });
            field("capability_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &binding : node.capability_bindings) {
                        item([&]() { print_capability_binding_ref(binding, indent_level + 2); });
                    }
                });
            });
            field("mock_results", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &mock : node.mock_results) {
                        item([&]() { print_capability_mock(mock, indent_level + 2); });
                    }
                });
            });
        });
    }
};

} // namespace

void print_dry_run_trace_json(const dry_run::DryRunTrace &trace, std::ostream &out) {
    DryRunTraceJsonPrinter{out}.print(trace);
}

} // namespace ahfl
