#include "ahfl/persistence_export/review.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ahfl::persistence_export {

namespace {

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error("persistence export review summary " + std::string(owner_name) +
                          " contains failure summary with empty message");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error("persistence export review summary " + std::string(owner_name) +
                          " contains failure summary with empty node_name");
    }
}

void validate_store_import_blocker(const StoreImportBlocker &blocker,
                                   DiagnosticBag &diagnostics) {
    if (blocker.message.empty()) {
        diagnostics.error(
            "persistence export review summary store_import_blocker message must not be empty");
    }

    if (blocker.logical_artifact_name.has_value() &&
        blocker.logical_artifact_name->empty()) {
        diagnostics.error(
            "persistence export review summary store_import_blocker logical_artifact_name must not be empty");
    }
}

[[nodiscard]] std::string artifact_kind_name(ExportArtifactKind kind) {
    switch (kind) {
    case ExportArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case ExportArtifactKind::PersistenceReview:
        return "persistence_review";
    case ExportArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] PersistenceExportReviewNextActionKind
next_action_for_manifest(const PersistenceExportManifest &manifest) {
    switch (manifest.manifest_status) {
    case PersistenceExportManifestStatus::ReadyToImport:
        return PersistenceExportReviewNextActionKind::HandoffExportPackage;
    case PersistenceExportManifestStatus::Blocked:
        return PersistenceExportReviewNextActionKind::AwaitManifestReadiness;
    case PersistenceExportManifestStatus::TerminalCompleted:
        return PersistenceExportReviewNextActionKind::ArchiveCompletedExportPackage;
    case PersistenceExportManifestStatus::TerminalFailed:
        return PersistenceExportReviewNextActionKind::InvestigateExportFailure;
    case PersistenceExportManifestStatus::TerminalPartial:
        return PersistenceExportReviewNextActionKind::PreservePartialExportState;
    }

    return PersistenceExportReviewNextActionKind::AwaitManifestReadiness;
}

[[nodiscard]] std::string
store_boundary_summary_for_manifest(const PersistenceExportManifest &manifest) {
    switch (manifest.manifest_boundary_kind) {
    case ManifestBoundaryKind::LocalBundleOnly:
        return "local export bundle only; durable store import ABI not yet promised";
    case ManifestBoundaryKind::StoreImportAdjacent:
        return "store-import-adjacent export shape available; durable store import ABI still not promised";
    }

    return "local export bundle only; durable store import ABI not yet promised";
}

[[nodiscard]] std::string import_preview_for_manifest(const PersistenceExportManifest &manifest) {
    switch (manifest.manifest_status) {
    case PersistenceExportManifestStatus::ReadyToImport:
        return "export package can be handed off with current artifact bundle and planned durable identity '" +
               manifest.planned_durable_identity + "'";
    case PersistenceExportManifestStatus::Blocked:
        if (manifest.store_import_blocker.has_value() &&
            manifest.store_import_blocker->logical_artifact_name.has_value()) {
            return "export package is waiting on artifact '" +
                   *manifest.store_import_blocker->logical_artifact_name +
                   "' before handoff can proceed";
        }
        if (manifest.next_required_artifact_kind.has_value()) {
            return "export package is waiting on artifact kind '" +
                   artifact_kind_name(*manifest.next_required_artifact_kind) +
                   "' before handoff can proceed";
        }
        return "export package is waiting on manifest blockers before handoff can proceed";
    case PersistenceExportManifestStatus::TerminalCompleted:
        return "workflow already completed; export package bundle is retained for archival review";
    case PersistenceExportManifestStatus::TerminalFailed:
        if (manifest.workflow_failure_summary.has_value() &&
            manifest.workflow_failure_summary->node_name.has_value()) {
            return "workflow failed at node '" +
                   *manifest.workflow_failure_summary->node_name +
                   "'; export package handoff is closed";
        }
        return "workflow failed; export package handoff is closed";
    case PersistenceExportManifestStatus::TerminalPartial:
        return "partial workflow is retained as local export package handoff without store import";
    }

    return "export package import state unavailable";
}

[[nodiscard]] std::string
next_step_recommendation_for_manifest(const PersistenceExportManifest &manifest) {
    switch (manifest.manifest_status) {
    case PersistenceExportManifestStatus::ReadyToImport:
        return "handoff current export package before future durable-store adapter work";
    case PersistenceExportManifestStatus::Blocked:
        if (manifest.next_required_artifact_kind.has_value()) {
            return "materialize required artifact kind '" +
                   artifact_kind_name(*manifest.next_required_artifact_kind) +
                   "' before advertising store import";
        }
        return "stabilize export package blockers before advertising store import";
    case PersistenceExportManifestStatus::TerminalCompleted:
        return "archive completed export package handoff; no further import action";
    case PersistenceExportManifestStatus::TerminalFailed:
        return "inspect workflow failure before planning store import";
    case PersistenceExportManifestStatus::TerminalPartial:
        return "preserve partial export package handoff for inspection; do not advertise store import";
    }

    return "no export package action";
}

[[nodiscard]] std::vector<std::string>
artifact_bundle_preview_for_manifest(const PersistenceExportManifest &manifest) {
    std::vector<std::string> preview;
    preview.reserve(manifest.artifact_bundle.entries.size());
    for (const auto &entry : manifest.artifact_bundle.entries) {
        preview.push_back(artifact_kind_name(entry.artifact_kind) + " " +
                          entry.logical_artifact_name);
    }
    return preview;
}

} // namespace

