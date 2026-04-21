#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/support/diagnostics.hpp"
#include "ahfl/validation/common.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::scheduler_snapshot {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.SCHEDULER_SNAPSHOT";
inline constexpr std::string_view kBootstrapDiagnosticCode = "AHFL.BE.SCHEDULER_SNAPSHOT_BOOTSTRAP";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

void emit_bootstrap_error(DiagnosticBag &diagnostics, std::string message) {
    diagnostics.error()
        .legacy_code(DiagnosticCategory::Backend, kBootstrapDiagnosticCode)
        .message(std::move(message))
        .emit();
}


[[nodiscard]] bool insert_unique_node_name(const std::string &name,
                                           std::unordered_set<std::string> &seen_names,
                                           DiagnosticBag &diagnostics,
                                           std::string_view field_name) {
    if (!seen_names.insert(name).second) {
        emit_validation_error(diagnostics, "scheduler snapshot contains duplicate node '" + name + "' in " +
                          std::string(field_name));
        return false;
    }

    return true;
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "scheduler snapshot");
}

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(identity, diagnostics, "scheduler snapshot");
}

void validate_dependency_list(const std::vector<std::string> &dependencies,
                              std::string_view owner_kind,
                              std::string_view owner_name,
                              std::string_view field_name,
                              const std::unordered_set<std::string> &execution_nodes,
                              DiagnosticBag &diagnostics) {
    std::unordered_set<std::string> seen_dependencies;
    for (const auto &dependency : dependencies) {
        if (dependency.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) + "' contains empty " +
                              std::string(field_name));
            continue;
        }

        if (!seen_dependencies.insert(dependency).second) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) + "' contains duplicate " +
                              std::string(field_name) + " '" + dependency + "'");
        }

        if (!execution_nodes.contains(dependency)) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) + "' references unknown dependency '" +
                              dependency + "' in " + std::string(field_name));
        }
    }
}

void validate_dependency_subset(const std::vector<std::string> &subset,
                                const std::vector<std::string> &superset,
                                std::string_view owner_kind,
                                std::string_view owner_name,
                                std::string_view subset_field_name,
                                DiagnosticBag &diagnostics) {
    const std::unordered_set<std::string> declared_dependencies{superset.begin(), superset.end()};
    for (const auto &dependency : subset) {
        if (!declared_dependencies.contains(dependency)) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) + "' contains " +
                              std::string(subset_field_name) + " '" + dependency +
                              "' not declared in planned_dependencies");
        }
    }
}

void validate_capability_bindings(
    const std::vector<handoff::CapabilityBindingReference> &capability_bindings,
    std::string_view owner_kind,
    std::string_view owner_name,
    DiagnosticBag &diagnostics) {
    std::unordered_set<std::string> seen_bindings;
    for (const auto &binding : capability_bindings) {
        if (binding.capability_name.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) +
                              "' contains capability binding with empty capability_name");
        }

        if (binding.binding_key.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) +
                              "' contains capability binding with empty binding_key");
        }

        const auto binding_identity = binding.capability_name + "#" + binding.binding_key;
        if (!seen_bindings.insert(binding_identity).second) {
            emit_validation_error(diagnostics, "scheduler snapshot " + std::string(owner_kind) + " '" +
                              std::string(owner_name) + "' contains duplicate capability binding '" +
                              binding_identity + "'");
        }
    }
}

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

[[nodiscard]] bool is_prefix(const std::vector<std::string> &prefix,
                             const std::vector<std::string> &full) {
    return validation::is_prefix(prefix, full);
}

[[nodiscard]] bool failure_summary_equals(
    const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
    const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    return validation::failure_summary_equals(lhs, rhs);
}

} // namespace

