#include "ahfl/audit_report/report.hpp"
#include "ahfl/validation/common.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ahfl::audit_report {

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

[[nodiscard]] bool package_identity_equals(const std::optional<handoff::PackageIdentity> &lhs,
                                           const std::optional<handoff::PackageIdentity> &rhs) {
    return validation::package_identity_equals(lhs, rhs);
}

[[nodiscard]] std::string mock_selector_key(const dry_run::CapabilityMock &mock) {
    if (mock.binding_key.has_value()) {
        return "binding:" + *mock.binding_key;
    }

    if (mock.capability_name.has_value()) {
        return "capability:" + *mock.capability_name;
    }

    return {};
}

[[nodiscard]] bool node_status_is_executed(runtime_session::NodeSessionStatus status) {
    return status == runtime_session::NodeSessionStatus::Completed ||
           status == runtime_session::NodeSessionStatus::Failed;
}

[[nodiscard]] std::size_t
count_session_nodes_with_status(const std::vector<AuditSessionNodeSummary> &nodes,
                                runtime_session::NodeSessionStatus status) {
    std::size_t count = 0;
    for (const auto &node : nodes) {
        if (node.final_status == status) {
            count += 1;
        }
    }

    return count;
}

[[nodiscard]] std::vector<std::string>
completed_node_prefix(const std::vector<AuditSessionNodeSummary> &nodes,
                      const std::vector<std::string> &execution_order) {
    std::unordered_map<std::string, const AuditSessionNodeSummary *> nodes_by_name;
    for (const auto &node : nodes) {
        nodes_by_name.emplace(node.node_name, &node);
    }

    std::vector<std::string> completed_order;
    completed_order.reserve(execution_order.size());
    for (const auto &node_name : execution_order) {
        const auto found = nodes_by_name.find(node_name);
        if (found != nodes_by_name.end() &&
            found->second->final_status == runtime_session::NodeSessionStatus::Completed) {
            completed_order.push_back(node_name);
        }
    }

    return completed_order;
}

[[nodiscard]] bool is_prefix(const std::vector<std::string> &prefix,
                             const std::vector<std::string> &full) {
    return validation::is_prefix(prefix, full);
}

} // namespace

