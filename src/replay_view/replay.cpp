#include "ahfl/replay_view/replay.hpp"
#include "ahfl/validation/common.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::replay_view {

namespace {

[[nodiscard]] const handoff::WorkflowPlan *
find_workflow_plan(const handoff::ExecutionPlan &plan, std::string_view workflow_canonical_name) {
    for (const auto &workflow : plan.workflows) {
        if (workflow.workflow_canonical_name == workflow_canonical_name) {
            return &workflow;
        }
    }

    return nullptr;
}

[[nodiscard]] const handoff::WorkflowNodePlan *
find_workflow_node(const handoff::WorkflowPlan &workflow, std::string_view node_name) {
    for (const auto &node : workflow.nodes) {
        if (node.name == node_name) {
            return &node;
        }
    }

    return nullptr;
}

[[nodiscard]] bool package_identity_equals(const std::optional<handoff::PackageIdentity> &lhs,
                                           const std::optional<handoff::PackageIdentity> &rhs) {
    return validation::package_identity_equals(lhs, rhs);
}

[[nodiscard]] std::string node_event_key(std::string_view node_name, std::size_t execution_index) {
    return std::string(node_name) + "#" + std::to_string(execution_index);
}

[[nodiscard]] bool
failure_summary_equals(const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
                       const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    return validation::failure_summary_equals(lhs, rhs);
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "replay view validation");
}

} // namespace