SchedulerSnapshotValidationResult
validate_scheduler_snapshot(const SchedulerSnapshot &snapshot) {
    SchedulerSnapshotValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (snapshot.format_version != kSchedulerSnapshotFormatVersion) {
        emit_validation_error(diagnostics, "scheduler snapshot format_version must be '" +
                          std::string(kSchedulerSnapshotFormatVersion) + "'");
    }

    if (snapshot.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        emit_validation_error(diagnostics, "scheduler snapshot source_execution_plan_format_version must be '" +
                          std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (snapshot.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        emit_validation_error(diagnostics, "scheduler snapshot source_runtime_session_format_version must be '" +
                          std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (snapshot.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        emit_validation_error(diagnostics, 
            "scheduler snapshot source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (snapshot.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        emit_validation_error(diagnostics, "scheduler snapshot source_replay_view_format_version must be '" +
                          std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (snapshot.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "scheduler snapshot workflow_canonical_name must not be empty");
    }

    if (snapshot.session_id.empty()) {
        emit_validation_error(diagnostics, "scheduler snapshot session_id must not be empty");
    }

    if (snapshot.input_fixture.empty()) {
        emit_validation_error(diagnostics, "scheduler snapshot input_fixture must not be empty");
    }

    if (snapshot.source_package_identity.has_value()) {
        validate_package_identity(*snapshot.source_package_identity, diagnostics);
    }

    if (snapshot.workflow_failure_summary.has_value()) {
        validate_failure_summary(*snapshot.workflow_failure_summary, "workflow", diagnostics);
    }

    std::unordered_set<std::string> execution_nodes;
    for (const auto &node_name : snapshot.execution_order) {
        if (node_name.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot execution_order contains empty node name");
            continue;
        }

        if (!execution_nodes.insert(node_name).second) {
            emit_validation_error(diagnostics, "scheduler snapshot execution_order contains duplicate node '" +
                              node_name + "'");
        }
    }

    if (snapshot.cursor.completed_prefix_size != snapshot.cursor.completed_prefix.size()) {
        emit_validation_error(diagnostics, 
            "scheduler snapshot cursor completed_prefix_size must match completed_prefix length");
    }

    if (snapshot.cursor.completed_prefix.size() > snapshot.execution_order.size()) {
        emit_validation_error(diagnostics, 
            "scheduler snapshot cursor completed_prefix cannot be longer than execution_order");
    }

    for (std::size_t index = 0; index < snapshot.cursor.completed_prefix.size(); ++index) {
        if (snapshot.cursor.completed_prefix[index] != snapshot.execution_order[index]) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot cursor completed_prefix must be a prefix of execution_order");
            break;
        }
    }

    if (snapshot.cursor.next_candidate_node_name.has_value()) {
        if (snapshot.cursor.next_candidate_node_name->empty()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot cursor next_candidate_node_name must not be empty");
        }

        if (!execution_nodes.contains(*snapshot.cursor.next_candidate_node_name)) {
            emit_validation_error(diagnostics, "scheduler snapshot cursor next_candidate_node_name '" +
                              *snapshot.cursor.next_candidate_node_name +
                              "' does not exist in execution_order");
        } else {
            for (const auto &completed_node : snapshot.cursor.completed_prefix) {
                if (completed_node == *snapshot.cursor.next_candidate_node_name) {
                    emit_validation_error(diagnostics, "scheduler snapshot cursor next_candidate_node_name '" +
                                      *snapshot.cursor.next_candidate_node_name +
                                      "' cannot already be in completed_prefix");
                    break;
                }
            }
        }
    }

    std::unordered_set<std::string> seen_snapshot_nodes;
    std::unordered_set<std::string> ready_node_names;
    std::unordered_set<std::string> blocked_node_names;

    for (const auto &node : snapshot.ready_nodes) {
        static_cast<void>(insert_unique_node_name(
            node.node_name, seen_snapshot_nodes, diagnostics, "ready_nodes"));
        static_cast<void>(ready_node_names.insert(node.node_name));

        if (node.node_name.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot ready node must not have empty node_name");
            continue;
        }

        if (node.target.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot ready node '" + node.node_name +
                              "' must not have empty target");
        }

        if (!execution_nodes.contains(node.node_name)) {
            emit_validation_error(diagnostics, "scheduler snapshot ready node '" + node.node_name +
                              "' does not exist in execution_order");
        } else if (node.execution_index >= snapshot.execution_order.size()) {
            emit_validation_error(diagnostics, "scheduler snapshot ready node '" + node.node_name +
                              "' has execution_index outside execution_order");
        } else if (snapshot.execution_order[node.execution_index] != node.node_name) {
            emit_validation_error(diagnostics, "scheduler snapshot ready node '" + node.node_name +
                              "' has execution_index that does not match execution_order");
        }

        for (const auto &completed_node : snapshot.cursor.completed_prefix) {
            if (completed_node == node.node_name) {
                emit_validation_error(diagnostics, "scheduler snapshot ready node '" + node.node_name +
                                  "' cannot already be in completed_prefix");
                break;
            }
        }

        validate_dependency_list(node.planned_dependencies,
                                 "ready node",
                                 node.node_name,
                                 "planned_dependencies",
                                 execution_nodes,
                                 diagnostics);
        validate_dependency_list(node.satisfied_dependencies,
                                 "ready node",
                                 node.node_name,
                                 "satisfied_dependencies",
                                 execution_nodes,
                                 diagnostics);
        validate_dependency_subset(node.satisfied_dependencies,
                                   node.planned_dependencies,
                                   "ready node",
                                   node.node_name,
                                   "satisfied dependency",
                                   diagnostics);
        validate_capability_bindings(
            node.capability_bindings, "ready node", node.node_name, diagnostics);
    }

    for (const auto &node : snapshot.blocked_nodes) {
        static_cast<void>(insert_unique_node_name(
            node.node_name, seen_snapshot_nodes, diagnostics, "blocked_nodes"));
        static_cast<void>(blocked_node_names.insert(node.node_name));

        if (node.node_name.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node must not have empty node_name");
            continue;
        }

        if (node.target.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' must not have empty target");
        }

        if (!execution_nodes.contains(node.node_name)) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' does not exist in execution_order");
        } else if (node.execution_index.has_value() &&
                   *node.execution_index >= snapshot.execution_order.size()) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' has execution_index outside execution_order");
        } else if (node.execution_index.has_value() &&
                   snapshot.execution_order[*node.execution_index] != node.node_name) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' has execution_index that does not match execution_order");
        }

        for (const auto &completed_node : snapshot.cursor.completed_prefix) {
            if (completed_node == node.node_name) {
                emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                                  "' cannot already be in completed_prefix");
                break;
            }
        }

        validate_dependency_list(node.planned_dependencies,
                                 "blocked node",
                                 node.node_name,
                                 "planned_dependencies",
                                 execution_nodes,
                                 diagnostics);
        validate_dependency_list(node.missing_dependencies,
                                 "blocked node",
                                 node.node_name,
                                 "missing_dependencies",
                                 execution_nodes,
                                 diagnostics);
        validate_dependency_subset(node.missing_dependencies,
                                   node.planned_dependencies,
                                   "blocked node",
                                   node.node_name,
                                   "missing dependency",
                                   diagnostics);

        if (node.blocking_failure_summary.has_value()) {
            validate_failure_summary(
                *node.blocking_failure_summary,
                "blocked node '" + node.node_name + "'",
                diagnostics);
        }

        if (node.blocked_reason == SchedulerBlockedReasonKind::WaitingOnDependencies) {
            if (node.missing_dependencies.empty()) {
                emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                                  "' waiting_on_dependencies requires missing_dependencies");
            }
            if (node.blocking_failure_summary.has_value()) {
                emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                                  "' waiting_on_dependencies must not carry blocking_failure_summary");
            }
        }

        if (node.blocked_reason == SchedulerBlockedReasonKind::WorkflowTerminalFailure &&
            node.may_become_ready) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' cannot be marked may_become_ready under workflow terminal failure");
        }

        if (node.blocked_reason == SchedulerBlockedReasonKind::WorkflowTerminalFailure &&
            !node.blocking_failure_summary.has_value()) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' workflow terminal failure requires blocking_failure_summary");
        }

        if (node.blocked_reason == SchedulerBlockedReasonKind::UpstreamPartial &&
            node.may_become_ready) {
            emit_validation_error(diagnostics, "scheduler snapshot blocked node '" + node.node_name +
                              "' cannot be marked may_become_ready under upstream partial");
        }
    }

    switch (snapshot.workflow_status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        if (snapshot.snapshot_status != SchedulerSnapshotStatus::TerminalCompleted) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot completed workflow must use terminal_completed status");
        }
        if (snapshot.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot completed workflow must not carry workflow_failure_summary");
        }
        break;
    case runtime_session::WorkflowSessionStatus::Failed:
        if (snapshot.snapshot_status != SchedulerSnapshotStatus::TerminalFailed) {
            emit_validation_error(diagnostics, "scheduler snapshot failed workflow must use terminal_failed status");
        }
        if (!snapshot.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot failed workflow must carry workflow_failure_summary");
        }
        break;
    case runtime_session::WorkflowSessionStatus::Partial:
        if (snapshot.snapshot_status == SchedulerSnapshotStatus::TerminalCompleted ||
            snapshot.snapshot_status == SchedulerSnapshotStatus::TerminalFailed) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot partial workflow cannot use completed/failed terminal status");
        }
        if (snapshot.workflow_failure_summary.has_value()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot partial workflow must not carry workflow_failure_summary");
        }
        break;
    }

    switch (snapshot.snapshot_status) {
    case SchedulerSnapshotStatus::Runnable:
        if (snapshot.workflow_status != runtime_session::WorkflowSessionStatus::Partial) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot runnable status requires partial workflow_status");
        }
        if (snapshot.ready_nodes.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot runnable status requires at least one ready node");
        }
        if (snapshot.cursor.next_candidate_node_name.has_value() &&
            !ready_node_names.contains(*snapshot.cursor.next_candidate_node_name)) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot runnable status next candidate must reference a ready node");
        }
        break;
    case SchedulerSnapshotStatus::Waiting:
        if (snapshot.workflow_status != runtime_session::WorkflowSessionStatus::Partial) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot waiting status requires partial workflow_status");
        }
        if (!snapshot.ready_nodes.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot waiting status must not contain ready nodes");
        }
        if (snapshot.blocked_nodes.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot waiting status requires at least one blocked node");
        }
        if (snapshot.cursor.next_candidate_node_name.has_value() &&
            !blocked_node_names.contains(*snapshot.cursor.next_candidate_node_name)) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot waiting status next candidate must reference a blocked node");
        }
        break;
    case SchedulerSnapshotStatus::TerminalCompleted:
        if (snapshot.cursor.completed_prefix_size != snapshot.execution_order.size()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot terminal completed status requires fully completed prefix");
        }
        if (!snapshot.blocked_nodes.empty()) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot terminal completed status must not contain blocked nodes");
        }
        [[fallthrough]];
    case SchedulerSnapshotStatus::TerminalFailed:
        if (snapshot.snapshot_status == SchedulerSnapshotStatus::TerminalFailed &&
            snapshot.workflow_status != runtime_session::WorkflowSessionStatus::Failed) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot terminal failed status requires failed workflow_status");
        }
        [[fallthrough]];
    case SchedulerSnapshotStatus::TerminalPartial:
        if (snapshot.snapshot_status == SchedulerSnapshotStatus::TerminalPartial &&
            snapshot.workflow_status != runtime_session::WorkflowSessionStatus::Partial) {
            emit_validation_error(diagnostics, 
                "scheduler snapshot terminal partial status requires partial workflow_status");
        }
        if (!snapshot.ready_nodes.empty()) {
            emit_validation_error(diagnostics, "scheduler snapshot terminal status must not contain ready nodes");
        }
        if (snapshot.cursor.next_candidate_node_name.has_value()) {
            emit_validation_error(diagnostics, "scheduler snapshot terminal status must not contain next candidate");
        }
        break;
    }

    return result;
}