AuditReportValidationResult validate_audit_report(const AuditReport &report) {
    AuditReportValidationResult result{
        .diagnostics = {},
    };

    if (report.format_version != kAuditReportFormatVersion) {
        result.diagnostics.error()
            .message("audit report validation encountered unsupported format_version '" +
                     report.format_version + "'")
            .emit();
    }

    if (report.workflow_canonical_name.empty()) {
        result.diagnostics.error()
            .message("audit report validation contains empty workflow_canonical_name")
            .emit();
    }

    if (report.session_id.empty()) {
        result.diagnostics.error()
            .message("audit report validation contains empty session_id")
            .emit();
    }

    if (report.input_fixture.empty()) {
        result.diagnostics.error()
            .message("audit report validation contains empty input_fixture")
            .emit();
    }

    if (report.plan_summary.source_execution_plan_format_version !=
        handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error()
            .message("audit report validation encountered unsupported plan_summary "
                     "source_execution_plan_format_version '" +
                     report.plan_summary.source_execution_plan_format_version + "'")
            .emit();
    }

    if (report.session_summary.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        result.diagnostics.error()
            .message("audit report validation encountered unsupported session_summary "
                     "source_runtime_session_format_version '" +
                     report.session_summary.source_runtime_session_format_version + "'")
            .emit();
    }

    if (report.journal_summary.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        result.diagnostics.error()
            .message("audit report validation encountered unsupported journal_summary "
                     "source_execution_journal_format_version '" +
                     report.journal_summary.source_execution_journal_format_version + "'")
            .emit();
    }

    if (report.trace_summary.source_dry_run_trace_format_version != dry_run::kTraceFormatVersion) {
        result.diagnostics.error()
            .message("audit report validation encountered unsupported trace_summary "
                     "source_dry_run_trace_format_version '" +
                     report.trace_summary.source_dry_run_trace_format_version + "'")
            .emit();
    }

    std::unordered_map<std::string, const AuditPlanNodeSummary *> plan_nodes_by_name;
    std::unordered_set<std::string> plan_execution_order_names;
    for (const auto &node_name : report.plan_summary.execution_order) {
        if (node_name.empty()) {
            result.diagnostics.error()
                .message(
                    "audit report validation plan_summary execution_order contains empty node name")
                .emit();
            continue;
        }

        if (!plan_execution_order_names.insert(node_name).second) {
            result.diagnostics.error()
                .message("audit report validation plan_summary execution_order contains duplicate "
                         "node '" +
                         node_name + "'")
                .emit();
        }
    }

    for (const auto &node : report.plan_summary.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error()
                .message("audit report validation plan_summary contains node with empty node_name")
                .emit();
            continue;
        }

        if (!plan_nodes_by_name.emplace(node.node_name, &node).second) {
            result.diagnostics.error()
                .message("audit report validation plan_summary contains duplicate node '" +
                         node.node_name + "'")
                .emit();
            continue;
        }

        if (node.target.empty()) {
            result.diagnostics.error()
                .message("audit report validation plan_summary node '" + node.node_name +
                         "' has empty target")
                .emit();
        }

        std::unordered_set<std::string> planned_dependencies;
        for (const auto &dependency : node.planned_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error()
                    .message("audit report validation plan_summary node '" + node.node_name +
                             "' contains empty planned dependency")
                    .emit();
                continue;
            }

            if (!planned_dependencies.insert(dependency).second) {
                result.diagnostics.error()
                    .message("audit report validation plan_summary node '" + node.node_name +
                             "' contains duplicate planned dependency '" + dependency + "'")
                    .emit();
            }
        }
    }

    for (const auto &node_name : report.plan_summary.execution_order) {
        if (!plan_nodes_by_name.contains(node_name)) {
            result.diagnostics.error()
                .message("audit report validation plan_summary execution_order references unknown "
                         "node '" +
                         node_name + "'")
                .emit();
        }
    }

    std::unordered_map<std::string, const AuditSessionNodeSummary *> session_nodes_by_name;
    std::unordered_set<std::size_t> session_execution_indices;
    for (const auto &node : report.session_summary.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error()
                .message(
                    "audit report validation session_summary contains node with empty node_name")
                .emit();
            continue;
        }

        if (!session_nodes_by_name.emplace(node.node_name, &node).second) {
            result.diagnostics.error()
                .message("audit report validation session_summary contains duplicate node '" +
                         node.node_name + "'")
                .emit();
            continue;
        }

        if (node_status_is_executed(node.final_status)) {
            if (!session_execution_indices.insert(node.execution_index).second) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' reuses execution_index " + std::to_string(node.execution_index))
                    .emit();
            }
        }

        const auto plan_found = plan_nodes_by_name.find(node.node_name);
        if (plan_found == plan_nodes_by_name.end()) {
            result.diagnostics.error()
                .message("audit report validation session_summary node '" + node.node_name +
                         "' does not exist in plan_summary")
                .emit();
            continue;
        }

        std::unordered_set<std::string> planned_dependencies(
            plan_found->second->planned_dependencies.begin(),
            plan_found->second->planned_dependencies.end());
        std::unordered_set<std::string> satisfied_dependencies;
        for (const auto &dependency : node.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' contains empty satisfied dependency")
                    .emit();
                continue;
            }

            if (!satisfied_dependencies.insert(dependency).second) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' contains duplicate satisfied dependency '" + dependency + "'")
                    .emit();
            }

            if (!planned_dependencies.contains(dependency)) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' contains satisfied dependency '" + dependency +
                             "' not declared in plan_summary")
                    .emit();
            }
        }

        std::unordered_set<std::string> used_mock_selectors;
        for (const auto &selector : node.used_mock_selectors) {
            if (selector.empty()) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' contains empty used mock selector")
                    .emit();
                continue;
            }

            if (!used_mock_selectors.insert(selector).second) {
                result.diagnostics.error()
                    .message("audit report validation session_summary node '" + node.node_name +
                             "' contains duplicate used mock selector '" + selector + "'")
                    .emit();
            }
        }

        if (report.session_summary.workflow_status ==
                runtime_session::WorkflowSessionStatus::Completed &&
            node.final_status != runtime_session::NodeSessionStatus::Completed) {
            result.diagnostics.error()
                .message("audit report validation session_summary node '" + node.node_name +
                         "' is not Completed while workflow_status is Completed")
                .emit();
        }
    }

    for (std::size_t index = 0; index < report.plan_summary.execution_order.size(); ++index) {
        const auto &node_name = report.plan_summary.execution_order[index];
        const auto session_found = session_nodes_by_name.find(node_name);
        if (session_found == session_nodes_by_name.end()) {
            result.diagnostics.error()
                .message("audit report validation plan_summary execution_order node '" + node_name +
                         "' does not exist in session_summary")
                .emit();
            continue;
        }

        if (session_found->second->execution_index != index) {
            result.diagnostics.error()
                .message("audit report validation session_summary node '" + node_name +
                         "' execution_index does not match plan_summary execution_order")
                .emit();
        }
    }

    if (report.journal_summary.total_events == 0) {
        result.diagnostics.error()
            .message(
                "audit report validation journal_summary total_events must be greater than zero")
            .emit();
    }

    const auto completed_node_count = count_session_nodes_with_status(
        report.session_summary.nodes, runtime_session::NodeSessionStatus::Completed);
    const auto failed_node_count = count_session_nodes_with_status(
        report.session_summary.nodes, runtime_session::NodeSessionStatus::Failed);
    const auto completed_execution_order =
        completed_node_prefix(report.session_summary.nodes, report.plan_summary.execution_order);

    if (report.journal_summary.node_completed_events != completed_node_count) {
        result.diagnostics.error()
            .message("audit report validation journal_summary node_completed_events must match "
                     "completed session node count")
            .emit();
    }

    if (report.journal_summary.completed_node_order != completed_execution_order) {
        result.diagnostics.error()
            .message("audit report validation journal_summary completed_node_order does not match "
                     "completed execution_order prefix")
            .emit();
    }

    switch (report.session_summary.workflow_status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        if (report.conclusion != AuditConclusion::Passed) {
            result.diagnostics.error()
                .message("audit report validation completed workflow must use conclusion Passed")
                .emit();
        }
        if (report.journal_summary.workflow_completed_events != 1) {
            result.diagnostics.error()
                .message("audit report validation journal_summary workflow_completed_events must "
                         "equal 1 for completed workflow")
                .emit();
        }
        if (report.journal_summary.workflow_failed_events != 0) {
            result.diagnostics.error()
                .message("audit report validation completed workflow must not contain "
                         "workflow_failed events")
                .emit();
        }
        if (report.journal_summary.node_failed_events != 0) {
            result.diagnostics.error()
                .message("audit report validation completed workflow must not contain failed node "
                         "events")
                .emit();
        }
        break;
    case runtime_session::WorkflowSessionStatus::Failed:
        if (report.conclusion != AuditConclusion::RuntimeFailed) {
            result.diagnostics.error()
                .message(
                    "audit report validation failed workflow must use conclusion RuntimeFailed")
                .emit();
        }
        if (report.journal_summary.workflow_completed_events != 0) {
            result.diagnostics.error()
                .message("audit report validation failed workflow must not contain "
                         "workflow_completed events")
                .emit();
        }
        if (report.journal_summary.workflow_failed_events != 1) {
            result.diagnostics.error()
                .message("audit report validation failed workflow must contain exactly one "
                         "workflow_failed event")
                .emit();
        }
        if (failed_node_count == 0 || report.journal_summary.node_failed_events == 0) {
            result.diagnostics.error()
                .message("audit report validation failed workflow must contain failed session "
                         "nodes and failed journal events")
                .emit();
        }
        break;
    case runtime_session::WorkflowSessionStatus::Partial:
        if (report.conclusion != AuditConclusion::Partial) {
            result.diagnostics.error()
                .message("audit report validation partial workflow must use conclusion Partial")
                .emit();
        }
        if (report.journal_summary.workflow_completed_events != 0 ||
            report.journal_summary.workflow_failed_events != 0) {
            result.diagnostics.error()
                .message("audit report validation partial workflow must not contain terminal "
                         "workflow events")
                .emit();
        }
        if (report.journal_summary.node_failed_events != 0) {
            result.diagnostics.error()
                .message(
                    "audit report validation partial workflow must not contain failed node events")
                .emit();
        }
        break;
    }

    std::unordered_set<std::string> trace_execution_order_names;
    for (const auto &node_name : report.trace_summary.execution_order) {
        if (node_name.empty()) {
            result.diagnostics.error()
                .message("audit report validation trace_summary execution_order contains empty "
                         "node name")
                .emit();
            continue;
        }
        if (!trace_execution_order_names.insert(node_name).second) {
            result.diagnostics.error()
                .message("audit report validation trace_summary execution_order contains duplicate "
                         "node '" +
                         node_name + "'")
                .emit();
        }
    }

    if (!is_prefix(report.plan_summary.execution_order, report.trace_summary.execution_order)) {
        result.diagnostics.error()
            .message("audit report validation trace_summary execution_order must contain "
                     "plan_summary execution_order as prefix")
            .emit();
    }

    std::unordered_set<std::string> trace_nodes_by_name;
    for (const auto &node : report.trace_summary.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error()
                .message("audit report validation trace_summary contains node with empty node_name")
                .emit();
            continue;
        }

        if (!trace_nodes_by_name.insert(node.node_name).second) {
            result.diagnostics.error()
                .message("audit report validation trace_summary contains duplicate node '" +
                         node.node_name + "'")
                .emit();
        }

        std::unordered_set<std::string> selectors;
        for (const auto &selector : node.mock_result_selectors) {
            if (selector.empty()) {
                result.diagnostics.error()
                    .message("audit report validation trace_summary node '" + node.node_name +
                             "' contains empty mock result selector")
                    .emit();
                continue;
            }
            if (!selectors.insert(selector).second) {
                result.diagnostics.error()
                    .message("audit report validation trace_summary node '" + node.node_name +
                             "' contains duplicate mock result selector '" + selector + "'")
                    .emit();
            }
        }
    }

    if (trace_nodes_by_name.size() < report.plan_summary.execution_order.size()) {
        result.diagnostics.error()
            .message("audit report validation trace_summary node count must cover plan_summary "
                     "execution_order size")
            .emit();
    }

    if (!report.replay_consistency.plan_matches_session) {
        result.diagnostics.error()
            .message("audit report validation replay_consistency.plan_matches_session must be true")
            .emit();
    }
    if (!report.replay_consistency.session_matches_journal) {
        result.diagnostics.error()
            .message(
                "audit report validation replay_consistency.session_matches_journal must be true")
            .emit();
    }
    if (!report.replay_consistency.journal_matches_execution_order) {
        result.diagnostics.error()
            .message("audit report validation replay_consistency.journal_matches_execution_order "
                     "must be true")
            .emit();
    }

    if (!report.audit_consistency.plan_matches_session) {
        result.diagnostics.error()
            .message("audit report validation audit_consistency.plan_matches_session must be true")
            .emit();
    }
    if (!report.audit_consistency.session_matches_journal) {
        result.diagnostics.error()
            .message(
                "audit report validation audit_consistency.session_matches_journal must be true")
            .emit();
    }
    if (!report.audit_consistency.journal_matches_trace) {
        result.diagnostics.error()
            .message("audit report validation audit_consistency.journal_matches_trace must be true")
            .emit();
    }
    if (!report.audit_consistency.trace_matches_replay) {
        result.diagnostics.error()
            .message("audit report validation audit_consistency.trace_matches_replay must be true")
            .emit();
    }

    return result;
}

