#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl::store_import {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(identity, diagnostics, "store import descriptor");
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_field(summary, diagnostics, "store import descriptor");
}

[[nodiscard]] bool package_identity_equals(const std::optional<handoff::PackageIdentity> &lhs,
                                           const std::optional<handoff::PackageIdentity> &rhs) {
    return validation::package_identity_equals(lhs, rhs);
}

[[nodiscard]] bool failure_summary_equals(
    const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
    const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    return validation::failure_summary_equals(lhs, rhs);
}

[[nodiscard]] bool export_artifact_entry_equals(
    const persistence_export::ExportArtifactEntry &lhs,
    const persistence_export::ExportArtifactEntry &rhs) {
    return lhs.artifact_kind == rhs.artifact_kind &&
           lhs.logical_artifact_name == rhs.logical_artifact_name &&
           lhs.source_format_version == rhs.source_format_version;
}

[[nodiscard]] bool export_artifact_bundle_equals(
    const persistence_export::ExportArtifactBundle &lhs,
    const persistence_export::ExportArtifactBundle &rhs) {
    if (lhs.entry_count != rhs.entry_count || lhs.entries.size() != rhs.entries.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.entries.size(); ++index) {
        if (!export_artifact_entry_equals(lhs.entries[index], rhs.entries[index])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool export_blocker_equals(
    const std::optional<persistence_export::StoreImportBlocker> &lhs,
    const std::optional<persistence_export::StoreImportBlocker> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->kind == rhs->kind && lhs->message == rhs->message &&
           lhs->logical_artifact_name == rhs->logical_artifact_name;
}

[[nodiscard]] std::string build_store_import_candidate_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    std::size_t staging_entry_count) {
    const auto &identity_anchor =
        source_package_identity.has_value() ? source_package_identity->name
                                            : std::string(workflow_canonical_name);
    return "store-import::" + identity_anchor + "::" + std::string(session_id) + "::stage-" +
           std::to_string(staging_entry_count);
}

[[nodiscard]] StagingArtifactKind to_staging_artifact_kind(
    persistence_export::ExportArtifactKind kind) {
    switch (kind) {
    case persistence_export::ExportArtifactKind::PersistenceDescriptor:
        return StagingArtifactKind::PersistenceDescriptor;
    case persistence_export::ExportArtifactKind::PersistenceReview:
        return StagingArtifactKind::PersistenceReview;
    case persistence_export::ExportArtifactKind::CheckpointRecord:
        return StagingArtifactKind::CheckpointRecord;
    }

    return StagingArtifactKind::ExportManifest;
}

[[nodiscard]] StagingBlockerKind to_staging_blocker_kind(
    persistence_export::StoreImportBlockerKind kind) {
    switch (kind) {
    case persistence_export::StoreImportBlockerKind::WaitingOnPersistenceState:
        return StagingBlockerKind::WaitingOnExportManifest;
    case persistence_export::StoreImportBlockerKind::MissingExportPackageIdentity:
        return StagingBlockerKind::MissingStoreImportCandidateIdentity;
    case persistence_export::StoreImportBlockerKind::MissingArtifactBundle:
        return StagingBlockerKind::MissingStagingArtifactSet;
    case persistence_export::StoreImportBlockerKind::WorkflowFailure:
        return StagingBlockerKind::WorkflowFailure;
    case persistence_export::StoreImportBlockerKind::WorkflowPartial:
        return StagingBlockerKind::WorkflowPartial;
    }

    return StagingBlockerKind::WaitingOnExportManifest;
}

[[nodiscard]] StagingArtifactSet build_staging_artifact_set(
    const persistence_export::PersistenceExportManifest &manifest) {
    StagingArtifactSet artifact_set;
    artifact_set.entries.push_back(StagingArtifactEntry{
        .artifact_kind = StagingArtifactKind::ExportManifest,
        .logical_artifact_name = "export-manifest.json",
        .source_format_version = manifest.format_version,
        .required_for_import = true,
    });

    for (const auto &entry : manifest.artifact_bundle.entries) {
        artifact_set.entries.push_back(StagingArtifactEntry{
            .artifact_kind = to_staging_artifact_kind(entry.artifact_kind),
            .logical_artifact_name = entry.logical_artifact_name,
            .source_format_version = entry.source_format_version,
            .required_for_import =
                entry.artifact_kind != persistence_export::ExportArtifactKind::PersistenceReview,
        });
    }

    artifact_set.entry_count = artifact_set.entries.size();
    return artifact_set;
}

void compare_manifest_against_expected(
    const persistence_export::PersistenceExportManifest &manifest,
    const persistence_export::PersistenceExportManifest &expected,
    DiagnosticBag &diagnostics) {
    if (manifest.format_version != expected.format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest format_version does not match derived export manifest");
    }

    if (manifest.source_execution_plan_format_version !=
        expected.source_execution_plan_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_execution_plan_format_version does not match derived export manifest");
    }

    if (manifest.source_runtime_session_format_version !=
        expected.source_runtime_session_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_runtime_session_format_version does not match derived export manifest");
    }

    if (manifest.source_execution_journal_format_version !=
        expected.source_execution_journal_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_execution_journal_format_version does not match derived export manifest");
    }

    if (manifest.source_replay_view_format_version !=
        expected.source_replay_view_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_replay_view_format_version does not match derived export manifest");
    }

    if (manifest.source_scheduler_snapshot_format_version !=
        expected.source_scheduler_snapshot_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_scheduler_snapshot_format_version does not match derived export manifest");
    }

    if (manifest.source_checkpoint_record_format_version !=
        expected.source_checkpoint_record_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_checkpoint_record_format_version does not match derived export manifest");
    }

    if (manifest.source_persistence_descriptor_format_version !=
        expected.source_persistence_descriptor_format_version) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_persistence_descriptor_format_version does not match derived export manifest");
    }

    if (!package_identity_equals(manifest.source_package_identity,
                                 expected.source_package_identity)) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest source_package_identity does not match derived export manifest");
    }

    if (manifest.workflow_canonical_name != expected.workflow_canonical_name) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest workflow_canonical_name does not match derived export manifest");
    }

    if (manifest.session_id != expected.session_id) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest session_id does not match derived export manifest");
    }

    if (manifest.run_id != expected.run_id) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest run_id does not match derived export manifest");
    }

    if (manifest.input_fixture != expected.input_fixture) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest input_fixture does not match derived export manifest");
    }

    if (manifest.workflow_status != expected.workflow_status) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest workflow_status does not match derived export manifest");
    }

    if (manifest.checkpoint_status != expected.checkpoint_status) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest checkpoint_status does not match derived export manifest");
    }

    if (manifest.persistence_status != expected.persistence_status) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest persistence_status does not match derived export manifest");
    }

    if (manifest.manifest_status != expected.manifest_status) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest manifest_status does not match derived export manifest");
    }

    if (!failure_summary_equals(manifest.workflow_failure_summary,
                                expected.workflow_failure_summary)) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest workflow_failure_summary does not match derived export manifest");
    }

    if (manifest.export_package_identity != expected.export_package_identity) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest export_package_identity does not match derived export manifest");
    }

    if (manifest.planned_durable_identity != expected.planned_durable_identity) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest planned_durable_identity does not match derived export manifest");
    }

    if (manifest.manifest_boundary_kind != expected.manifest_boundary_kind) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest manifest_boundary_kind does not match derived export manifest");
    }

    if (!export_artifact_bundle_equals(manifest.artifact_bundle, expected.artifact_bundle)) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest artifact_bundle does not match derived export manifest");
    }

    if (manifest.manifest_ready != expected.manifest_ready) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest manifest_ready does not match derived export manifest");
    }

    if (manifest.next_required_artifact_kind != expected.next_required_artifact_kind) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest next_required_artifact_kind does not match derived export manifest");
    }

    if (!export_blocker_equals(manifest.store_import_blocker, expected.store_import_blocker)) {
        diagnostics.error(
            "store import descriptor bootstrap export manifest store_import_blocker does not match derived export manifest");
    }
}

} // namespace

