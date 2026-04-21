#include "ahfl/store_import/review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ahfl::store_import {

namespace {

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "store import review summary");
}

void validate_staging_blocker(const StagingBlocker &blocker, DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        diagnostics.error().message("store import review summary staging_blocker message must not be empty").emit();
    }

    if (blocker.logical_artifact_name.has_value() && blocker.logical_artifact_name->empty()) {
        diagnostics.error().message("store import review summary staging_blocker logical_artifact_name must not be empty").emit();
    }
}

[[nodiscard]] std::string artifact_kind_name(StagingArtifactKind kind) {
    switch (kind) {
    case StagingArtifactKind::ExportManifest:
        return "export_manifest";
    case StagingArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case StagingArtifactKind::PersistenceReview:
        return "persistence_review";
    case StagingArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] StoreImportReviewNextActionKind
next_action_for_descriptor(const StoreImportDescriptor &descriptor) {
    switch (descriptor.descriptor_status) {
    case StoreImportDescriptorStatus::ReadyToImport:
        return StoreImportReviewNextActionKind::HandoffStoreImportDescriptor;
    case StoreImportDescriptorStatus::Blocked:
        return StoreImportReviewNextActionKind::AwaitStagingReadiness;
    case StoreImportDescriptorStatus::TerminalCompleted:
        return StoreImportReviewNextActionKind::ArchiveCompletedStoreImportState;
    case StoreImportDescriptorStatus::TerminalFailed:
        return StoreImportReviewNextActionKind::InvestigateStoreImportFailure;
    case StoreImportDescriptorStatus::TerminalPartial:
        return StoreImportReviewNextActionKind::PreservePartialStoreImportState;
    }

    return StoreImportReviewNextActionKind::AwaitStagingReadiness;
}

[[nodiscard]] std::string
store_boundary_summary_for_descriptor(const StoreImportDescriptor &descriptor) {
    switch (descriptor.descriptor_boundary_kind) {
    case StoreImportBoundaryKind::LocalStagingOnly:
        return "local staging handoff only; durable store adapter ABI not yet promised";
    case StoreImportBoundaryKind::AdapterConsumable:
        return "adapter-consumable staging shape available; durable store mutation ABI still not promised";
    }

    return "local staging handoff only; durable store adapter ABI not yet promised";
}

[[nodiscard]] std::string staging_preview_for_descriptor(
    const StoreImportDescriptor &descriptor) {
    switch (descriptor.descriptor_status) {
    case StoreImportDescriptorStatus::ReadyToImport:
        return "store import candidate '" + descriptor.store_import_candidate_identity +
               "' can be handed off with current staging set and planned durable identity '" +
               descriptor.planned_durable_identity + "'";
    case StoreImportDescriptorStatus::Blocked:
        if (descriptor.staging_blocker.has_value() &&
            descriptor.staging_blocker->logical_artifact_name.has_value()) {
            return "store import staging is waiting on artifact '" +
                   *descriptor.staging_blocker->logical_artifact_name +
                   "' before handoff can proceed";
        }
        if (descriptor.next_required_staging_artifact_kind.has_value()) {
            return "store import staging is waiting on artifact kind '" +
                   artifact_kind_name(*descriptor.next_required_staging_artifact_kind) +
                   "' before handoff can proceed";
        }
        return "store import staging is waiting on blockers before handoff can proceed";
    case StoreImportDescriptorStatus::TerminalCompleted:
        return "workflow already completed; store import staging is retained for archival review";
    case StoreImportDescriptorStatus::TerminalFailed:
        if (descriptor.workflow_failure_summary.has_value() &&
            descriptor.workflow_failure_summary->node_name.has_value()) {
            return "workflow failed at node '" +
                   *descriptor.workflow_failure_summary->node_name +
                   "'; store import staging handoff is closed";
        }
        return "workflow failed; store import staging handoff is closed";
    case StoreImportDescriptorStatus::TerminalPartial:
        return "partial workflow is retained as local staging handoff without durable store import";
    }

    return "store import staging state unavailable";
}

[[nodiscard]] std::string
next_step_recommendation_for_descriptor(const StoreImportDescriptor &descriptor) {
    switch (descriptor.descriptor_status) {
    case StoreImportDescriptorStatus::ReadyToImport:
        return "handoff current store import descriptor before future durable-store adapter work";
    case StoreImportDescriptorStatus::Blocked:
        if (descriptor.next_required_staging_artifact_kind.has_value()) {
            return "materialize required staging artifact kind '" +
                   artifact_kind_name(*descriptor.next_required_staging_artifact_kind) +
                   "' before advertising durable store handoff";
        }
        return "stabilize staging blockers before advertising durable store handoff";
    case StoreImportDescriptorStatus::TerminalCompleted:
        return "archive completed store import staging handoff; no further import action";
    case StoreImportDescriptorStatus::TerminalFailed:
        return "inspect workflow failure before planning durable store import";
    case StoreImportDescriptorStatus::TerminalPartial:
        return "preserve partial store import staging for inspection; do not advertise durable store import";
    }

    return "no store import action";
}

[[nodiscard]] std::vector<std::string>
staging_artifact_preview_for_descriptor(const StoreImportDescriptor &descriptor) {
    std::vector<std::string> preview;
    preview.reserve(descriptor.staging_artifact_set.entries.size());
    for (const auto &entry : descriptor.staging_artifact_set.entries) {
        preview.push_back(std::string(entry.required_for_import ? "required " : "optional ") +
                          artifact_kind_name(entry.artifact_kind) + " " +
                          entry.logical_artifact_name);
    }
    return preview;
}

} // namespace

