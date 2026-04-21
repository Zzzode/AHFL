#include "ahfl/persistence_descriptor/review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ahfl::persistence_descriptor {

namespace {

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "persistence review summary");
}

void validate_persistence_blocker(const PersistenceBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        diagnostics.error().message("persistence review summary persistence_blocker message must not be empty").emit();
    }

    if (blocker.node_name.has_value() && blocker.node_name->empty()) {
        diagnostics.error().message("persistence review summary persistence_blocker node_name must not be empty").emit();
    }
}

[[nodiscard]] PersistenceReviewNextActionKind
next_action_for_descriptor(const CheckpointPersistenceDescriptor &descriptor) {
    switch (descriptor.persistence_status) {
    case PersistenceDescriptorStatus::ReadyToExport:
        return PersistenceReviewNextActionKind::ExportPersistenceHandoff;
    case PersistenceDescriptorStatus::Blocked:
        return PersistenceReviewNextActionKind::AwaitPersistenceReadiness;
    case PersistenceDescriptorStatus::TerminalCompleted:
        return PersistenceReviewNextActionKind::WorkflowCompleted;
    case PersistenceDescriptorStatus::TerminalFailed:
        return PersistenceReviewNextActionKind::InvestigateFailure;
    case PersistenceDescriptorStatus::TerminalPartial:
        return PersistenceReviewNextActionKind::PreservePartialState;
    }

    return PersistenceReviewNextActionKind::AwaitPersistenceReadiness;
}

[[nodiscard]] std::string
store_boundary_summary_for_descriptor(const CheckpointPersistenceDescriptor &descriptor) {
    switch (descriptor.export_basis_kind) {
    case PersistenceBasisKind::LocalPlanningOnly:
        return "local persistence planning only; durable store mutation ABI not yet promised";
    case PersistenceBasisKind::StoreAdjacent:
        return "store-adjacent persistence shape available; durable store mutation ABI still not promised";
    }

    return "local persistence planning only; durable store mutation ABI not yet promised";
}

[[nodiscard]] std::string
export_preview_for_descriptor(const CheckpointPersistenceDescriptor &descriptor) {
    switch (descriptor.persistence_status) {
    case PersistenceDescriptorStatus::ReadyToExport:
        if (descriptor.cursor.next_export_candidate_node_name.has_value()) {
            return "descriptor can export current prefix before node '" +
                   *descriptor.cursor.next_export_candidate_node_name +
                   "' with planned durable identity '" + descriptor.planned_durable_identity + "'";
        }
        return "descriptor can export current prefix with planned durable identity '" +
               descriptor.planned_durable_identity + "'";
    case PersistenceDescriptorStatus::Blocked:
        if (descriptor.persistence_blocker.has_value() &&
            descriptor.persistence_blocker->kind ==
                PersistenceBlockerKind::MissingPlannedDurableIdentity) {
            return "descriptor cannot export until planned durable identity is stable";
        }
        if (descriptor.persistence_blocker.has_value() &&
            descriptor.persistence_blocker->node_name.has_value()) {
            return "descriptor is waiting on node '" +
                   *descriptor.persistence_blocker->node_name +
                   "' before export can proceed";
        }
        return "descriptor is waiting on checkpoint state before export can proceed";
    case PersistenceDescriptorStatus::TerminalCompleted:
        return "workflow already completed; completed exportable prefix is retained for archival review";
    case PersistenceDescriptorStatus::TerminalFailed:
        if (descriptor.workflow_failure_summary.has_value() &&
            descriptor.workflow_failure_summary->node_name.has_value()) {
            return "workflow failed at node '" +
                   *descriptor.workflow_failure_summary->node_name +
                   "'; export is closed for current descriptor";
        }
        return "workflow failed; export is closed for current descriptor";
    case PersistenceDescriptorStatus::TerminalPartial:
        return "partial workflow is retained as local persistence handoff without export";
    }

    return "persistence export state unavailable";
}

