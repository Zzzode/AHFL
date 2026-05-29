#include "compiler/backends/pipeline/scheduler_snapshot.hpp"

#include <cstddef>
#include <ostream>
#include <string_view>

#include "compiler/backends/pipeline/json_helpers.hpp"

namespace ahfl {

namespace {

class SchedulerSnapshotJsonPrinter final : private PipelineJsonHelpers {
  public:
    explicit SchedulerSnapshotJsonPrinter(std::ostream &out) : PipelineJsonHelpers(out) {}

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
            field("workflow_canonical_name",
                  [&]() { write_string(snapshot.workflow_canonical_name); });
            field("session_id", [&]() { write_string(snapshot.session_id); });
            field("run_id", [&]() {
                if (snapshot.run_id.has_value()) {
                    write_string(*snapshot.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(snapshot.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(snapshot.workflow_status); });
            field("snapshot_status", [&]() { print_snapshot_status(snapshot.snapshot_status); });
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

    void print_ready_node(const scheduler_snapshot::SchedulerReadyNode &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("node_name", [&]() { write_string(node.node_name); });
            field("target", [&]() { write_string(node.target); });
            field("execution_index", [&]() { out_ << node.execution_index; });
            field("planned_dependencies",
                  [&]() { print_string_array(node.planned_dependencies, indent_level + 1); });
            field("satisfied_dependencies",
                  [&]() { print_string_array(node.satisfied_dependencies, indent_level + 1); });
            field("input_summary",
                  [&]() { print_workflow_value_summary(node.input_summary, indent_level + 1); });
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
            field("planned_dependencies",
                  [&]() { print_string_array(node.planned_dependencies, indent_level + 1); });
            field("missing_dependencies",
                  [&]() { print_string_array(node.missing_dependencies, indent_level + 1); });
            field("blocked_reason", [&]() { print_blocked_reason(node.blocked_reason); });
            field("may_become_ready",
                  [&]() { out_ << (node.may_become_ready ? "true" : "false"); });
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
            field("completed_prefix",
                  [&]() { print_string_array(cursor.completed_prefix, indent_level + 1); });
            field("next_candidate_node_name", [&]() {
                if (cursor.next_candidate_node_name.has_value()) {
                    write_string(*cursor.next_candidate_node_name);
                    return;
                }
                out_ << "null";
            });
            field("checkpoint_friendly",
                  [&]() { out_ << (cursor.checkpoint_friendly ? "true" : "false"); });
        });
    }
};

} // namespace

void print_scheduler_snapshot_json(const scheduler_snapshot::SchedulerSnapshot &snapshot,
                                   std::ostream &out) {
    SchedulerSnapshotJsonPrinter{out}.print(snapshot);
}

} // namespace ahfl
