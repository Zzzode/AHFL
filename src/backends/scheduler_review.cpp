#include "ahfl/backends/scheduler_review.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string snapshot_status_name(scheduler_snapshot::SchedulerSnapshotStatus status) {
    switch (status) {
    case scheduler_snapshot::SchedulerSnapshotStatus::Runnable:
        return "runnable";
    case scheduler_snapshot::SchedulerSnapshotStatus::Waiting:
        return "waiting";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalCompleted:
        return "terminal_completed";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalFailed:
        return "terminal_failed";
    case scheduler_snapshot::SchedulerSnapshotStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
blocked_reason_name(scheduler_snapshot::SchedulerBlockedReasonKind reason) {
    switch (reason) {
    case scheduler_snapshot::SchedulerBlockedReasonKind::WaitingOnDependencies:
        return "waiting_on_dependencies";
    case scheduler_snapshot::SchedulerBlockedReasonKind::WorkflowTerminalFailure:
        return "workflow_terminal_failure";
    case scheduler_snapshot::SchedulerBlockedReasonKind::UpstreamPartial:
        return "upstream_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(scheduler_snapshot::SchedulerNextActionKind action) {
    switch (action) {
    case scheduler_snapshot::SchedulerNextActionKind::RunReadyNode:
        return "run_ready_node";
    case scheduler_snapshot::SchedulerNextActionKind::AwaitDependencies:
        return "await_dependencies";
    case scheduler_snapshot::SchedulerNextActionKind::WorkflowCompleted:
        return "workflow_completed";
    case scheduler_snapshot::SchedulerNextActionKind::InvestigateFailure:
        return "investigate_failure";
    case scheduler_snapshot::SchedulerNextActionKind::PreservePartialState:
        return "preserve_partial_state";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_string_list(std::ostream &out,
                       int indent_level,
                       std::string_view label,
                       const std::vector<std::string> &values) {
    line(out, indent_level, std::string(label) + " {");
    line(out, indent_level + 1, "count " + std::to_string(values.size()));
    if (values.empty()) {
        line(out, indent_level + 1, "- none");
    } else {
        for (const auto &value : values) {
            line(out, indent_level + 1, "- " + value);
        }
    }
    line(out, indent_level, "}");
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_ready_nodes(const std::vector<scheduler_snapshot::SchedulerReviewReadyNode> &nodes,
                       std::ostream &out) {
    line(out, 0, "ready_set {");
    line(out, 1, "count " + std::to_string(nodes.size()));
    if (nodes.empty()) {
        line(out, 1, "- none");
    } else {
        for (const auto &node : nodes) {
            line(out,
                 1,
                 "- " + node.node_name + " target=" + node.target +
                     " execution_index=" + std::to_string(node.execution_index));
            line(out,
                 2,
                 "planned_dependencies " +
                     (node.planned_dependencies.empty()
                          ? std::string("none")
                          : std::to_string(node.planned_dependencies.size())));
            for (const auto &dependency : node.planned_dependencies) {
                line(out, 3, "- " + dependency);
            }
            line(out,
                 2,
                 "satisfied_dependencies " +
                     (node.satisfied_dependencies.empty()
                          ? std::string("none")
                          : std::to_string(node.satisfied_dependencies.size())));
            for (const auto &dependency : node.satisfied_dependencies) {
                line(out, 3, "- " + dependency);
            }
        }
    }
    line(out, 0, "}");
}

void print_blocked_nodes(const std::vector<scheduler_snapshot::SchedulerReviewBlockedNode> &nodes,
                         std::ostream &out) {
    line(out, 0, "blocked_frontier {");
    line(out, 1, "count " + std::to_string(nodes.size()));
    if (nodes.empty()) {
        line(out, 1, "- none");
    } else {
        for (const auto &node : nodes) {
            line(out,
                 1,
                 "- " + node.node_name + " target=" + node.target + " execution_index=" +
                     (node.execution_index.has_value() ? std::to_string(*node.execution_index)
                                                       : std::string("none")));
            line(out, 2, "blocked_reason " + blocked_reason_name(node.blocked_reason));
            line(out,
                 2,
                 std::string("may_become_ready ") + (node.may_become_ready ? "true" : "false"));
            line(out,
                 2,
                 "missing_dependencies " +
                     (node.missing_dependencies.empty()
                          ? std::string("none")
                          : std::to_string(node.missing_dependencies.size())));
            for (const auto &dependency : node.missing_dependencies) {
                line(out, 3, "- " + dependency);
            }
        }
    }
    line(out, 0, "}");
}

} // namespace

void print_scheduler_review(const scheduler_snapshot::SchedulerDecisionSummary &summary,
                            std::ostream &out) {
    out << summary.format_version << '\n';
    line(out, 0, "source_snapshot_format " + summary.source_scheduler_snapshot_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "snapshot_status " + snapshot_status_name(summary.snapshot_status));
    line(out,
         0,
         std::string("checkpoint_friendly ") + (summary.checkpoint_friendly ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_candidate " + (summary.next_candidate_node_name.has_value()
                                  ? *summary.next_candidate_node_name
                                  : "none"));
    line(out,
         0,
         "terminal_reason " +
             (summary.terminal_reason.has_value() ? *summary.terminal_reason : "none"));

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_string_list(out, 0, "executed_prefix", summary.completed_prefix);
    print_ready_nodes(summary.ready_nodes, out);
    print_blocked_nodes(summary.blocked_nodes, out);
}

} // namespace ahfl