[[nodiscard]] std::string
next_step_recommendation_for_descriptor(const CheckpointPersistenceDescriptor &descriptor) {
    switch (descriptor.persistence_status) {
    case PersistenceDescriptorStatus::ReadyToExport:
        return "export current persistence handoff before future durable-store work";
    case PersistenceDescriptorStatus::Blocked:
        if (descriptor.persistence_blocker.has_value() &&
            descriptor.persistence_blocker->kind ==
                PersistenceBlockerKind::MissingPlannedDurableIdentity) {
            return "stabilize planned durable identity before advertising export";
        }
        if (descriptor.persistence_blocker.has_value() &&
            descriptor.persistence_blocker->node_name.has_value()) {
            return "wait until node '" + *descriptor.persistence_blocker->node_name +
                   "' becomes export-ready";
        }
        return "wait until persistence export blockers clear";
    case PersistenceDescriptorStatus::TerminalCompleted:
        return "archive completed persistence handoff; no further export action";
    case PersistenceDescriptorStatus::TerminalFailed:
        return "inspect workflow failure before planning durable export";
    case PersistenceDescriptorStatus::TerminalPartial:
        return "preserve partial persistence handoff for inspection; do not advertise durable export";
    }

    return "no persistence action";
}

} // namespace

PersistenceReviewSummaryValidationResult
validate_persistence_review_summary(const PersistenceReviewSummary &summary) {
    PersistenceReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kPersistenceReviewSummaryFormatVersion) {
        diagnostics.error().message("persistence review summary format_version must be '" + std::string(kPersistenceReviewSummaryFormatVersion) + "'").emit();
    }

    if (summary.source_persistence_descriptor_format_version !=
        kPersistenceDescriptorFormatVersion) {
        diagnostics.error().message("persistence review summary source_persistence_descriptor_format_version must be '" + std::string(kPersistenceDescriptorFormatVersion) + "'").emit();
    }

    if (summary.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error().message("persistence review summary source_checkpoint_record_format_version must be '" + std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'").emit();
    }

    if (summary.workflow_canonical_name.empty()) {
        diagnostics.error().message("persistence review summary workflow_canonical_name must not be empty").emit();
    }

    if (summary.session_id.empty()) {
        diagnostics.error().message("persistence review summary session_id must not be empty").emit();
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        diagnostics.error().message("persistence review summary run_id must not be empty when present").emit();
    }

    if (summary.input_fixture.empty()) {
        diagnostics.error().message("persistence review summary input_fixture must not be empty").emit();
    }

    if (summary.planned_durable_identity.empty()) {
        diagnostics.error().message("persistence review summary planned_durable_identity must not be empty").emit();
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.exportable_prefix_size != summary.exportable_prefix.size()) {
        diagnostics.error().message("persistence review summary exportable_prefix_size must match exportable_prefix length").emit();
    }

    if (summary.next_export_candidate_node_name.has_value() &&
        summary.next_export_candidate_node_name->empty()) {
        diagnostics.error().message("persistence review summary next_export_candidate_node_name must not be empty").emit();
    }

    if (summary.persistence_blocker.has_value()) {
        validate_persistence_blocker(*summary.persistence_blocker, diagnostics);
    }

    if (summary.export_ready && summary.persistence_blocker.has_value()) {
        diagnostics.error().message("persistence review summary cannot contain persistence_blocker when export_ready is true").emit();
    }

    if (summary.store_boundary_summary.empty()) {
        diagnostics.error().message("persistence review summary store_boundary_summary must not be empty").emit();
    }

    if (summary.export_preview.empty()) {
        diagnostics.error().message("persistence review summary export_preview must not be empty").emit();
    }

    if (summary.next_step_recommendation.empty()) {
        diagnostics.error().message("persistence review summary next_step_recommendation must not be empty").emit();
    }

    switch (summary.persistence_status) {
    case PersistenceDescriptorStatus::ReadyToExport:
        if (summary.next_action !=
            PersistenceReviewNextActionKind::ExportPersistenceHandoff) {
            diagnostics.error().message("persistence review summary ReadyToExport persistence_status requires next_action export_persistence_handoff").emit();
        }
        if (!summary.export_ready) {
            diagnostics.error().message("persistence review summary ReadyToExport persistence_status requires export_ready").emit();
        }
        if (!summary.next_export_candidate_node_name.has_value()) {
            diagnostics.error().message("persistence review summary ReadyToExport persistence_status requires next_export_candidate_node_name").emit();
        }
        break;
    case PersistenceDescriptorStatus::Blocked:
        if (summary.next_action !=
            PersistenceReviewNextActionKind::AwaitPersistenceReadiness) {
            diagnostics.error().message("persistence review summary Blocked persistence_status requires next_action await_persistence_readiness").emit();
        }
        if (summary.export_ready) {
            diagnostics.error().message("persistence review summary Blocked persistence_status cannot be export_ready").emit();
        }
        if (!summary.persistence_blocker.has_value()) {
            diagnostics.error().message("persistence review summary Blocked persistence_status requires persistence_blocker").emit();
        }
        break;
    case PersistenceDescriptorStatus::TerminalCompleted:
        if (summary.next_action != PersistenceReviewNextActionKind::WorkflowCompleted) {
            diagnostics.error().message("persistence review summary TerminalCompleted persistence_status requires next_action workflow_completed").emit();
        }
        if (summary.persistence_blocker.has_value()) {
            diagnostics.error().message("persistence review summary TerminalCompleted persistence_status cannot contain persistence_blocker").emit();
        }
        if (summary.next_export_candidate_node_name.has_value()) {
            diagnostics.error().message("persistence review summary TerminalCompleted persistence_status cannot contain next_export_candidate_node_name").emit();
        }
        break;
    case PersistenceDescriptorStatus::TerminalFailed:
        if (summary.next_action != PersistenceReviewNextActionKind::InvestigateFailure) {
            diagnostics.error().message("persistence review summary TerminalFailed persistence_status requires next_action investigate_failure").emit();
        }
        if (!summary.persistence_blocker.has_value()) {
            diagnostics.error().message("persistence review summary TerminalFailed persistence_status requires persistence_blocker").emit();
        }
        break;
    case PersistenceDescriptorStatus::TerminalPartial:
        if (summary.next_action != PersistenceReviewNextActionKind::PreservePartialState) {
            diagnostics.error().message("persistence review summary TerminalPartial persistence_status requires next_action preserve_partial_state").emit();
        }
        if (!summary.persistence_blocker.has_value()) {
            diagnostics.error().message("persistence review summary TerminalPartial persistence_status requires persistence_blocker").emit();
        }
        break;
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalCompleted) {
        diagnostics.error().message("persistence review summary completed workflow_status requires TerminalCompleted persistence_status").emit();
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalFailed) {
        diagnostics.error().message("persistence review summary failed workflow_status requires TerminalFailed persistence_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalCompleted) {
        diagnostics.error().message("persistence review summary TerminalCompleted checkpoint_status requires TerminalCompleted persistence_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalFailed) {
        diagnostics.error().message("persistence review summary TerminalFailed checkpoint_status requires TerminalFailed persistence_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalPartial) {
        diagnostics.error().message("persistence review summary TerminalPartial checkpoint_status requires TerminalPartial persistence_status").emit();
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        summary.persistence_status != PersistenceDescriptorStatus::ReadyToExport &&
        summary.persistence_status != PersistenceDescriptorStatus::Blocked &&
        summary.persistence_status != PersistenceDescriptorStatus::TerminalPartial) {
        diagnostics.error().message("persistence review summary partial workflow_status must map to ReadyToExport, Blocked, or TerminalPartial persistence_status").emit();
    }

    return result;
}

PersistenceReviewSummaryResult
build_persistence_review_summary(const CheckpointPersistenceDescriptor &descriptor) {
    PersistenceReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_persistence_descriptor(descriptor);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    PersistenceReviewSummary summary{
        .format_version = std::string(kPersistenceReviewSummaryFormatVersion),
        .source_persistence_descriptor_format_version = descriptor.format_version,
        .source_checkpoint_record_format_version =
            descriptor.source_checkpoint_record_format_version,
        .workflow_canonical_name = descriptor.workflow_canonical_name,
        .session_id = descriptor.session_id,
        .run_id = descriptor.run_id,
        .input_fixture = descriptor.input_fixture,
        .workflow_status = descriptor.workflow_status,
        .checkpoint_status = descriptor.checkpoint_status,
        .persistence_status = descriptor.persistence_status,
        .workflow_failure_summary = descriptor.workflow_failure_summary,
        .planned_durable_identity = descriptor.planned_durable_identity,
        .export_basis_kind = descriptor.export_basis_kind,
        .exportable_prefix_size = descriptor.cursor.exportable_prefix_size,
        .exportable_prefix = descriptor.cursor.exportable_prefix,
        .next_export_candidate_node_name = descriptor.cursor.next_export_candidate_node_name,
        .export_ready = descriptor.cursor.export_ready,
        .persistence_blocker = descriptor.persistence_blocker,
        .next_action = next_action_for_descriptor(descriptor),
        .store_boundary_summary = store_boundary_summary_for_descriptor(descriptor),
        .export_preview = export_preview_for_descriptor(descriptor),
        .next_step_recommendation = next_step_recommendation_for_descriptor(descriptor),
    };

    const auto validation_result = validate_persistence_review_summary(summary);
    result.diagnostics.append(validation_result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::persistence_descriptor
