#include "backends/pipeline/persistence_export_review.hpp"
#include "printer_helpers.hpp"
#include "backends/pipeline/review_helpers.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

using backend_printer::line;

[[nodiscard]] std::string
manifest_boundary_kind_name(persistence_export::ManifestBoundaryKind kind) {
    switch (kind) {
    case persistence_export::ManifestBoundaryKind::LocalBundleOnly:
        return "local_bundle_only";
    case persistence_export::ManifestBoundaryKind::StoreImportAdjacent:
        return "store_import_adjacent";
    }

    return "invalid";
}

[[nodiscard]] std::string artifact_kind_name(persistence_export::ExportArtifactKind kind) {
    switch (kind) {
    case persistence_export::ExportArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case persistence_export::ExportArtifactKind::PersistenceReview:
        return "persistence_review";
    case persistence_export::ExportArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(persistence_export::StoreImportBlockerKind kind) {
    switch (kind) {
    case persistence_export::StoreImportBlockerKind::WaitingOnPersistenceState:
        return "waiting_on_persistence_state";
    case persistence_export::StoreImportBlockerKind::MissingExportPackageIdentity:
        return "missing_export_package_identity";
    case persistence_export::StoreImportBlockerKind::MissingArtifactBundle:
        return "missing_artifact_bundle";
    case persistence_export::StoreImportBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case persistence_export::StoreImportBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(persistence_export::PersistenceExportReviewNextActionKind action) {
    switch (action) {
    case persistence_export::PersistenceExportReviewNextActionKind::HandoffExportPackage:
        return "handoff_export_package";
    case persistence_export::PersistenceExportReviewNextActionKind::AwaitManifestReadiness:
        return "await_manifest_readiness";
    case persistence_export::PersistenceExportReviewNextActionKind::ArchiveCompletedExportPackage:
        return "archive_completed_export_package";
    case persistence_export::PersistenceExportReviewNextActionKind::InvestigateExportFailure:
        return "investigate_export_failure";
    case persistence_export::PersistenceExportReviewNextActionKind::PreservePartialExportState:
        return "preserve_partial_export_state";
    }

    return "invalid";
}

void print_store_import_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<persistence_export::StoreImportBlocker> &blocker) {
    line(out, indent_level, "store_import_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "logical_artifact_name " + (blocker->logical_artifact_name.has_value()
                                         ? *blocker->logical_artifact_name
                                         : "none"));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_persistence_export_review(
    const persistence_export::PersistenceExportReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
    line(out, 0, "source_manifest_format " + summary.source_export_manifest_format_version);
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
    line(out, 0, "manifest_status " + pipeline_review::manifest_status_name(summary.manifest_status));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "manifest_boundary_kind " + manifest_boundary_kind_name(summary.manifest_boundary_kind));
    line(out, 0, std::string("manifest_ready ") + (summary.manifest_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "store_boundary " + summary.store_boundary_summary);
    line(out, 0, "import_preview " + summary.import_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_required_artifact " + (summary.next_required_artifact_kind.has_value()
                                          ? artifact_kind_name(*summary.next_required_artifact_kind)
                                          : "none"));

    pipeline_review::print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_store_import_blocker(out, 0, summary.store_import_blocker);
    pipeline_review::print_string_list(out, 0, "artifact_bundle_preview", summary.artifact_bundle_preview);
}

} // namespace ahfl
