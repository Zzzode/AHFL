#include "ahfl/backends/audit_report.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class AuditReportJsonPrinter final {
  public:
    explicit AuditReportJsonPrinter(std::ostream &out) : out_(out) {}

    void print(const audit_report::AuditReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("source_package_identity", [&]() {
                if (report.source_package_identity.has_value()) {
                    print_package_identity(*report.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name", [&]() {
                write_string(report.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() {
                if (report.run_id.has_value()) {
                    write_string(*report.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("conclusion", [&]() {
                switch (report.conclusion) {
                case audit_report::AuditConclusion::Passed:
                    write_string("passed");
                    return;
                case audit_report::AuditConclusion::RuntimeFailed:
                    write_string("runtime_failed");
                    return;
                case audit_report::AuditConclusion::Partial:
                    write_string("partial");
                    return;
                }
            });
            field("plan_summary", [&]() { print_plan_summary(report.plan_summary, 1); });
            field("session_summary", [&]() { print_session_summary(report.session_summary, 1); });
            field("journal_summary", [&]() { print_journal_summary(report.journal_summary, 1); });
            field("trace_summary", [&]() { print_trace_summary(report.trace_summary, 1); });
            field("replay_consistency", [&]() {
                print_replay_consistency(report.replay_consistency, 1);
            });
            field("audit_consistency", [&]() {
                print_audit_consistency(report.audit_consistency, 1);
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

    void print_capability_binding_ref(const handoff::CapabilityBindingReference &binding,
                                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_name", [&]() { write_string(binding.capability_name); });
            field("binding_key", [&]() { write_string(binding.binding_key); });
        });
    }

    void print_plan_node(const audit_report::AuditPlanNodeSummary &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("planned_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.planned_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("capability_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &binding : node.capability_bindings) {
                        item([&]() { print_capability_binding_ref(binding, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_plan_summary(const audit_report::AuditPlanSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("source_execution_plan_format_version", [&]() {
                write_string(summary.source_execution_plan_format_version);
            });
            field("execution_order", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node_name : summary.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : summary.nodes) {
                        item([&]() { print_plan_node(node, indent_level + 2); });
                    }
                });
            });
            field("return_summary", [&]() {
                print_workflow_value_summary(summary.return_summary, indent_level + 1);
            });
        });
    }

    void print_session_node(const audit_report::AuditSessionNodeSummary &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("final_status", [&]() {
                switch (node.final_status) {
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
            field("satisfied_dependencies", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.satisfied_dependencies) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("used_mock_selectors", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &selector : node.used_mock_selectors) {
                        item([&]() { write_string(selector); });
                    }
                });
            });
        });
    }

    void print_session_summary(const audit_report::AuditSessionSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("source_runtime_session_format_version", [&]() {
                write_string(summary.source_runtime_session_format_version);
            });
            field("workflow_status", [&]() {
                switch (summary.workflow_status) {
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
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : summary.nodes) {
                        item([&]() { print_session_node(node, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_journal_summary(const audit_report::AuditJournalSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("source_execution_journal_format_version", [&]() {
                write_string(summary.source_execution_journal_format_version);
            });
            field("total_events", [&]() { out_ << summary.total_events; });
            field("node_ready_events", [&]() { out_ << summary.node_ready_events; });
            field("node_started_events", [&]() { out_ << summary.node_started_events; });
            field("node_completed_events", [&]() { out_ << summary.node_completed_events; });
            field("node_failed_events", [&]() { out_ << summary.node_failed_events; });
            field("workflow_completed_events", [&]() {
                out_ << summary.workflow_completed_events;
            });
            field("workflow_failed_events", [&]() {
                out_ << summary.workflow_failed_events;
            });
            field("completed_node_order", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node_name : summary.completed_node_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
        });
    }

    void print_trace_node(const audit_report::AuditTraceNodeSummary &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("mock_result_selectors", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &selector : node.mock_result_selectors) {
                        item([&]() { write_string(selector); });
                    }
                });
            });
        });
    }

    void print_trace_summary(const audit_report::AuditTraceSummary &summary, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("source_dry_run_trace_format_version", [&]() {
                write_string(summary.source_dry_run_trace_format_version);
            });
            field("status", [&]() { write_string("completed"); });
            field("execution_order", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node_name : summary.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : summary.nodes) {
                        item([&]() { print_trace_node(node, indent_level + 2); });
                    }
                });
            });
            field("return_summary", [&]() {
                print_workflow_value_summary(summary.return_summary, indent_level + 1);
            });
        });
    }

    void print_replay_consistency(const replay_view::ReplayConsistencySummary &consistency,
                                  int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("plan_matches_session", [&]() {
                out_ << (consistency.plan_matches_session ? "true" : "false");
            });
            field("session_matches_journal", [&]() {
                out_ << (consistency.session_matches_journal ? "true" : "false");
            });
            field("journal_matches_execution_order", [&]() {
                out_ << (consistency.journal_matches_execution_order ? "true" : "false");
            });
        });
    }

    void print_audit_consistency(const audit_report::AuditConsistencySummary &consistency,
                                 int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("plan_matches_session", [&]() {
                out_ << (consistency.plan_matches_session ? "true" : "false");
            });
            field("session_matches_journal", [&]() {
                out_ << (consistency.session_matches_journal ? "true" : "false");
            });
            field("journal_matches_trace", [&]() {
                out_ << (consistency.journal_matches_trace ? "true" : "false");
            });
            field("trace_matches_replay", [&]() {
                out_ << (consistency.trace_matches_replay ? "true" : "false");
            });
        });
    }
};

} // namespace

void print_audit_report_json(const audit_report::AuditReport &report, std::ostream &out) {
    AuditReportJsonPrinter{out}.print(report);
}

} // namespace ahfl