ReplayViewValidationResult validate_replay_view(const ReplayView &replay) {
    ReplayViewValidationResult result{
        .diagnostics = {},
    };

    if (replay.format_version != kReplayViewFormatVersion) {
        result.diagnostics.error()
            .message("replay view validation encountered unsupported format_version '" +
                     replay.format_version + "'")
            .emit();
    }

    if (replay.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error()
            .message("replay view validation encountered unsupported "
                     "source_execution_plan_format_version '" +
                     replay.source_execution_plan_format_version + "'")
            .emit();
    }

    if (replay.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        result.diagnostics.error()
            .message("replay view validation encountered unsupported "
                     "source_runtime_session_format_version '" +
                     replay.source_runtime_session_format_version + "'")
            .emit();
    }

    if (replay.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        result.diagnostics.error()
            .message("replay view validation encountered unsupported "
                     "source_execution_journal_format_version '" +
                     replay.source_execution_journal_format_version + "'")
            .emit();
    }

    if (replay.workflow_canonical_name.empty()) {
        result.diagnostics.error()
            .message("replay view validation contains empty workflow_canonical_name")
            .emit();
    }

    if (replay.session_id.empty()) {
        result.diagnostics.error()
            .message("replay view validation contains empty session_id")
            .emit();
    }

    if (replay.input_fixture.empty()) {
        result.diagnostics.error()
            .message("replay view validation contains empty input_fixture")
            .emit();
    }

    if (replay.workflow_failure_summary.has_value()) {
        validate_failure_summary(*replay.workflow_failure_summary, "workflow", result.diagnostics);
    }

    std::unordered_map<std::string, const ReplayNodeProgression *> nodes_by_name;
    std::unordered_set<std::string> execution_order_names;
    std::unordered_set<std::size_t> execution_indices;

    for (const auto &node_name : replay.execution_order) {
        if (node_name.empty()) {
            result.diagnostics.error()
                .message("replay view validation execution_order contains empty node name")
                .emit();
            continue;
        }

        if (!execution_order_names.insert(node_name).second) {
            result.diagnostics.error()
                .message("replay view validation execution_order contains duplicate node '" +
                         node_name + "'")
                .emit();
        }
    }

    for (const auto &node : replay.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error()
                .message("replay view validation contains node with empty node_name")
                .emit();
            continue;
        }

        if (!nodes_by_name.emplace(node.node_name, &node).second) {
            result.diagnostics.error()
                .message("replay view validation contains duplicate node '" + node.node_name + "'")
                .emit();
            continue;
        }

        if (node.target.empty()) {
            result.diagnostics.error()
                .message("replay view validation node '" + node.node_name + "' has empty target")
                .emit();
        }

        if (!execution_indices.insert(node.execution_index).second) {
            result.diagnostics.error()
                .message("replay view validation node '" + node.node_name +
                         "' reuses execution_index " + std::to_string(node.execution_index))
                .emit();
        }

        std::unordered_set<std::string> planned_dependencies;
        for (const auto &dependency : node.planned_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains empty planned dependency")
                    .emit();
                continue;
            }

            if (!planned_dependencies.insert(dependency).second) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains duplicate planned dependency '" + dependency + "'")
                    .emit();
            }
        }

        std::unordered_set<std::string> satisfied_dependencies;
        for (const auto &dependency : node.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains empty satisfied dependency")
                    .emit();
                continue;
            }

            if (!satisfied_dependencies.insert(dependency).second) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains duplicate satisfied dependency '" + dependency + "'")
                    .emit();
            }

            if (!planned_dependencies.contains(dependency)) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains satisfied dependency '" + dependency +
                             "' not declared in planned_dependencies")
                    .emit();
            }
        }

        std::unordered_set<std::string> used_mock_selectors;
        for (const auto &selector : node.used_mock_selectors) {
            if (selector.empty()) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains empty used mock selector")
                    .emit();
                continue;
            }

            if (!used_mock_selectors.insert(selector).second) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' contains duplicate used mock selector '" + selector + "'")
                    .emit();
            }
        }

        if (node.failure_summary.has_value()) {
            validate_failure_summary(
                *node.failure_summary, "node '" + node.node_name + "'", result.diagnostics);
            if (node.failure_summary->node_name.has_value() &&
                *node.failure_summary->node_name != node.node_name) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' failure summary node_name does not match node")
                    .emit();
            }
        }

        switch (node.final_status) {
        case runtime_session::NodeSessionStatus::Completed:
            if (!node.saw_node_became_ready || !node.saw_node_started || !node.saw_node_completed ||
                node.saw_node_failed) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' does not contain a complete ready/start/completed progression")
                    .emit();
            }
            if (node.failure_summary.has_value()) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' must not carry failure summary while final_status is Completed")
                    .emit();
            }
            break;
        case runtime_session::NodeSessionStatus::Failed:
            if (!node.saw_node_became_ready || !node.saw_node_started || node.saw_node_completed ||
                !node.saw_node_failed) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' does not contain a complete ready/start/failed progression")
                    .emit();
            }
            if (!node.failure_summary.has_value()) {
                result.diagnostics.error()
                    .message("replay view validation node '" + node.node_name +
                             "' must carry failure summary while final_status is Failed")
                    .emit();
            }
            break;
        case runtime_session::NodeSessionStatus::Blocked:
        case runtime_session::NodeSessionStatus::Ready:
        case runtime_session::NodeSessionStatus::Skipped:
            result.diagnostics.error()
                .message("replay view validation node '" + node.node_name +
                         "' is not executable terminal in replay progression")
                .emit();
            break;
        }
    }

    for (std::size_t index = 0; index < replay.execution_order.size(); ++index) {
        const auto &node_name = replay.execution_order[index];
        const auto found = nodes_by_name.find(node_name);
        if (found == nodes_by_name.end()) {
            result.diagnostics.error()
                .message("replay view validation execution_order references unknown node '" +
                         node_name + "'")
                .emit();
            continue;
        }

        if (found->second->execution_index != index) {
            result.diagnostics.error()
                .message("replay view validation execution_order node '" + node_name +
                         "' index does not match node execution_index")
                .emit();
        }
    }

    if (replay.execution_order.size() != replay.nodes.size()) {
        result.diagnostics.error()
            .message("replay view validation execution_order size must match replay node count")
            .emit();
    }

    switch (replay.workflow_status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        if (replay.replay_status != ReplayStatus::Consistent) {
            result.diagnostics.error()
                .message(
                    "replay view validation completed workflow must use replay_status Consistent")
                .emit();
        }
        if (replay.workflow_failure_summary.has_value()) {
            result.diagnostics.error()
                .message("replay view validation completed workflow must not carry workflow "
                         "failure summary")
                .emit();
        }
        break;
    case runtime_session::WorkflowSessionStatus::Failed:
        if (replay.replay_status != ReplayStatus::RuntimeFailed) {
            result.diagnostics.error()
                .message(
                    "replay view validation failed workflow must use replay_status RuntimeFailed")
                .emit();
        }
        if (!replay.workflow_failure_summary.has_value()) {
            result.diagnostics.error()
                .message(
                    "replay view validation failed workflow must carry workflow failure summary")
                .emit();
        }
        break;
    case runtime_session::WorkflowSessionStatus::Partial:
        if (replay.replay_status != ReplayStatus::Partial) {
            result.diagnostics.error()
                .message("replay view validation partial workflow must use replay_status Partial")
                .emit();
        }
        if (replay.workflow_failure_summary.has_value()) {
            result.diagnostics.error()
                .message("replay view validation partial workflow must not carry workflow failure "
                         "summary")
                .emit();
        }
        break;
    }

    if (!replay.consistency.plan_matches_session) {
        result.diagnostics.error()
            .message("replay view validation consistency.plan_matches_session must be true")
            .emit();
    }

    if (!replay.consistency.session_matches_journal) {
        result.diagnostics.error()
            .message("replay view validation consistency.session_matches_journal must be true")
            .emit();
    }

    if (!replay.consistency.journal_matches_execution_order) {
        result.diagnostics.error()
            .message(
                "replay view validation consistency.journal_matches_execution_order must be true")
            .emit();
    }

    return result;
}

