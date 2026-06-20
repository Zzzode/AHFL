#include "compiler/backends/pipeline/store_import_review.hpp"
#include "compiler/backends/pipeline/review_helpers.hpp"
#include "printer_helpers.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl {

namespace {

using backend_printer::line;

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
descriptor_boundary_kind_name(store_import::StoreImportBoundaryKind kind) {
    switch (kind) {
    case store_import::StoreImportBoundaryKind::LocalStagingOnly:
        return "local_staging_only";
    case store_import::StoreImportBoundaryKind::AdapterConsumable:
        return "adapter_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string artifact_kind_name(store_import::StagingArtifactKind kind) {
    switch (kind) {
    case store_import::StagingArtifactKind::ExportManifest:
        return "export_manifest";
    case store_import::StagingArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case store_import::StagingArtifactKind::PersistenceReview:
        return "persistence_review";
    case store_import::StagingArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(store_import::StagingBlockerKind kind) {
    switch (kind) {
    case store_import::StagingBlockerKind::WaitingOnExportManifest:
        return "waiting_on_export_manifest";
    case store_import::StagingBlockerKind::MissingStoreImportCandidateIdentity:
        return "missing_store_import_candidate_identity";
    case store_import::StagingBlockerKind::MissingStagingArtifactSet:
        return "missing_staging_artifact_set";
    case store_import::StagingBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case store_import::StagingBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(store_import::StoreImportReviewNextActionKind action) {
    switch (action) {
    case store_import::StoreImportReviewNextActionKind::HandoffStoreImportDescriptor:
        return "handoff_store_import_descriptor";
    case store_import::StoreImportReviewNextActionKind::AwaitStagingReadiness:
        return "await_staging_readiness";
    case store_import::StoreImportReviewNextActionKind::ArchiveCompletedStoreImportState:
        return "archive_completed_store_import_state";
    case store_import::StoreImportReviewNextActionKind::InvestigateStoreImportFailure:
        return "investigate_store_import_failure";
    case store_import::StoreImportReviewNextActionKind::PreservePartialStoreImportState:
        return "preserve_partial_store_import_state";
    }

    return "invalid";
}

void print_staging_blocker(std::ostream &out,
                           int indent_level,
                           const std::optional<store_import::StagingBlocker> &blocker) {
    line(out, indent_level, "staging_blocker {");
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

void print_store_import_review(const store_import::StoreImportReviewSummary &summary,
                               std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "source_manifest_format " + summary.source_export_manifest_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out,
         0,
         "workflow_status " + pipeline_review::workflow_status_name(summary.workflow_status));
    line(out,
         0,
         "checkpoint_status " + pipeline_review::checkpoint_status_name(summary.checkpoint_status));
    line(out,
         0,
         "persistence_status " +
             pipeline_review::persistence_status_name(summary.persistence_status));
    line(out,
         0,
         "manifest_status " + pipeline_review::manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "descriptor_boundary_kind " +
             descriptor_boundary_kind_name(summary.descriptor_boundary_kind));
    line(out, 0, std::string("import_ready ") + (summary.import_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "store_boundary " + summary.store_boundary_summary);
    line(out, 0, "staging_preview " + summary.staging_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_required_staging_artifact " +
             (summary.next_required_staging_artifact_kind.has_value()
                  ? artifact_kind_name(*summary.next_required_staging_artifact_kind)
                  : "none"));

    pipeline_review::print_failure_summary(
        out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_staging_blocker(out, 0, summary.staging_blocker);
    pipeline_review::print_string_list(
        out, 0, "staging_artifact_preview", summary.staging_artifact_preview);
}

} // namespace ahfl