StoreImportDescriptorValidationResult
validate_store_import_descriptor(const StoreImportDescriptor &descriptor) {
    StoreImportDescriptorValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (descriptor.format_version != kStoreImportDescriptorFormatVersion) {
        diagnostics.error("store import descriptor format_version must be '" +
                          std::string(kStoreImportDescriptorFormatVersion) + "'");
    }

    if (descriptor.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error(
            "store import descriptor source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (descriptor.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "store import descriptor source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (descriptor.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "store import descriptor source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (descriptor.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error(
            "store import descriptor source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (descriptor.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "store import descriptor source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (descriptor.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "store import descriptor source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (descriptor.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        diagnostics.error(
            "store import descriptor source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (descriptor.source_export_manifest_format_version !=
        persistence_export::kPersistenceExportManifestFormatVersion) {
        diagnostics.error(
            "store import descriptor source_export_manifest_format_version must be '" +
            std::string(persistence_export::kPersistenceExportManifestFormatVersion) + "'");
    }

    if (descriptor.workflow_canonical_name.empty()) {
        diagnostics.error("store import descriptor workflow_canonical_name must not be empty");
    }

    if (descriptor.session_id.empty()) {
        diagnostics.error("store import descriptor session_id must not be empty");
    }

    if (descriptor.input_fixture.empty()) {
        diagnostics.error("store import descriptor input_fixture must not be empty");
    }

    if (descriptor.export_package_identity.empty()) {
        diagnostics.error("store import descriptor export_package_identity must not be empty");
    }

    if (descriptor.store_import_candidate_identity.empty()) {
        diagnostics.error(
            "store import descriptor store_import_candidate_identity must not be empty");
    }

    if (descriptor.planned_durable_identity.empty()) {
        diagnostics.error(
            "store import descriptor planned_durable_identity must not be empty");
    }

    if (descriptor.source_package_identity.has_value()) {
        validate_package_identity(*descriptor.source_package_identity, diagnostics);
    }

    if (descriptor.workflow_failure_summary.has_value()) {
        validate_failure_summary(*descriptor.workflow_failure_summary, diagnostics);
    }

    if (descriptor.staging_artifact_set.entry_count !=
        descriptor.staging_artifact_set.entries.size()) {
        diagnostics.error(
            "store import descriptor staging_artifact_set entry_count must match entries length");
    }

    if (descriptor.import_ready && descriptor.staging_artifact_set.entries.empty()) {
        diagnostics.error(
            "store import descriptor import_ready requires non-empty staging_artifact_set");
    }

    std::unordered_set<std::string> logical_names;
    for (const auto &entry : descriptor.staging_artifact_set.entries) {
        if (entry.logical_artifact_name.empty()) {
            diagnostics.error(
                "store import descriptor staging_artifact_set entry logical_artifact_name must not be empty");
        } else if (!logical_names.insert(entry.logical_artifact_name).second) {
            diagnostics.error(
                "store import descriptor staging_artifact_set contains duplicate logical_artifact_name '" +
                entry.logical_artifact_name + "'");
        }

        if (entry.source_format_version.empty()) {
            diagnostics.error(
                "store import descriptor staging_artifact_set entry source_format_version must not be empty");
        }
    }

    if (descriptor.import_ready && descriptor.staging_blocker.has_value()) {
        diagnostics.error(
            "store import descriptor cannot contain staging_blocker when import_ready is true");
    }

    if (descriptor.import_ready && descriptor.next_required_staging_artifact_kind.has_value()) {
        diagnostics.error(
            "store import descriptor cannot contain next_required_staging_artifact_kind when import_ready is true");
    }

    if (!descriptor.import_ready &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted &&
        !descriptor.staging_blocker.has_value()) {
        diagnostics.error(
            "store import descriptor must contain staging_blocker when import_ready is false");
    }

    if (descriptor.staging_blocker.has_value()) {
        if (descriptor.staging_blocker->message.empty()) {
            diagnostics.error(
                "store import descriptor staging_blocker message must not be empty");
        }

        if (descriptor.staging_blocker->logical_artifact_name.has_value() &&
            descriptor.staging_blocker->logical_artifact_name->empty()) {
            diagnostics.error(
                "store import descriptor staging_blocker logical_artifact_name must not be empty");
        }
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::ReadyToImport &&
        !descriptor.import_ready) {
        diagnostics.error(
            "store import descriptor ReadyToImport status requires import_ready");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::ReadyToImport &&
        descriptor.manifest_status !=
            persistence_export::PersistenceExportManifestStatus::ReadyToImport) {
        diagnostics.error(
            "store import descriptor ReadyToImport status requires ReadyToImport manifest_status");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::ReadyToImport &&
        descriptor.staging_blocker.has_value()) {
        diagnostics.error(
            "store import descriptor ReadyToImport status cannot have staging_blocker");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::Blocked &&
        descriptor.import_ready) {
        diagnostics.error("store import descriptor Blocked status cannot be import_ready");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::Blocked &&
        descriptor.manifest_status != persistence_export::PersistenceExportManifestStatus::Blocked) {
        diagnostics.error(
            "store import descriptor Blocked status requires Blocked manifest_status");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalCompleted &&
        !descriptor.import_ready) {
        diagnostics.error(
            "store import descriptor TerminalCompleted status requires import_ready");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalCompleted &&
        descriptor.manifest_status !=
            persistence_export::PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "store import descriptor TerminalCompleted status requires TerminalCompleted manifest_status");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalCompleted &&
        descriptor.staging_blocker.has_value()) {
        diagnostics.error(
            "store import descriptor TerminalCompleted status cannot have staging_blocker");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalCompleted &&
        descriptor.next_required_staging_artifact_kind.has_value()) {
        diagnostics.error(
            "store import descriptor TerminalCompleted status cannot have next_required_staging_artifact_kind");
    }

    if ((descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalFailed ||
         descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalPartial) &&
        !descriptor.staging_blocker.has_value()) {
        diagnostics.error(
            "store import descriptor terminal blocked status requires staging_blocker");
    }

    if ((descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalFailed ||
         descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalPartial) &&
        descriptor.import_ready) {
        diagnostics.error(
            "store import descriptor terminal blocked status cannot be import_ready");
    }

    if ((descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalFailed ||
         descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalPartial) &&
        descriptor.next_required_staging_artifact_kind.has_value()) {
        diagnostics.error(
            "store import descriptor terminal blocked status cannot have next_required_staging_artifact_kind");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalFailed &&
        descriptor.manifest_status !=
            persistence_export::PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "store import descriptor TerminalFailed status requires TerminalFailed manifest_status");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalPartial &&
        descriptor.manifest_status !=
            persistence_export::PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "store import descriptor TerminalPartial status requires TerminalPartial manifest_status");
    }

    if (descriptor.descriptor_status == StoreImportDescriptorStatus::TerminalFailed &&
        !descriptor.workflow_failure_summary.has_value()) {
        diagnostics.error(
            "store import descriptor TerminalFailed status requires workflow_failure_summary");
    }

    if (descriptor.descriptor_boundary_kind == StoreImportBoundaryKind::AdapterConsumable &&
        !descriptor.import_ready &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "store import descriptor adapter-consumable boundary requires ready or completed descriptor_status");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "store import descriptor completed workflow_status requires TerminalCompleted descriptor_status");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error(
            "store import descriptor failed workflow_status requires TerminalFailed descriptor_status");
    }

    if (descriptor.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "store import descriptor TerminalCompleted checkpoint_status requires TerminalCompleted descriptor_status");
    }

    if (descriptor.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error(
            "store import descriptor TerminalFailed checkpoint_status requires TerminalFailed descriptor_status");
    }

    if (descriptor.checkpoint_status ==
            checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error(
            "store import descriptor TerminalPartial checkpoint_status requires TerminalPartial descriptor_status");
    }

    if (descriptor.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalCompleted) {
        diagnostics.error(
            "store import descriptor TerminalCompleted persistence_status requires TerminalCompleted descriptor_status");
    }

    if (descriptor.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalFailed) {
        diagnostics.error(
            "store import descriptor TerminalFailed persistence_status requires TerminalFailed descriptor_status");
    }

    if (descriptor.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error(
            "store import descriptor TerminalPartial persistence_status requires TerminalPartial descriptor_status");
    }

    if (descriptor.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::ReadyToImport &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::Blocked &&
        descriptor.descriptor_status != StoreImportDescriptorStatus::TerminalPartial) {
        diagnostics.error(
            "store import descriptor partial workflow_status must map to ReadyToImport, Blocked, or TerminalPartial descriptor_status");
    }

    return result;
}