ReplayViewResult build_replay_view(const handoff::ExecutionPlan &plan,
                                   const runtime_session::RuntimeSession &session,
                                   const execution_journal::ExecutionJournal &journal) {
    ReplayViewResult result{
        .replay = std::nullopt,
        .diagnostics = {},
    };

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);

    const auto session_validation = runtime_session::validate_runtime_session(session);
    result.diagnostics.append(session_validation.diagnostics);

    const auto journal_validation = execution_journal::validate_execution_journal(journal);
    result.diagnostics.append(journal_validation.diagnostics);

    if (result.has_errors()) {
        return result;
    }

    if (session.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error()
            .message("replay view runtime session source_execution_plan_format_version does not "
                     "match execution plan")
            .emit();
    }

    if (journal.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error()
            .message("replay view execution journal source_execution_plan_format_version does not "
                     "match execution plan")
            .emit();
    }

    if (journal.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error()
            .message("replay view execution journal source_runtime_session_format_version does not "
                     "match runtime session")
            .emit();
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        result.diagnostics.error()
            .message(
                "replay view runtime session source_package_identity does not match execution plan")
            .emit();
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        result.diagnostics.error()
            .message("replay view execution journal source_package_identity does not match "
                     "execution plan")
            .emit();
    }

    if (session.workflow_canonical_name != journal.workflow_canonical_name) {
        result.diagnostics.error()
            .message("replay view workflow_canonical_name does not match between runtime session "
                     "and execution journal")
            .emit();
    }

    if (session.session_id != journal.session_id) {
        result.diagnostics.error()
            .message("replay view session_id does not match between runtime session and execution "
                     "journal")
            .emit();
    }

    if (session.run_id != journal.run_id) {
        result.diagnostics.error()
            .message(
                "replay view run_id does not match between runtime session and execution journal")
            .emit();
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error()
            .message("replay view workflow '" + session.workflow_canonical_name +
                     "' does not exist in execution plan")
            .emit();
    }

    if (result.has_errors()) {
        return result;
    }

    std::unordered_map<std::string, const runtime_session::RuntimeSessionNode *> session_nodes;
    for (const auto &node : session.nodes) {
        session_nodes.emplace(node.node_name, &node);
    }

    std::vector<std::string> session_completed_order;
    session_completed_order.reserve(session.execution_order.size());
    for (const auto &node_name : session.execution_order) {
        const auto found = session_nodes.find(node_name);
        if (found != session_nodes.end() &&
            found->second->status == runtime_session::NodeSessionStatus::Completed) {
            session_completed_order.push_back(node_name);
        }
    }

    std::unordered_map<std::string, std::vector<const execution_journal::ExecutionJournalEvent *>>
        journal_events_by_node;
    std::vector<std::string> journal_completed_order;
    for (const auto &event : journal.events) {
        if (!event.node_name.has_value() || !event.execution_index.has_value()) {
            continue;
        }

        journal_events_by_node[node_event_key(*event.node_name, *event.execution_index)].push_back(
            &event);
        if (event.kind == execution_journal::ExecutionJournalEventKind::NodeCompleted) {
            journal_completed_order.push_back(*event.node_name);
        }
    }

    ReplayView replay{
        .format_version = std::string(kReplayViewFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = session.workflow_canonical_name,
        .session_id = session.session_id,
        .run_id = session.run_id,
        .input_fixture = session.input_fixture,
        .workflow_status = session.workflow_status,
        .replay_status =
            session.workflow_status == runtime_session::WorkflowSessionStatus::Completed
                ? ReplayStatus::Consistent
                : (session.workflow_status == runtime_session::WorkflowSessionStatus::Failed
                       ? ReplayStatus::RuntimeFailed
                       : ReplayStatus::Partial),
        .workflow_failure_summary = session.failure_summary,
        .execution_order = session.execution_order,
        .nodes = {},
        .consistency =
            ReplayConsistencySummary{
                .plan_matches_session = true,
                .session_matches_journal = true,
                .journal_matches_execution_order = true,
            },
    };
    replay.nodes.reserve(session.execution_order.size());

    if (journal_completed_order != session_completed_order) {
        replay.consistency.journal_matches_execution_order = false;
        result.diagnostics.error()
            .message("replay view execution journal completed node order does not match runtime "
                     "session completed execution_order")
            .emit();
    }

    for (const auto &node_name : session.execution_order) {
        const auto session_found = session_nodes.find(node_name);
        if (session_found == session_nodes.end()) {
            replay.consistency.plan_matches_session = false;
            result.diagnostics.error()
                .message("replay view runtime session execution_order references unknown node '" +
                         node_name + "'")
                .emit();
            continue;
        }

        const auto *plan_node = find_workflow_node(*workflow, node_name);
        if (plan_node == nullptr) {
            replay.consistency.plan_matches_session = false;
            result.diagnostics.error()
                .message("replay view execution_order node '" + node_name +
                         "' does not exist in execution plan workflow")
                .emit();
            continue;
        }

        const auto &session_node = *session_found->second;
        if (plan_node->target != session_node.target) {
            replay.consistency.plan_matches_session = false;
            result.diagnostics.error()
                .message("replay view node '" + node_name +
                         "' target does not match execution plan")
                .emit();
        }

        const auto key = node_event_key(node_name, session_node.execution_index);
        const auto journal_found = journal_events_by_node.find(key);
        if (journal_found == journal_events_by_node.end()) {
            replay.consistency.session_matches_journal = false;
            result.diagnostics.error()
                .message("replay view missing execution journal events for node '" + node_name +
                         "' execution_index " + std::to_string(session_node.execution_index))
                .emit();
            continue;
        }

        ReplayNodeProgression progression{
            .node_name = session_node.node_name,
            .target = session_node.target,
            .execution_index = session_node.execution_index,
            .planned_dependencies = plan_node->after,
            .satisfied_dependencies = session_node.satisfied_dependencies,
            .saw_node_became_ready = false,
            .saw_node_started = false,
            .saw_node_completed = false,
            .saw_node_failed = false,
            .failure_summary = std::nullopt,
            .used_mock_selectors = {},
            .final_status = session_node.status,
        };

        for (const auto *event : journal_found->second) {
            switch (event->kind) {
            case execution_journal::ExecutionJournalEventKind::NodeBecameReady:
                progression.saw_node_became_ready = true;
                break;
            case execution_journal::ExecutionJournalEventKind::NodeStarted:
                progression.saw_node_started = true;
                break;
            case execution_journal::ExecutionJournalEventKind::NodeCompleted:
                progression.saw_node_completed = true;
                progression.used_mock_selectors = event->used_mock_selectors;
                break;
            case execution_journal::ExecutionJournalEventKind::MockMissing:
            case execution_journal::ExecutionJournalEventKind::NodeFailed:
                progression.saw_node_failed = true;
                progression.failure_summary = event->failure_summary;
                progression.used_mock_selectors = event->used_mock_selectors;
                break;
            case execution_journal::ExecutionJournalEventKind::SessionStarted:
            case execution_journal::ExecutionJournalEventKind::WorkflowFailed:
            case execution_journal::ExecutionJournalEventKind::WorkflowCompleted:
                break;
            }
        }

        switch (session_node.status) {
        case runtime_session::NodeSessionStatus::Completed:
            if (!progression.saw_node_became_ready || !progression.saw_node_started ||
                !progression.saw_node_completed || progression.saw_node_failed) {
                replay.consistency.session_matches_journal = false;
                result.diagnostics.error()
                    .message("replay view node '" + node_name +
                             "' does not have a complete ready/start/completed event sequence")
                    .emit();
            }
            break;
        case runtime_session::NodeSessionStatus::Failed:
            if (!progression.saw_node_became_ready || !progression.saw_node_started ||
                progression.saw_node_completed || !progression.saw_node_failed) {
                replay.consistency.session_matches_journal = false;
                result.diagnostics.error()
                    .message("replay view node '" + node_name +
                             "' does not have a complete ready/start/failed event sequence")
                    .emit();
            }
            if (!failure_summary_equals(progression.failure_summary,
                                        session_node.failure_summary)) {
                replay.consistency.session_matches_journal = false;
                result.diagnostics.error()
                    .message("replay view node '" + node_name +
                             "' failure summary does not match runtime session")
                    .emit();
            }
            break;
        case runtime_session::NodeSessionStatus::Blocked:
        case runtime_session::NodeSessionStatus::Ready:
        case runtime_session::NodeSessionStatus::Skipped:
            replay.consistency.session_matches_journal = false;
            result.diagnostics.error()
                .message("replay view execution_order node '" + node_name +
                         "' is not executable terminal in runtime session")
                .emit();
            break;
        }

        replay.nodes.push_back(std::move(progression));
    }

    if (result.has_errors()) {
        return result;
    }

    const auto replay_validation = validate_replay_view(replay);
    result.diagnostics.append(replay_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.replay = std::move(replay);
    return result;
}

} // namespace ahfl::replay_view