AuditReportResult build_audit_report(const handoff::ExecutionPlan &plan,
                                     const runtime_session::RuntimeSession &session,
                                     const execution_journal::ExecutionJournal &journal,
                                     const dry_run::DryRunTrace &trace) {
    AuditReportResult result{
        .report = std::nullopt,
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

    if (trace.format_version != dry_run::kTraceFormatVersion) {
        result.diagnostics.error()
            .message(
                "audit report bootstrap encountered unsupported dry-run trace format_version '" +
                trace.format_version + "'")
            .emit();
    }

    if (trace.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error()
            .message("audit report bootstrap dry-run trace source_execution_plan_format_version "
                     "does not match execution plan")
            .emit();
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        result.diagnostics.error()
            .message("audit report bootstrap runtime session source_package_identity does not "
                     "match execution plan")
            .emit();
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        result.diagnostics.error()
            .message("audit report bootstrap execution journal source_package_identity does not "
                     "match execution plan")
            .emit();
    }

    if (!package_identity_equals(plan.source_package_identity, trace.source_package_identity)) {
        result.diagnostics.error()
            .message("audit report bootstrap dry-run trace source_package_identity does not match "
                     "execution plan")
            .emit();
    }

    if (trace.workflow_canonical_name != session.workflow_canonical_name) {
        result.diagnostics.error()
            .message("audit report bootstrap dry-run trace workflow_canonical_name does not match "
                     "runtime session")
            .emit();
    }

    if (trace.input_fixture != session.input_fixture) {
        result.diagnostics.error()
            .message(
                "audit report bootstrap dry-run trace input_fixture does not match runtime session")
            .emit();
    }

    if (trace.run_id != session.run_id) {
        result.diagnostics.error()
            .message("audit report bootstrap dry-run trace run_id does not match runtime session")
            .emit();
    }

    if (trace.status != dry_run::DryRunStatus::Completed) {
        result.diagnostics.error()
            .message("audit report bootstrap currently requires dry-run trace status Completed")
            .emit();
    }

    const auto replay = replay_view::build_replay_view(plan, session, journal);
    result.diagnostics.append(replay.diagnostics);
    if (result.has_errors() || !replay.replay.has_value()) {
        return result;
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error()
            .message("audit report bootstrap workflow '" + session.workflow_canonical_name +
                     "' does not exist in execution plan")
            .emit();
        return result;
    }

    AuditReport report{
        .format_version = std::string(kAuditReportFormatVersion),
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = session.workflow_canonical_name,
        .session_id = session.session_id,
        .run_id = session.run_id,
        .input_fixture = session.input_fixture,
        .conclusion =
            session.workflow_status == runtime_session::WorkflowSessionStatus::Completed
                ? AuditConclusion::Passed
                : (session.workflow_status == runtime_session::WorkflowSessionStatus::Failed
                       ? AuditConclusion::RuntimeFailed
                       : AuditConclusion::Partial),
        .plan_summary =
            AuditPlanSummary{
                .source_execution_plan_format_version = plan.format_version,
                .execution_order = replay.replay->execution_order,
                .nodes = {},
                .return_summary = workflow->return_summary,
            },
        .session_summary =
            AuditSessionSummary{
                .source_runtime_session_format_version = session.format_version,
                .workflow_status = session.workflow_status,
                .nodes = {},
            },
        .journal_summary =
            AuditJournalSummary{
                .source_execution_journal_format_version = journal.format_version,
                .total_events = journal.events.size(),
                .node_ready_events = 0,
                .node_started_events = 0,
                .node_completed_events = 0,
                .node_failed_events = 0,
                .workflow_completed_events = 0,
                .workflow_failed_events = 0,
                .completed_node_order = {},
            },
        .trace_summary =
            AuditTraceSummary{
                .source_dry_run_trace_format_version = trace.format_version,
                .status = trace.status,
                .execution_order = trace.execution_order,
                .nodes = {},
                .return_summary = trace.return_summary,
            },
        .replay_consistency = replay.replay->consistency,
        .audit_consistency =
            AuditConsistencySummary{
                .plan_matches_session = replay.replay->consistency.plan_matches_session,
                .session_matches_journal = replay.replay->consistency.session_matches_journal,
                .journal_matches_trace = true,
                .trace_matches_replay = true,
            },
    };

    report.plan_summary.nodes.reserve(workflow->nodes.size());
    for (const auto &node : workflow->nodes) {
        report.plan_summary.nodes.push_back(AuditPlanNodeSummary{
            .node_name = node.name,
            .target = node.target,
            .planned_dependencies = node.after,
            .capability_bindings = node.capability_bindings,
        });
    }

    report.session_summary.nodes.reserve(session.nodes.size());
    for (const auto &node : session.nodes) {
        std::vector<std::string> used_mock_selectors;
        used_mock_selectors.reserve(node.used_mocks.size());
        for (const auto &usage : node.used_mocks) {
            used_mock_selectors.push_back(usage.selector);
        }

        report.session_summary.nodes.push_back(AuditSessionNodeSummary{
            .node_name = node.node_name,
            .final_status = node.status,
            .execution_index = node.execution_index,
            .satisfied_dependencies = node.satisfied_dependencies,
            .used_mock_selectors = std::move(used_mock_selectors),
        });
    }

    for (const auto &event : journal.events) {
        switch (event.kind) {
        case execution_journal::ExecutionJournalEventKind::SessionStarted:
            break;
        case execution_journal::ExecutionJournalEventKind::NodeBecameReady:
            report.journal_summary.node_ready_events += 1;
            break;
        case execution_journal::ExecutionJournalEventKind::NodeStarted:
            report.journal_summary.node_started_events += 1;
            break;
        case execution_journal::ExecutionJournalEventKind::NodeCompleted:
            report.journal_summary.node_completed_events += 1;
            if (event.node_name.has_value()) {
                report.journal_summary.completed_node_order.push_back(*event.node_name);
            }
            break;
        case execution_journal::ExecutionJournalEventKind::MockMissing:
        case execution_journal::ExecutionJournalEventKind::NodeFailed:
            report.journal_summary.node_failed_events += 1;
            break;
        case execution_journal::ExecutionJournalEventKind::WorkflowCompleted:
            report.journal_summary.workflow_completed_events += 1;
            break;
        case execution_journal::ExecutionJournalEventKind::WorkflowFailed:
            report.journal_summary.workflow_failed_events += 1;
            break;
        }
    }

    report.trace_summary.nodes.reserve(trace.node_traces.size());
    for (const auto &node : trace.node_traces) {
        std::vector<std::string> selectors;
        selectors.reserve(node.mock_results.size());
        for (const auto &mock : node.mock_results) {
            selectors.push_back(mock_selector_key(mock));
        }

        report.trace_summary.nodes.push_back(AuditTraceNodeSummary{
            .node_name = node.node_name,
            .execution_index = node.execution_index,
            .mock_result_selectors = std::move(selectors),
        });
    }

    if (!is_prefix(replay.replay->execution_order, trace.execution_order)) {
        report.audit_consistency.journal_matches_trace = false;
        report.audit_consistency.trace_matches_replay = false;
        result.diagnostics.error()
            .message("audit report bootstrap dry-run trace execution_order does not contain replay "
                     "view execution_order as prefix")
            .emit();
    }

    std::unordered_map<std::string, const AuditTraceNodeSummary *> trace_nodes_by_name;
    for (const auto &node : report.trace_summary.nodes) {
        trace_nodes_by_name.emplace(node.node_name, &node);
    }

    for (const auto &node : replay.replay->nodes) {
        const auto found = trace_nodes_by_name.find(node.node_name);
        if (found == trace_nodes_by_name.end()) {
            report.audit_consistency.trace_matches_replay = false;
            result.diagnostics.error()
                .message("audit report bootstrap dry-run trace node '" + node.node_name +
                         "' does not exist in replay view")
                .emit();
            continue;
        }

        if (found->second->execution_index != node.execution_index) {
            report.audit_consistency.trace_matches_replay = false;
            result.diagnostics.error()
                .message("audit report bootstrap dry-run trace node '" + node.node_name +
                         "' execution_index does not match replay view")
                .emit();
        }

        if (found->second->mock_result_selectors != node.used_mock_selectors) {
            report.audit_consistency.trace_matches_replay = false;
            result.diagnostics.error()
                .message("audit report bootstrap dry-run trace node '" + node.node_name +
                         "' mock result selectors do not match replay view")
                .emit();
        }
    }

    const auto report_validation = validate_audit_report(report);
    result.diagnostics.append(report_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.report = std::move(report);
    return result;
}

} // namespace ahfl::audit_report
