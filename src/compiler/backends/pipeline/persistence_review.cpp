#include "compiler/backends/pipeline/persistence_review.hpp"
#include "printer_helpers.hpp"
#include "compiler/backends/pipeline/review_helpers.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

using backend_printer::line;

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
    line(out, 0, "workflow_status " + pipeline_review::workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + pipeline_review::checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + pipeline_review::persistence_status_name(summary.persistence_status));
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

    pipeline_review::print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_persistence_blocker(out, 0, summary.persistence_blocker);
    pipeline_review::print_string_list(out, 0, "exportable_prefix", summary.exportable_prefix);
}

} // namespace ahfl