StoreImportReviewSummaryValidationResult
validate_store_import_review_summary(const StoreImportReviewSummary &summary) {
    StoreImportReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kStoreImportReviewSummaryFormatVersion) {
        diagnostics.error().message("store import review summary format_version must be '" + std::string(kStoreImportReviewSummaryFormatVersion) + "'").emit();
    }

    if (summary.source_store_import_descriptor_format_version !=
        kStoreImportDescriptorFormatVersion) {
        diagnostics.error().message("store import review summary source_store_import_descriptor_format_version must be '" + std::string(kStoreImportDescriptorFormatVersion) + "'").emit();
    }

    if (summary.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        diagnostics.error().message("store import review summary source_export_manifest_format_version must be '" + std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'").emit();
    }

    if (summary.workflow_canonical_name.empty()) {
        diagnostics.error().message("store import review summary workflow_canonical_name must not be empty").emit();
    }

    if (summary.session_id.empty()) {
        diagnostics.error().message("store import review summary session_id must not be empty").emit();
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        diagnostics.error().message("store import review summary run_id must not be empty when present").emit();
    }

    if (summary.input_fixture.empty()) {
        diagnostics.error().message("store import review summary input_fixture must not be empty").emit();
    }

    if (summary.export_package_identity.empty()) {
        diagnostics.error().message("store import review summary export_package_identity must not be empty").emit();
    }

    if (summary.store_import_candidate_identity.empty()) {
        diagnostics.error().message("store import review summary store_import_candidate_identity must not be empty").emit();
    }

    if (summary.planned_durable_identity.empty()) {
        diagnostics.error().message("store import review summary planned_durable_identity must not be empty").emit();
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.staging_artifact_entry_count != summary.staging_artifact_preview.size()) {
        diagnostics.error().message("store import review summary staging_artifact_entry_count must match staging_artifact_preview length").emit();
    }

    for (const auto &entry : summary.staging_artifact_preview) {
        if (entry.empty()) {
            diagnostics.error().message("store import review summary staging_artifact_preview must not contain empty entries").emit();
            break;
        }
    }

    if (summary.next_required_staging_artifact_kind.has_value() && summary.import_ready) {
        diagnostics.error().message("store import review summary cannot contain next_required_staging_artifact_kind when import_ready is true").emit();
    }

    if (summary.staging_blocker.has_value()) {
        validate_staging_blocker(*summary.staging_blocker, diagnostics);
    }

    if (summary.import_ready && summary.staging_blocker.has_value()) {
        diagnostics.error().message("store import review summary cannot contain staging_blocker when import_ready is true").emit();
    }

    if (summary.store_boundary_summary.empty()) {
        diagnostics.error().message("store import review summary store_boundary_summary must not be empty").emit();
    }

    if (summary.staging_preview.empty()) {
        diagnostics.error().message("store import review summary staging_preview must not be empty").emit();
    }

    if (summary.next_step_recommendation.empty()) {
        diagnostics.error().message("store import review summary next_step_recommendation must not be empty").emit();
    }

    switch (summary.descriptor_status) {
    case StoreImportDescriptorStatus::ReadyToImport:
        if (summary.next_action !=
            StoreImportReviewNextActionKind::HandoffStoreImportDescriptor) {
            diagnostics.error().message("store import review summary ReadyToImport descriptor_status requires next_action handoff_store_import_descriptor").emit();
        }
        if (!summary.import_ready) {
            diagnostics.error().message("store import review summary ReadyToImport descriptor_status requires import_ready").emit();
        }
        break;
    case StoreImportDescriptorStatus::Blocked:
        if (summary.next_action != StoreImportReviewNextActionKind::AwaitStagingReadiness) {
            diagnostics.error().message("store import review summary Blocked descriptor_status requires next_action await_staging_readiness").emit();
        }
        if (summary.import_ready) {
            diagnostics.error().message("store import review summary Blocked descriptor_status cannot be import_ready").emit();
        }
        if (!summary.staging_blocker.has_value()) {
            diagnostics.error().message("store import review summary Blocked descriptor_status requires staging_blocker").emit();
        }
        break;
    case StoreImportDescriptorStatus::TerminalCompleted:
        if (summary.next_action !=
            StoreImportReviewNextActionKind::ArchiveCompletedStoreImportState) {
            diagnostics.error().message("store import review summary TerminalCompleted descriptor_status requires next_action archive_completed_store_import_state").emit();
        }
        if (!summary.import_ready) {
            diagnostics.error().message("store import review summary TerminalCompleted descriptor_status requires import_ready").emit();
        }
        if (summary.staging_blocker.has_value()) {
            diagnostics.error().message("store import review summary TerminalCompleted descriptor_status cannot contain staging_blocker").emit();
        }
        if (summary.next_required_staging_artifact_kind.has_value()) {
            diagnostics.error().message("store import review summary TerminalCompleted descriptor_status cannot contain next_required_staging_artifact_kind").emit();
        }
        break;
    case StoreImportDescriptorStatus::TerminalFailed:
        if (summary.next_action !=
            StoreImportReviewNextActionKind::InvestigateStoreImportFailure) {
            diagnostics.error().message("store import review summary TerminalFailed descriptor_status requires next_action investigate_store_import_failure").emit();
        }
        if (!summary.staging_blocker.has_value()) {
            diagnostics.error().message("store import review summary TerminalFailed descriptor_status requires staging_blocker").emit();
        }
        break;
    case StoreImportDescriptorStatus::TerminalPartial:
        if (summary.next_action !=
            StoreImportReviewNextActionKind::PreservePartialStoreImportState) {
            diagnostics.error().message("store import review summary TerminalPartial descriptor_status requires next_action preserve_partial_store_import_state").emit();
        }
        if (!summary.staging_blocker.has_value()) {
            diagnostics.error().message("store import review summary TerminalPartial descriptor_status requires staging_blocker").emit();
        }
        break;
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error().message("store import review summary completed workflow_status requires TerminalCompleted descriptor_status").emit();
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error().message("store import review summary failed workflow_status requires TerminalFailed descriptor_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error().message("store import review summary TerminalCompleted checkpoint_status requires TerminalCompleted descriptor_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error().message("store import review summary TerminalFailed checkpoint_status requires TerminalFailed descriptor_status").emit();
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error().message("store import review summary TerminalPartial checkpoint_status requires TerminalPartial descriptor_status").emit();
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error().message("store import review summary TerminalCompleted persistence_status requires TerminalCompleted descriptor_status").emit();
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error().message("store import review summary TerminalFailed persistence_status requires TerminalFailed descriptor_status").emit();
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error().message("store import review summary TerminalPartial persistence_status requires TerminalPartial descriptor_status").emit();
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        summary.descriptor_status != StoreImportDescriptorStatus::ReadyToImport &&
        summary.descriptor_status != StoreImportDescriptorStatus::Blocked &&
        summary.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error().message("store import review summary partial workflow_status must map to ReadyToImport, Blocked, or TerminalPartial descriptor_status").emit();
    }

    return result;
}

StoreImportReviewSummaryResult
build_store_import_review_summary(const StoreImportDescriptor &descriptor) {
    StoreImportReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_store_import_descriptor(descriptor);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    StoreImportReviewSummary summary{
        .format_version = std::string(kStoreImportReviewSummaryFormatVersion),
        .source_store_import_descriptor_format_version = descriptor.format_version,
        .source_export_manifest_format_version = descriptor.source_export_manifest_format_version,
        .workflow_canonical_name = descriptor.workflow_canonical_name,
        .session_id = descriptor.session_id,
        .run_id = descriptor.run_id,
        .input_fixture = descriptor.input_fixture,
        .workflow_status = descriptor.workflow_status,
        .checkpoint_status = descriptor.checkpoint_status,
        .persistence_status = descriptor.persistence_status,
        .manifest_status = descriptor.manifest_status,
        .descriptor_status = descriptor.descriptor_status,
        .workflow_failure_summary = descriptor.workflow_failure_summary,
        .export_package_identity = descriptor.export_package_identity,
        .store_import_candidate_identity = descriptor.store_import_candidate_identity,
        .planned_durable_identity = descriptor.planned_durable_identity,
        .descriptor_boundary_kind = descriptor.descriptor_boundary_kind,
        .staging_artifact_entry_count = descriptor.staging_artifact_set.entry_count,
        .staging_artifact_preview = staging_artifact_preview_for_descriptor(descriptor),
        .import_ready = descriptor.import_ready,
        .next_required_staging_artifact_kind = descriptor.next_required_staging_artifact_kind,
        .staging_blocker = descriptor.staging_blocker,
        .next_action = next_action_for_descriptor(descriptor),
        .store_boundary_summary = store_boundary_summary_for_descriptor(descriptor),
        .staging_preview = staging_preview_for_descriptor(descriptor),
        .next_step_recommendation = next_step_recommendation_for_descriptor(descriptor),
    };

    const auto summary_validation = validate_store_import_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::store_import
