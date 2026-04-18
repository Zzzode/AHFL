#include "ahfl/backends/runtime_session.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class RuntimeSessionJsonPrinter final {
  public:
    explicit RuntimeSessionJsonPrinter(std::ostream &out) : out_(out) {}

    void print(const runtime_session::RuntimeSession &session) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(session.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(session.source_execution_plan_format_version); });
            field("source_package_identity", [&]() {
                if (session.source_package_identity.has_value()) {
                    print_package_identity(*session.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name", [&]() {
                write_string(session.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(session.session_id); });
            field("run_id", [&]() {
                if (session.run_id.has_value()) {
                    write_string(*session.run_id);
                    return;
                }
                out_ << "null";
            });
            field("workflow_status", [&]() {
                switch (session.workflow_status) {
                case runtime_session::WorkflowSessionStatus::Completed:
                    write_string("completed");
                    return;
                case runtime_session::WorkflowSessionStatus::Failed:
                    write_string("failed");
                    return;
                case runtime_session::WorkflowSessionStatus::Partial:
                    write_string("partial");
                    return;
                }
            });
            field("failure_summary", [&]() {
                if (session.failure_summary.has_value()) {
                    print_failure_summary(*session.failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(session.input_fixture); });
            field("options", [&]() { print_options(session.options, 1); });
            field("execution_order", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node_name : session.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : session.nodes) {
                        item([&]() { print_node(node, 2); });
                    }
                });
            });
            field("return_summary", [&]() {
                print_workflow_value_summary(session.return_summary, 1);
            });
        });
        out_ << '\n';
    }

  private:
    std::ostream &out_;

    void write_indent(int indent_level) {
        out_ << std::string(static_cast<std::size_t>(indent_level) * 2, ' ');
    }

    void newline_and_indent(int indent_level) {
        out_ << '\n';
        write_indent(indent_level);
    }

    void write_string(std::string_view value) {
        write_escaped_json_string(out_, value);
    }

    template <typename WriteFields> void print_object(int indent_level, WriteFields write_fields) {
        out_ << '{';
        bool wrote_any_field = false;

        const auto field = [&](std::string_view name, const auto &write_value) {
            if (wrote_any_field) {
                out_ << ',';
            }
            wrote_any_field = true;
            newline_and_indent(indent_level + 1);
            write_string(name);
            out_ << ": ";
            write_value();
        };

        write_fields(field);

        if (wrote_any_field) {
            newline_and_indent(indent_level);
        }
        out_ << '}';
    }

    template <typename WriteItems> void print_array(int indent_level, WriteItems write_items) {
        out_ << '[';
        bool wrote_any_item = false;

        const auto item = [&](const auto &write_value) {
            if (wrote_any_item) {
                out_ << ',';
            }
            wrote_any_item = true;
            newline_and_indent(indent_level + 1);
            write_value();
        };

        write_items(item);

        if (wrote_any_item) {
            newline_and_indent(indent_level);
        }
        out_ << ']';
    }

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
                                    write_string(
                                        read.kind == ir::WorkflowValueSourceKind::WorkflowInput
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
            field("completion_condition", [&]() {
                write_string("target_reached_final_state");
            });
            field("completion_latched", [&]() {
                out_ << (lifecycle.completion_latched ? "true" : "false");
            });
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

    void print_mock_usage(const runtime_session::RuntimeSessionMockUsage &usage, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("selector", [&]() { write_string(usage.selector); });
            field("capability_name", [&]() {
                if (usage.capability_name.has_value()) {
                    write_string(*usage.capability_name);
                    return;
                }
                out_ << "null";
            });
            field("binding_key", [&]() {
                if (usage.binding_key.has_value()) {
                    write_string(*usage.binding_key);
                    return;
                }
                out_ << "null";
            });
            field("result_fixture", [&]() { write_string(usage.result_fixture); });
            field("invocation_label", [&]() {
                if (usage.invocation_label.has_value()) {
                    write_string(*usage.invocation_label);
                    return;
                }
                out_ << "null";
            });
        });
    }

    void print_options(const runtime_session::RuntimeSessionOptions &options, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("sequential_mode", [&]() {
                out_ << (options.sequential_mode ? "true" : "false");
            });
        });
    }

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() {
                if (summary.node_name.has_value()) {
                    write_string(*summary.node_name);
                    return;
                }
                out_ << "null";
            });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_node(const runtime_session::RuntimeSessionNode &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("status", [&]() {
                switch (node.status) {
                case runtime_session::NodeSessionStatus::Blocked:
                    write_string("blocked");
                    return;
                case runtime_session::NodeSessionStatus::Ready:
                    write_string("ready");
                    return;
                case runtime_session::NodeSessionStatus::Completed:
                    write_string("completed");
                    return;
                case runtime_session::NodeSessionStatus::Failed:
                    write_string("failed");
                    return;
                case runtime_session::NodeSessionStatus::Skipped:
                    write_string("skipped");
                    return;
                }
            });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("failure_summary", [&]() {
                if (node.failure_summary.has_value()) {
                    print_failure_summary(*node.failure_summary, indent_level + 1);
                    return;
                }
                out_ << "null";
            });
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("lifecycle", [&]() { print_lifecycle(node.lifecycle, indent_level + 1); });
            field("input_summary", [&]() {
                print_workflow_value_summary(node.input_summary, indent_level + 1);
            });
            field("capability_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &binding : node.capability_bindings) {
                        item([&]() { print_capability_binding_ref(binding, indent_level + 2); });
                    }
                });
            });
            field("used_mocks", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &usage : node.used_mocks) {
                        item([&]() { print_mock_usage(usage, indent_level + 2); });
                    }
                });
            });
        });
    }
};

} // namespace

void print_runtime_session_json(const runtime_session::RuntimeSession &session, std::ostream &out) {
    RuntimeSessionJsonPrinter{out}.print(session);
}

} // namespace ahfl
