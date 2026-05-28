#include "backends/pipeline/scheduler_review.hpp"
#include "backends/pipeline/review_helpers.hpp"
#include "printer_helpers.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

using backend_printer::line;

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
    line(out, 0, "workflow_status " + pipeline_review::workflow_status_name(summary.workflow_status));
    line(out, 0, "snapshot_status " + pipeline_review::snapshot_status_name(summary.snapshot_status));
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

    pipeline_review::print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    pipeline_review::print_string_list(out, 0, "executed_prefix", summary.completed_prefix);
    print_ready_nodes(summary.ready_nodes, out);
    print_blocked_nodes(summary.blocked_nodes, out);
}

} // namespace ahfl
