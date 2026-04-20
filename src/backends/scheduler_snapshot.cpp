#include "ahfl/backends/scheduler_snapshot.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "ahfl/support/json.hpp"

namespace ahfl {

namespace {

class SchedulerSnapshotJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit SchedulerSnapshotJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const scheduler_snapshot::SchedulerSnapshot &snapshot) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(snapshot.format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(snapshot.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(snapshot.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(snapshot.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(snapshot.source_replay_view_format_version); });
            field("source_package_identity", [&]() {
                if (snapshot.source_package_identity.has_value()) {
                    print_package_identity(*snapshot.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name", [&]() {
                write_string(snapshot.workflow_canonical_name);
            });
            field("session_id", [&]() { write_string(snapshot.session_id); });
            field("run_id", [&]() {
                if (snapshot.run_id.has_value()) {
                    write_string(*snapshot.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(snapshot.input_fixture); });
            field("workflow_status", [&]() {
                print_workflow_status(snapshot.workflow_status);
            });
            field("snapshot_status", [&]() {
                print_snapshot_status(snapshot.snapshot_status);
            });
            field("workflow_failure_summary", [&]() {
                if (snapshot.workflow_failure_summary.has_value()) {
                    print_failure_summary(*snapshot.workflow_failure_summary, 1);
                    return;
                }
                out_ << "null";
            });
            field("execution_order", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node_name : snapshot.execution_order) {
                        item([&]() { write_string(node_name); });
                    }
                });
            });
            field("ready_nodes", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : snapshot.ready_nodes) {
                        item([&]() { print_ready_node(node, 2); });
                    }
                });
            });
            field("blocked_nodes", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &node : snapshot.blocked_nodes) {
                        item([&]() { print_blocked_node(node, 2); });
                    }
                });
            });
            field("cursor", [&]() { print_cursor(snapshot.cursor, 1); });
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

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        switch (status) {
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
    }

    void print_snapshot_status(scheduler_snapshot::SchedulerSnapshotStatus status) {
        switch (status) {
        case scheduler_snapshot::SchedulerSnapshotStatus::Runnable:
            write_string("runnable");
            return;
        case scheduler_snapshot::SchedulerSnapshotStatus::Waiting:
            write_string("waiting");
            return;
        case scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted:
            write_string("terminal_completed");
            return;
        case scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed:
            write_string("terminal_failed");
            return;
        case scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial:
            write_string("terminal_partial");
            return;
        }
    }

    void print_blocked_reason(scheduler_snapshot::SchedulerBlockedReasonKind reason) {
        switch (reason) {
        case scheduler_snapshot::SchedulerBlockedReasonKind::WaitingOnDependencies:
            write_string("waiting_on_dependencies");
            return;
        case scheduler_snapshot::SchedulerBlockedReasonKind::WorkflowTerminalFailure:
            write_string("workflow_terminal_failure");
            return;
        case scheduler_snapshot::SchedulerBlockedReasonKind::UpstreamPartial:
            write_string("upstream_partial");
            return;
        }
    }

    void print_string_array(const std::vector<std::string> &values, int indent_level) {
        print_array(indent_level, [&](const auto &item) {
            for (const auto &value : values) {
                item([&]() { write_string(value); });
            }
        });
    }

    void print_ready_node(const scheduler_snapshot::SchedulerReadyNode &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("planned_dependencies", [&]() {
                print_string_array(node.planned_dependencies, indent_level + 1);
            });
            field("satisfied_dependencies", [&]() {
                print_string_array(node.satisfied_dependencies, indent_level + 1);
            });
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
        });
    }

    void print_blocked_node(const scheduler_snapshot::SchedulerBlockedNode &node,
                            int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("execution_index", [&]() {
                if (node.execution_index.has_value()) {
                    out_ << *node.execution_index;
                    return;
                }
                out_ << "null";
            });
            field("planned_dependencies", [&]() {
                print_string_array(node.planned_dependencies, indent_level + 1);
            });
            field("missing_dependencies", [&]() {
                print_string_array(node.missing_dependencies, indent_level + 1);
            });
            field("blocked_reason", [&]() { print_blocked_reason(node.blocked_reason); });
            field("may_become_ready", [&]() {
                out_ << (node.may_become_ready ? "true" : "false");
            });
            field("blocking_failure_summary", [&]() {
                if (node.blocking_failure_summary.has_value()) {
                    print_failure_summary(*node.blocking_failure_summary, indent_level + 1);
                    return;
                }
                out_ << "null";
            });
        });
    }

    void print_cursor(const scheduler_snapshot::SchedulerCursor &cursor, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("completed_prefix_size", [&]() { out_ << cursor.completed_prefix_size; });
            field("completed_prefix", [&]() {
                print_string_array(cursor.completed_prefix, indent_level + 1);
            });
            field("next_candidate_node_name", [&]() {
                if (cursor.next_candidate_node_name.has_value()) {
                    write_string(*cursor.next_candidate_node_name);
                    return;
                }
                out_ << "null";
            });
            field("checkpoint_friendly", [&]() {
                out_ << (cursor.checkpoint_friendly ? "true" : "false");
            });
        });
    }
};

} // namespace

void print_scheduler_snapshot_json(const scheduler_snapshot::SchedulerSnapshot &snapshot,
                                   std::ostream &out) {
    SchedulerSnapshotJsonPrinter{out}.print(snapshot);
}

} // namespace ahfl
