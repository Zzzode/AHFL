#include "ahfl/backends/persistence_review.hpp"

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

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string basis_kind_name(persistence_descriptor::PersistenceBasisKind kind) {
    switch (kind) {
    case persistence_descriptor::PersistenceBasisKind::LocalPlanningOnly:
        return "local_planning_only";
    case persistence_descriptor::PersistenceBasisKind::StoreAdjacent:
        return "store_adjacent";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(persistence_descriptor::PersistenceBlockerKind kind) {
    switch (kind) {
    case persistence_descriptor::PersistenceBlockerKind::WaitingOnCheckpointState:
        return "waiting_on_checkpoint_state";
    case persistence_descriptor::PersistenceBlockerKind::MissingPlannedDurableIdentity:
        return "missing_planned_durable_identity";
    case persistence_descriptor::PersistenceBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case persistence_descriptor::PersistenceBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(persistence_descriptor::PersistenceReviewNextActionKind action) {
    switch (action) {
    case persistence_descriptor::PersistenceReviewNextActionKind::ExportPersistenceHandoff:
        return "export_persistence_handoff";
    case persistence_descriptor::PersistenceReviewNextActionKind::AwaitPersistenceReadiness:
        return "await_persistence_readiness";
    case persistence_descriptor::PersistenceReviewNextActionKind::WorkflowCompleted:
        return "workflow_completed";
    case persistence_descriptor::PersistenceReviewNextActionKind::InvestigateFailure:
        return "investigate_failure";
    case persistence_descriptor::PersistenceReviewNextActionKind::PreservePartialState:
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

void print_persistence_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<persistence_descriptor::PersistenceBlocker> &blocker) {
    line(out, indent_level, "persistence_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "node_name " + (blocker->node_name.has_value() ? *blocker->node_name : "none"));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
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

} // namespace

void print_persistence_review(const persistence_descriptor::PersistenceReviewSummary &summary,
                              std::ostream &out) {
    out << summary.format_version << '\n';
    line(
        out, 0, "source_descriptor_format " + summary.source_persistence_descriptor_format_version);
    line(out, 0, "source_record_format " + summary.source_checkpoint_record_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out, 0, "export_basis_kind " + basis_kind_name(summary.export_basis_kind));
    line(out, 0, std::string("export_ready ") + (summary.export_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "store_boundary " + summary.store_boundary_summary);
    line(out, 0, "export_preview " + summary.export_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_export_candidate " + (summary.next_export_candidate_node_name.has_value()
                                         ? *summary.next_export_candidate_node_name
                                         : "none"));

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_persistence_blocker(out, 0, summary.persistence_blocker);
    print_string_list(out, 0, "exportable_prefix", summary.exportable_prefix);
}

} // namespace ahfl