PersistenceExportReviewSummaryValidationResult
validate_persistence_export_review_summary(const PersistenceExportReviewSummary &summary) {
    PersistenceExportReviewSummaryValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (summary.format_version != kPersistenceExportReviewSummaryFormatVersion) {
        diagnostics.error("persistence export review summary format_version must be '" +
                          std::string(kPersistenceExportReviewSummaryFormatVersion) + "'");
    }

    if (summary.source_export_manifest_format_version !=
        kPersistenceExportManifestFormatVersion) {
        diagnostics.error(
            "persistence export review summary source_export_manifest_format_version must be '" +
            std::string(kPersistenceExportManifestFormatVersion) + "'");
    }

    if (summary.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        diagnostics.error(
            "persistence export review summary source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (summary.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "persistence export review summary source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (summary.workflow_canonical_name.empty()) {
        diagnostics.error(
            "persistence export review summary workflow_canonical_name must not be empty");
    }

    if (summary.session_id.empty()) {
        diagnostics.error("persistence export review summary session_id must not be empty");
    }

    if (summary.run_id.has_value() && summary.run_id->empty()) {
        diagnostics.error("persistence export review summary run_id must not be empty when present");
    }

    if (summary.input_fixture.empty()) {
        diagnostics.error("persistence export review summary input_fixture must not be empty");
    }

    if (summary.export_package_identity.empty()) {
        diagnostics.error(
            "persistence export review summary export_package_identity must not be empty");
    }

    if (summary.planned_durable_identity.empty()) {
        diagnostics.error(
            "persistence export review summary planned_durable_identity must not be empty");
    }

    if (summary.workflow_failure_summary.has_value()) {
        validate_failure_summary(*summary.workflow_failure_summary, "workflow", diagnostics);
    }

    if (summary.artifact_bundle_entry_count != summary.artifact_bundle_preview.size()) {
        diagnostics.error(
            "persistence export review summary artifact_bundle_entry_count must match artifact_bundle_preview length");
    }

    for (const auto &entry : summary.artifact_bundle_preview) {
        if (entry.empty()) {
            diagnostics.error(
                "persistence export review summary artifact_bundle_preview must not contain empty entries");
            break;
        }
    }

    if (summary.next_required_artifact_kind.has_value() && summary.manifest_ready) {
        diagnostics.error(
            "persistence export review summary cannot contain next_required_artifact_kind when manifest_ready is true");
    }

    if (summary.store_import_blocker.has_value()) {
        validate_store_import_blocker(*summary.store_import_blocker, diagnostics);
    }

    if (summary.manifest_ready && summary.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export review summary cannot contain store_import_blocker when manifest_ready is true");
    }

    if (summary.store_boundary_summary.empty()) {
        diagnostics.error(
            "persistence export review summary store_boundary_summary must not be empty");
    }

    if (summary.import_preview.empty()) {
        diagnostics.error("persistence export review summary import_preview must not be empty");
    }

    if (summary.next_step_recommendation.empty()) {
        diagnostics.error(
            "persistence export review summary next_step_recommendation must not be empty");
    }

    switch (summary.manifest_status) {
    case PersistenceExportManifestStatus::ReadyToImport:
        if (summary.next_action !=
            PersistenceExportReviewNextActionKind::HandoffExportPackage) {
            diagnostics.error(
                "persistence export review summary ReadyToImport manifest_status requires next_action handoff_export_package");
        }
        if (!summary.manifest_ready) {
            diagnostics.error(
                "persistence export review summary ReadyToImport manifest_status requires manifest_ready");
        }
        break;
    case PersistenceExportManifestStatus::Blocked:
        if (summary.next_action !=
            PersistenceExportReviewNextActionKind::AwaitManifestReadiness) {
            diagnostics.error(
                "persistence export review summary Blocked manifest_status requires next_action await_manifest_readiness");
        }
        if (summary.manifest_ready) {
            diagnostics.error(
                "persistence export review summary Blocked manifest_status cannot be manifest_ready");
        }
        if (!summary.store_import_blocker.has_value()) {
            diagnostics.error(
                "persistence export review summary Blocked manifest_status requires store_import_blocker");
        }
        break;
    case PersistenceExportManifestStatus::TerminalCompleted:
        if (summary.next_action !=
            PersistenceExportReviewNextActionKind::ArchiveCompletedExportPackage) {
            diagnostics.error(
                "persistence export review summary TerminalCompleted manifest_status requires next_action archive_completed_export_package");
        }
        if (!summary.manifest_ready) {
            diagnostics.error(
                "persistence export review summary TerminalCompleted manifest_status requires manifest_ready");
        }
        if (summary.store_import_blocker.has_value()) {
            diagnostics.error(
                "persistence export review summary TerminalCompleted manifest_status cannot contain store_import_blocker");
        }
        if (summary.next_required_artifact_kind.has_value()) {
            diagnostics.error(
                "persistence export review summary TerminalCompleted manifest_status cannot contain next_required_artifact_kind");
        }
        break;
    case PersistenceExportManifestStatus::TerminalFailed:
        if (summary.next_action !=
            PersistenceExportReviewNextActionKind::InvestigateExportFailure) {
            diagnostics.error(
                "persistence export review summary TerminalFailed manifest_status requires next_action investigate_export_failure");
        }
        if (!summary.store_import_blocker.has_value()) {
            diagnostics.error(
                "persistence export review summary TerminalFailed manifest_status requires store_import_blocker");
        }
        break;
    case PersistenceExportManifestStatus::TerminalPartial:
        if (summary.next_action !=
            PersistenceExportReviewNextActionKind::PreservePartialExportState) {
            diagnostics.error(
                "persistence export review summary TerminalPartial manifest_status requires next_action preserve_partial_export_state");
        }
        if (!summary.store_import_blocker.has_value()) {
            diagnostics.error(
                "persistence export review summary TerminalPartial manifest_status requires store_import_blocker");
        }
        break;
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export review summary completed workflow_status requires TerminalCompleted manifest_status");
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export review summary failed workflow_status requires TerminalFailed manifest_status");
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export review summary TerminalCompleted checkpoint_status requires TerminalCompleted manifest_status");
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export review summary TerminalFailed checkpoint_status requires TerminalFailed manifest_status");
    }

    if (summary.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export review summary TerminalPartial checkpoint_status requires TerminalPartial manifest_status");
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export review summary TerminalCompleted persistence_status requires TerminalCompleted manifest_status");
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export review summary TerminalFailed persistence_status requires TerminalFailed manifest_status");
    }

    if (summary.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export review summary TerminalPartial persistence_status requires TerminalPartial manifest_status");
    }

    if (summary.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        summary.manifest_status != PersistenceExportManifestStatus::ReadyToImport &&
        summary.manifest_status != PersistenceExportManifestStatus::Blocked &&
        summary.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export review summary partial workflow_status must map to ReadyToImport, Blocked, or TerminalPartial manifest_status");
    }

    return result;
}

PersistenceExportReviewSummaryResult
build_persistence_export_review_summary(const PersistenceExportManifest &manifest) {
    PersistenceExportReviewSummaryResult result{
        .summary = std::nullopt,
        .diagnostics = {},
    };

    const auto validation = validate_persistence_export_manifest(manifest);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    PersistenceExportReviewSummary summary{
        .format_version = std::string(kPersistenceExportReviewSummaryFormatVersion),
        .source_export_manifest_format_version = manifest.format_version,
        .source_persistence_descriptor_format_version =
            manifest.source_persistence_descriptor_format_version,
        .source_checkpoint_record_format_version =
            manifest.source_checkpoint_record_format_version,
        .workflow_canonical_name = manifest.workflow_canonical_name,
        .session_id = manifest.session_id,
        .run_id = manifest.run_id,
        .input_fixture = manifest.input_fixture,
        .workflow_status = manifest.workflow_status,
        .checkpoint_status = manifest.checkpoint_status,
        .persistence_status = manifest.persistence_status,
        .manifest_status = manifest.manifest_status,
        .workflow_failure_summary = manifest.workflow_failure_summary,
        .export_package_identity = manifest.export_package_identity,
        .planned_durable_identity = manifest.planned_durable_identity,
        .manifest_boundary_kind = manifest.manifest_boundary_kind,
        .artifact_bundle_entry_count = manifest.artifact_bundle.entry_count,
        .artifact_bundle_preview = artifact_bundle_preview_for_manifest(manifest),
        .manifest_ready = manifest.manifest_ready,
        .next_required_artifact_kind = manifest.next_required_artifact_kind,
        .store_import_blocker = manifest.store_import_blocker,
        .next_action = next_action_for_manifest(manifest),
        .store_boundary_summary = store_boundary_summary_for_manifest(manifest),
        .import_preview = import_preview_for_manifest(manifest),
        .next_step_recommendation = next_step_recommendation_for_manifest(manifest),
    };

    const auto summary_validation = validate_persistence_export_review_summary(summary);
    result.diagnostics.append(summary_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.summary = std::move(summary);
    return result;
}

} // namespace ahfl::persistence_export