StoreImportDescriptorResult
build_store_import_descriptor(
    const handoff::ExecutionPlan &plan,
    const runtime_session::RuntimeSession &session,
    const execution_journal::ExecutionJournal &journal,
    const replay_view::ReplayView &replay,
    const scheduler_snapshot::SchedulerSnapshot &snapshot,
    const checkpoint_record::CheckpointRecord &record,
    const persistence_descriptor::CheckpointPersistenceDescriptor &persistence_descriptor,
    const persistence_export::PersistenceExportManifest &manifest) {
    StoreImportDescriptorResult result{
        .descriptor = std::nullopt,
        .diagnostics = {},
    };

    const auto manifest_validation =
        persistence_export::validate_persistence_export_manifest(manifest);
    result.diagnostics.append(manifest_validation.diagnostics);

    const auto expected_manifest = persistence_export::build_persistence_export_manifest(
        plan, session, journal, replay, snapshot, record, persistence_descriptor);
    result.diagnostics.append(expected_manifest.diagnostics);

    if (result.has_errors() || !expected_manifest.manifest.has_value()) {
        return result;
    }

    compare_manifest_against_expected(manifest, *expected_manifest.manifest, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    StoreImportDescriptor descriptor{
        .format_version = std::string(kStoreImportDescriptorFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_replay_view_format_version = replay.format_version,
        .source_scheduler_snapshot_format_version = snapshot.format_version,
        .source_checkpoint_record_format_version = record.format_version,
        .source_persistence_descriptor_format_version = persistence_descriptor.format_version,
        .source_export_manifest_format_version = manifest.format_version,
        .source_package_identity = plan.source_package_identity,
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
        .store_import_candidate_identity =
            build_store_import_candidate_identity(plan.source_package_identity,
                                                  manifest.workflow_canonical_name,
                                                  manifest.session_id,
                                                  manifest.artifact_bundle.entry_count + 1),
        .planned_durable_identity = manifest.planned_durable_identity,
        .descriptor_boundary_kind = StoreImportBoundaryKind::LocalStagingOnly,
        .staging_artifact_set = build_staging_artifact_set(manifest),
        .import_ready = false,
        .next_required_staging_artifact_kind = std::nullopt,
        .descriptor_status = StoreImportDescriptorStatus::Blocked,
        .staging_blocker = std::nullopt,
    };

    switch (manifest.manifest_status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        descriptor.descriptor_status = StoreImportDescriptorStatus::ReadyToImport;
        descriptor.import_ready = true;
        descriptor.descriptor_boundary_kind = StoreImportBoundaryKind::AdapterConsumable;
        break;
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        descriptor.descriptor_status = StoreImportDescriptorStatus::Blocked;
        if (manifest.next_required_artifact_kind.has_value()) {
            descriptor.next_required_staging_artifact_kind =
                to_staging_artifact_kind(*manifest.next_required_artifact_kind);
        }
        if (manifest.store_import_blocker.has_value()) {
            descriptor.staging_blocker = StagingBlocker{
                .kind = to_staging_blocker_kind(manifest.store_import_blocker->kind),
                .message = manifest.store_import_blocker->message,
                .logical_artifact_name = manifest.store_import_blocker->logical_artifact_name,
            };
        }
        break;
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        descriptor.descriptor_status = StoreImportDescriptorStatus::TerminalCompleted;
        descriptor.import_ready = true;
        descriptor.descriptor_boundary_kind = StoreImportBoundaryKind::AdapterConsumable;
        break;
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        descriptor.descriptor_status = StoreImportDescriptorStatus::TerminalFailed;
        if (manifest.store_import_blocker.has_value()) {
            descriptor.staging_blocker = StagingBlocker{
                .kind = to_staging_blocker_kind(manifest.store_import_blocker->kind),
                .message = manifest.store_import_blocker->message,
                .logical_artifact_name = manifest.store_import_blocker->logical_artifact_name,
            };
        }
        break;
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        descriptor.descriptor_status = StoreImportDescriptorStatus::TerminalPartial;
        if (manifest.store_import_blocker.has_value()) {
            descriptor.staging_blocker = StagingBlocker{
                .kind = to_staging_blocker_kind(manifest.store_import_blocker->kind),
                .message = manifest.store_import_blocker->message,
                .logical_artifact_name = manifest.store_import_blocker->logical_artifact_name,
            };
        }
        break;
    }

    const auto validation = validate_store_import_descriptor(descriptor);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.descriptor = std::move(descriptor);
    return result;
}

} // namespace ahfl::store_import