SchedulerSnapshotResult build_scheduler_snapshot(const handoff::ExecutionPlan &plan,
                                                 const runtime_session::RuntimeSession &session,
                                                 const execution_journal::ExecutionJournal &journal,
                                                 const replay_view::ReplayView &replay) {
    SchedulerSnapshotResult result{
        .snapshot = std::nullopt,
        .diagnostics = {},
    };

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);

    const auto session_validation = runtime_session::validate_runtime_session(session);
    result.diagnostics.append(session_validation.diagnostics);

    const auto journal_validation = execution_journal::validate_execution_journal(journal);
    result.diagnostics.append(journal_validation.diagnostics);

    const auto replay_validation = replay_view::validate_replay_view(replay);
    result.diagnostics.append(replay_validation.diagnostics);

    if (result.has_errors()) {
        return result;
    }

    if (session.source_execution_plan_format_version != plan.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap runtime session source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_execution_plan_format_version != plan.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_runtime_session_format_version != session.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_plan_format_version != plan.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view source_execution_plan_format_version does not match execution plan");
    }

    if (replay.source_runtime_session_format_version != session.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_journal_format_version != journal.format_version) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view source_execution_journal_format_version does not match execution journal");
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap runtime session source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, replay.source_package_identity)) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view source_package_identity does not match execution plan");
    }

    if (session.workflow_canonical_name != journal.workflow_canonical_name) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != replay.workflow_canonical_name) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view workflow_canonical_name does not match runtime session");
    }

    if (session.session_id != journal.session_id) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal session_id does not match runtime session");
    }

    if (session.session_id != replay.session_id) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view session_id does not match runtime session");
    }

    if (session.run_id != journal.run_id) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap execution journal run_id does not match runtime session");
    }

    if (session.run_id != replay.run_id) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view run_id does not match runtime session");
    }

    if (session.input_fixture != replay.input_fixture) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view input_fixture does not match runtime session");
    }

    if (session.workflow_status != replay.workflow_status) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view workflow_status does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, replay.workflow_failure_summary)) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view workflow_failure_summary does not match runtime session");
    }

    if (!replay.consistency.plan_matches_session || !replay.consistency.session_matches_journal ||
        !replay.consistency.journal_matches_execution_order) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view consistency must hold before building snapshot");
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        emit_bootstrap_error(result.diagnostics, "scheduler snapshot bootstrap workflow '" +
                                 session.workflow_canonical_name +
                                 "' does not exist in execution plan");
    }

    if (result.has_errors()) {
        return result;
    }

    std::vector<std::string> execution_order;
    execution_order.reserve(workflow->nodes.size());

    std::unordered_map<std::string, std::size_t> plan_node_indices;
    plan_node_indices.reserve(workflow->nodes.size());
    for (std::size_t index = 0; index < workflow->nodes.size(); ++index) {
        execution_order.push_back(workflow->nodes[index].name);
        plan_node_indices.emplace(workflow->nodes[index].name, index);
    }

    if (!is_prefix(replay.execution_order, execution_order)) {
        emit_bootstrap_error(result.diagnostics, 
            "scheduler snapshot bootstrap replay view execution_order must be a prefix of execution plan workflow order");
        return result;
    }

    std::unordered_map<std::string, const runtime_session::RuntimeSessionNode *> session_nodes;
    session_nodes.reserve(session.nodes.size());
    for (const auto &node : session.nodes) {
        session_nodes.emplace(node.node_name, &node);
    }

    std::vector<std::string> completed_prefix;
    completed_prefix.reserve(execution_order.size());
    for (const auto &node_name : execution_order) {
        const auto found = session_nodes.find(node_name);
        if (found == session_nodes.end()) {
            emit_bootstrap_error(result.diagnostics, "scheduler snapshot bootstrap execution plan node '" +
                                     node_name + "' does not exist in runtime session");
            return result;
        }

        if (found->second->status != runtime_session::NodeSessionStatus::Completed) {
            break;
        }

        completed_prefix.push_back(node_name);
    }

    std::unordered_set<std::string> completed_node_names(completed_prefix.begin(),
                                                         completed_prefix.end());

    std::vector<SchedulerReadyNode> ready_nodes;
    std::vector<SchedulerBlockedNode> blocked_nodes;
    ready_nodes.reserve(workflow->nodes.size());
    blocked_nodes.reserve(workflow->nodes.size());

    for (const auto &plan_node : workflow->nodes) {
        const auto session_found = session_nodes.find(plan_node.name);
        if (session_found == session_nodes.end()) {
            emit_bootstrap_error(result.diagnostics, "scheduler snapshot bootstrap execution plan node '" +
                                     plan_node.name + "' does not exist in runtime session");
            return result;
        }

        const auto &session_node = *session_found->second;
        const auto execution_index = plan_node_indices.at(plan_node.name);

        switch (session_node.status) {
        case runtime_session::NodeSessionStatus::Ready:
            ready_nodes.push_back(SchedulerReadyNode{
                .node_name = session_node.node_name,
                .target = session_node.target,
                .execution_index = execution_index,
                .planned_dependencies = plan_node.after,
                .satisfied_dependencies = session_node.satisfied_dependencies,
                .input_summary = session_node.input_summary,
                .capability_bindings = plan_node.capability_bindings,
            });
            break;
        case runtime_session::NodeSessionStatus::Blocked:
        case runtime_session::NodeSessionStatus::Skipped: {
            std::vector<std::string> missing_dependencies;
            if (session.workflow_status == runtime_session::WorkflowSessionStatus::Partial) {
                const std::unordered_set<std::string> satisfied_dependencies(
                    session_node.satisfied_dependencies.begin(),
                    session_node.satisfied_dependencies.end());
                for (const auto &dependency : plan_node.after) {
                    if (!satisfied_dependencies.contains(dependency) &&
                        !completed_node_names.contains(dependency)) {
                        missing_dependencies.push_back(dependency);
                    }
                }
            }

            SchedulerBlockedReasonKind blocked_reason =
                SchedulerBlockedReasonKind::WaitingOnDependencies;
            bool may_become_ready = true;
            std::optional<runtime_session::RuntimeFailureSummary> blocking_failure_summary =
                std::nullopt;

            if (session.workflow_status == runtime_session::WorkflowSessionStatus::Failed) {
                blocked_reason = SchedulerBlockedReasonKind::WorkflowTerminalFailure;
                may_become_ready = false;
                blocking_failure_summary = session.failure_summary;
                missing_dependencies.clear();
            } else if (missing_dependencies.empty()) {
                blocked_reason = SchedulerBlockedReasonKind::UpstreamPartial;
                may_become_ready = false;
            }

            blocked_nodes.push_back(SchedulerBlockedNode{
                .node_name = session_node.node_name,
                .target = session_node.target,
                .execution_index = execution_index,
                .planned_dependencies = plan_node.after,
                .missing_dependencies = std::move(missing_dependencies),
                .blocked_reason = blocked_reason,
                .may_become_ready = may_become_ready,
                .blocking_failure_summary = blocking_failure_summary,
            });
            break;
        }
        case runtime_session::NodeSessionStatus::Completed:
        case runtime_session::NodeSessionStatus::Failed:
            break;
        }
    }

    SchedulerSnapshot snapshot{
        .format_version = std::string(kSchedulerSnapshotFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_replay_view_format_version = replay.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = session.workflow_canonical_name,
        .session_id = session.session_id,
        .run_id = session.run_id,
        .input_fixture = session.input_fixture,
        .workflow_status = session.workflow_status,
        .snapshot_status = SchedulerSnapshotStatus::Waiting,
        .workflow_failure_summary = session.failure_summary,
        .execution_order = std::move(execution_order),
        .ready_nodes = std::move(ready_nodes),
        .blocked_nodes = std::move(blocked_nodes),
        .cursor =
            SchedulerCursor{
                .completed_prefix_size = completed_prefix.size(),
                .completed_prefix = std::move(completed_prefix),
                .next_candidate_node_name = std::nullopt,
                .checkpoint_friendly = true,
            },
    };

    switch (snapshot.workflow_status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        snapshot.snapshot_status = SchedulerSnapshotStatus::TerminalCompleted;
        break;
    case runtime_session::WorkflowSessionStatus::Failed:
        snapshot.snapshot_status = SchedulerSnapshotStatus::TerminalFailed;
        break;
    case runtime_session::WorkflowSessionStatus::Partial:
        snapshot.snapshot_status = snapshot.ready_nodes.empty() ? SchedulerSnapshotStatus::Waiting
                                                                : SchedulerSnapshotStatus::Runnable;
        break;
    }

    if (snapshot.snapshot_status == SchedulerSnapshotStatus::Runnable &&
        !snapshot.ready_nodes.empty()) {
        snapshot.cursor.next_candidate_node_name = snapshot.ready_nodes.front().node_name;
    } else if (snapshot.snapshot_status == SchedulerSnapshotStatus::Waiting &&
               !snapshot.blocked_nodes.empty()) {
        snapshot.cursor.next_candidate_node_name = snapshot.blocked_nodes.front().node_name;
    }

    const auto snapshot_validation = validate_scheduler_snapshot(snapshot);
    result.diagnostics.append(snapshot_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.snapshot = std::move(snapshot);
    return result;
}

} // namespace ahfl::scheduler_snapshot
