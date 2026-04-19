#include "ahfl/persistence_export/manifest.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl::persistence_export {

namespace {

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    if (identity.format_version != handoff::kFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_package_identity format_version must be '" +
            std::string(handoff::kFormatVersion) + "'");
    }

    if (identity.name.empty()) {
        diagnostics.error(
            "persistence export manifest source_package_identity name must not be empty");
    }

    if (identity.version.empty()) {
        diagnostics.error(
            "persistence export manifest source_package_identity version must not be empty");
    }
}

void validate_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                              DiagnosticBag &diagnostics) {
    if (summary.message.empty()) {
        diagnostics.error(
            "persistence export manifest workflow_failure_summary message must not be empty");
    }

    if (summary.node_name.has_value() && summary.node_name->empty()) {
        diagnostics.error(
            "persistence export manifest workflow_failure_summary node_name must not be empty");
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
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->format_version == rhs->format_version && lhs->name == rhs->name &&
           lhs->version == rhs->version;
}

[[nodiscard]] bool failure_summary_equals(
    const std::optional<runtime_session::RuntimeFailureSummary> &lhs,
    const std::optional<runtime_session::RuntimeFailureSummary> &rhs) {
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }

    if (!lhs.has_value()) {
        return true;
    }

    return lhs->kind == rhs->kind && lhs->node_name == rhs->node_name &&
           lhs->message == rhs->message;
}

[[nodiscard]] bool is_prefix(const std::vector<std::string> &prefix,
                             const std::vector<std::string> &full) {
    if (prefix.size() > full.size()) {
        return false;
    }

    for (std::size_t index = 0; index < prefix.size(); ++index) {
        if (prefix[index] != full[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool vector_equals(const std::vector<std::string> &lhs,
                                 const std::vector<std::string> &rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::string build_export_package_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    std::size_t exportable_prefix_size) {
    const auto &identity_anchor =
        source_package_identity.has_value() ? source_package_identity->name
                                            : std::string(workflow_canonical_name);
    return "export::" + identity_anchor + "::" + std::string(session_id) + "::prefix-" +
           std::to_string(exportable_prefix_size);
}

[[nodiscard]] ExportArtifactBundle build_default_artifact_bundle() {
    ExportArtifactBundle bundle;
    bundle.entries = {
        ExportArtifactEntry{
            .artifact_kind = ExportArtifactKind::PersistenceDescriptor,
            .logical_artifact_name = "persistence-descriptor.json",
            .source_format_version =
                std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion),
        },
        ExportArtifactEntry{
            .artifact_kind = ExportArtifactKind::CheckpointRecord,
            .logical_artifact_name = "checkpoint-record.json",
            .source_format_version = std::string(checkpoint_record::kCheckpointRecordFormatVersion),
        },
    };
    bundle.entry_count = bundle.entries.size();
    return bundle;
}

[[nodiscard]] StoreImportBlockerKind to_store_import_blocker_kind(
    persistence_descriptor::PersistenceBlockerKind kind) {
    switch (kind) {
    case persistence_descriptor::PersistenceBlockerKind::WaitingOnCheckpointState:
    case persistence_descriptor::PersistenceBlockerKind::MissingPlannedDurableIdentity:
        return StoreImportBlockerKind::WaitingOnPersistenceState;
    case persistence_descriptor::PersistenceBlockerKind::WorkflowFailure:
        return StoreImportBlockerKind::WorkflowFailure;
    case persistence_descriptor::PersistenceBlockerKind::WorkflowPartial:
        return StoreImportBlockerKind::WorkflowPartial;
    }

    return StoreImportBlockerKind::WaitingOnPersistenceState;
}

} // namespace

PersistenceExportManifestValidationResult
validate_persistence_export_manifest(const PersistenceExportManifest &manifest) {
    PersistenceExportManifestValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (manifest.format_version != kPersistenceExportManifestFormatVersion) {
        diagnostics.error("persistence export manifest format_version must be '" +
                          std::string(kPersistenceExportManifestFormatVersion) + "'");
    }

    if (manifest.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_execution_plan_format_version must be '" +
            std::string(handoff::kExecutionPlanFormatVersion) + "'");
    }

    if (manifest.source_runtime_session_format_version !=
        runtime_session::kRuntimeSessionFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_runtime_session_format_version must be '" +
            std::string(runtime_session::kRuntimeSessionFormatVersion) + "'");
    }

    if (manifest.source_execution_journal_format_version !=
        execution_journal::kExecutionJournalFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_execution_journal_format_version must be '" +
            std::string(execution_journal::kExecutionJournalFormatVersion) + "'");
    }

    if (manifest.source_replay_view_format_version != replay_view::kReplayViewFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_replay_view_format_version must be '" +
            std::string(replay_view::kReplayViewFormatVersion) + "'");
    }

    if (manifest.source_scheduler_snapshot_format_version !=
        scheduler_snapshot::kSchedulerSnapshotFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_scheduler_snapshot_format_version must be '" +
            std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion) + "'");
    }

    if (manifest.source_checkpoint_record_format_version !=
        checkpoint_record::kCheckpointRecordFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_checkpoint_record_format_version must be '" +
            std::string(checkpoint_record::kCheckpointRecordFormatVersion) + "'");
    }

    if (manifest.source_persistence_descriptor_format_version !=
        persistence_descriptor::kPersistenceDescriptorFormatVersion) {
        diagnostics.error(
            "persistence export manifest source_persistence_descriptor_format_version must be '" +
            std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion) + "'");
    }

    if (manifest.workflow_canonical_name.empty()) {
        diagnostics.error("persistence export manifest workflow_canonical_name must not be empty");
    }

    if (manifest.session_id.empty()) {
        diagnostics.error("persistence export manifest session_id must not be empty");
    }

    if (manifest.input_fixture.empty()) {
        diagnostics.error("persistence export manifest input_fixture must not be empty");
    }

    if (manifest.export_package_identity.empty()) {
        diagnostics.error("persistence export manifest export_package_identity must not be empty");
    }

    if (manifest.planned_durable_identity.empty()) {
        diagnostics.error(
            "persistence export manifest planned_durable_identity must not be empty");
    }

    if (manifest.source_package_identity.has_value()) {
        validate_package_identity(*manifest.source_package_identity, diagnostics);
    }

    if (manifest.workflow_failure_summary.has_value()) {
        validate_failure_summary(*manifest.workflow_failure_summary, diagnostics);
    }

    if (manifest.artifact_bundle.entry_count != manifest.artifact_bundle.entries.size()) {
        diagnostics.error(
            "persistence export manifest artifact_bundle entry_count must match entries length");
    }

    if (manifest.manifest_ready && manifest.artifact_bundle.entries.empty()) {
        diagnostics.error(
            "persistence export manifest manifest_ready requires non-empty artifact_bundle");
    }

    std::unordered_set<std::string> logical_names;
    for (const auto &entry : manifest.artifact_bundle.entries) {
        if (entry.logical_artifact_name.empty()) {
            diagnostics.error(
                "persistence export manifest artifact_bundle entry logical_artifact_name must not be empty");
        } else if (!logical_names.insert(entry.logical_artifact_name).second) {
            diagnostics.error(
                "persistence export manifest artifact_bundle contains duplicate logical_artifact_name '" +
                entry.logical_artifact_name + "'");
        }

        if (entry.source_format_version.empty()) {
            diagnostics.error(
                "persistence export manifest artifact_bundle entry source_format_version must not be empty");
        }
    }

    if (manifest.manifest_ready && manifest.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export manifest cannot contain store_import_blocker when manifest_ready is true");
    }

    if (manifest.manifest_ready && manifest.next_required_artifact_kind.has_value()) {
        diagnostics.error(
            "persistence export manifest cannot contain next_required_artifact_kind when manifest_ready is true");
    }

    if (!manifest.manifest_ready &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalCompleted &&
        !manifest.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export manifest must contain store_import_blocker when manifest_ready is false");
    }

    if (manifest.store_import_blocker.has_value()) {
        if (manifest.store_import_blocker->message.empty()) {
            diagnostics.error(
                "persistence export manifest store_import_blocker message must not be empty");
        }

        if (manifest.store_import_blocker->logical_artifact_name.has_value() &&
            manifest.store_import_blocker->logical_artifact_name->empty()) {
            diagnostics.error(
                "persistence export manifest store_import_blocker logical_artifact_name must not be empty");
        }
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::ReadyToImport &&
        !manifest.manifest_ready) {
        diagnostics.error(
            "persistence export manifest ReadyToImport status requires manifest_ready");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::ReadyToImport &&
        manifest.persistence_status !=
            persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport) {
        diagnostics.error(
            "persistence export manifest ReadyToImport status requires ReadyToExport persistence_status");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::ReadyToImport &&
        manifest.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export manifest ReadyToImport status cannot have store_import_blocker");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::Blocked &&
        manifest.manifest_ready) {
        diagnostics.error(
            "persistence export manifest Blocked status cannot be manifest_ready");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::TerminalCompleted &&
        !manifest.manifest_ready) {
        diagnostics.error(
            "persistence export manifest TerminalCompleted status requires manifest_ready");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::TerminalCompleted &&
        manifest.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export manifest TerminalCompleted status cannot have store_import_blocker");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::TerminalCompleted &&
        manifest.next_required_artifact_kind.has_value()) {
        diagnostics.error(
            "persistence export manifest TerminalCompleted status cannot have next_required_artifact_kind");
    }

    if ((manifest.manifest_status == PersistenceExportManifestStatus::TerminalFailed ||
         manifest.manifest_status == PersistenceExportManifestStatus::TerminalPartial) &&
        !manifest.store_import_blocker.has_value()) {
        diagnostics.error(
            "persistence export manifest terminal blocked status requires store_import_blocker");
    }

    if ((manifest.manifest_status == PersistenceExportManifestStatus::TerminalFailed ||
         manifest.manifest_status == PersistenceExportManifestStatus::TerminalPartial) &&
        manifest.manifest_ready) {
        diagnostics.error(
            "persistence export manifest terminal blocked status cannot be manifest_ready");
    }

    if ((manifest.manifest_status == PersistenceExportManifestStatus::TerminalFailed ||
         manifest.manifest_status == PersistenceExportManifestStatus::TerminalPartial) &&
        manifest.next_required_artifact_kind.has_value()) {
        diagnostics.error(
            "persistence export manifest terminal blocked status cannot have next_required_artifact_kind");
    }

    if (manifest.manifest_status == PersistenceExportManifestStatus::TerminalFailed &&
        !manifest.workflow_failure_summary.has_value()) {
        diagnostics.error(
            "persistence export manifest TerminalFailed status requires workflow_failure_summary");
    }

    if (manifest.manifest_boundary_kind == ManifestBoundaryKind::StoreImportAdjacent &&
        !manifest.manifest_ready &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export manifest store-import-adjacent boundary requires ready or completed manifest_status");
    }

    if (manifest.workflow_status == runtime_session::WorkflowSessionStatus::Completed &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export manifest completed workflow_status requires TerminalCompleted manifest_status");
    }

    if (manifest.workflow_status == runtime_session::WorkflowSessionStatus::Failed &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export manifest failed workflow_status requires TerminalFailed manifest_status");
    }

    if (manifest.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalCompleted &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export manifest TerminalCompleted checkpoint_status requires TerminalCompleted manifest_status");
    }

    if (manifest.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalFailed &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export manifest TerminalFailed checkpoint_status requires TerminalFailed manifest_status");
    }

    if (manifest.checkpoint_status == checkpoint_record::CheckpointRecordStatus::TerminalPartial &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export manifest TerminalPartial checkpoint_status requires TerminalPartial manifest_status");
    }

    if (manifest.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalCompleted) {
        diagnostics.error(
            "persistence export manifest TerminalCompleted persistence_status requires TerminalCompleted manifest_status");
    }

    if (manifest.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalFailed) {
        diagnostics.error(
            "persistence export manifest TerminalFailed persistence_status requires TerminalFailed manifest_status");
    }

    if (manifest.persistence_status ==
            persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export manifest TerminalPartial persistence_status requires TerminalPartial manifest_status");
    }

    if (manifest.workflow_status == runtime_session::WorkflowSessionStatus::Partial &&
        manifest.manifest_status != PersistenceExportManifestStatus::ReadyToImport &&
        manifest.manifest_status != PersistenceExportManifestStatus::Blocked &&
        manifest.manifest_status != PersistenceExportManifestStatus::TerminalPartial) {
        diagnostics.error(
            "persistence export manifest partial workflow_status must map to ReadyToImport, Blocked, or TerminalPartial manifest_status");
    }

    return result;
}

PersistenceExportManifestResult
build_persistence_export_manifest(
    const handoff::ExecutionPlan &plan,
    const runtime_session::RuntimeSession &session,
    const execution_journal::ExecutionJournal &journal,
    const replay_view::ReplayView &replay,
    const scheduler_snapshot::SchedulerSnapshot &snapshot,
    const checkpoint_record::CheckpointRecord &record,
    const persistence_descriptor::CheckpointPersistenceDescriptor &descriptor) {
    PersistenceExportManifestResult result{
        .manifest = std::nullopt,
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

    const auto snapshot_validation = scheduler_snapshot::validate_scheduler_snapshot(snapshot);
    result.diagnostics.append(snapshot_validation.diagnostics);

    const auto record_validation = checkpoint_record::validate_checkpoint_record(record);
    result.diagnostics.append(record_validation.diagnostics);

    const auto descriptor_validation =
        persistence_descriptor::validate_persistence_descriptor(descriptor);
    result.diagnostics.append(descriptor_validation.diagnostics);

    if (result.has_errors()) {
        return result;
    }

    if (session.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap runtime session source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal source_execution_plan_format_version does not match execution plan");
    }

    if (journal.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view source_execution_plan_format_version does not match execution plan");
    }

    if (replay.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view source_runtime_session_format_version does not match runtime session");
    }

    if (replay.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot source_execution_plan_format_version does not match execution plan");
    }

    if (snapshot.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot source_runtime_session_format_version does not match runtime session");
    }

    if (snapshot.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot source_execution_journal_format_version does not match execution journal");
    }

    if (snapshot.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot source_replay_view_format_version does not match replay view");
    }

    if (record.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_execution_plan_format_version does not match execution plan");
    }

    if (record.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_runtime_session_format_version does not match runtime session");
    }

    if (record.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_execution_journal_format_version does not match execution journal");
    }

    if (record.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_replay_view_format_version does not match replay view");
    }

    if (record.source_scheduler_snapshot_format_version != snapshot.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_scheduler_snapshot_format_version does not match scheduler snapshot");
    }

    if (descriptor.source_execution_plan_format_version != plan.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_execution_plan_format_version does not match execution plan");
    }

    if (descriptor.source_runtime_session_format_version != session.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_runtime_session_format_version does not match runtime session");
    }

    if (descriptor.source_execution_journal_format_version != journal.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_execution_journal_format_version does not match execution journal");
    }

    if (descriptor.source_replay_view_format_version != replay.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_replay_view_format_version does not match replay view");
    }

    if (descriptor.source_scheduler_snapshot_format_version != snapshot.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_scheduler_snapshot_format_version does not match scheduler snapshot");
    }

    if (descriptor.source_checkpoint_record_format_version != record.format_version) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_checkpoint_record_format_version does not match checkpoint record");
    }

    if (!package_identity_equals(plan.source_package_identity, session.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap runtime session source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, journal.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, replay.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, snapshot.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity, record.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record source_package_identity does not match execution plan");
    }

    if (!package_identity_equals(plan.source_package_identity,
                                 descriptor.source_package_identity)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor source_package_identity does not match execution plan");
    }

    if (session.workflow_canonical_name != journal.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != replay.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != snapshot.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != record.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record workflow_canonical_name does not match runtime session");
    }

    if (session.workflow_canonical_name != descriptor.workflow_canonical_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor workflow_canonical_name does not match runtime session");
    }

    if (session.session_id != journal.session_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal session_id does not match runtime session");
    }

    if (session.session_id != replay.session_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view session_id does not match runtime session");
    }

    if (session.session_id != snapshot.session_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot session_id does not match runtime session");
    }

    if (session.session_id != record.session_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record session_id does not match runtime session");
    }

    if (session.session_id != descriptor.session_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor session_id does not match runtime session");
    }

    if (session.run_id != journal.run_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap execution journal run_id does not match runtime session");
    }

    if (session.run_id != replay.run_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view run_id does not match runtime session");
    }

    if (session.run_id != snapshot.run_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot run_id does not match runtime session");
    }

    if (session.run_id != record.run_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record run_id does not match runtime session");
    }

    if (session.run_id != descriptor.run_id) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor run_id does not match runtime session");
    }

    if (session.input_fixture != replay.input_fixture) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view input_fixture does not match runtime session");
    }

    if (session.input_fixture != snapshot.input_fixture) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot input_fixture does not match runtime session");
    }

    if (session.input_fixture != record.input_fixture) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record input_fixture does not match runtime session");
    }

    if (session.input_fixture != descriptor.input_fixture) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor input_fixture does not match runtime session");
    }

    if (session.workflow_status != replay.workflow_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view workflow_status does not match runtime session");
    }

    if (session.workflow_status != snapshot.workflow_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot workflow_status does not match runtime session");
    }

    if (session.workflow_status != record.workflow_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record workflow_status does not match runtime session");
    }

    if (session.workflow_status != descriptor.workflow_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor workflow_status does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, replay.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, snapshot.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, record.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record workflow_failure_summary does not match runtime session");
    }

    if (!failure_summary_equals(session.failure_summary, descriptor.workflow_failure_summary)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor workflow_failure_summary does not match runtime session");
    }

    if (!replay.consistency.plan_matches_session || !replay.consistency.session_matches_journal ||
        !replay.consistency.journal_matches_execution_order) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view consistency must hold before building export manifest");
    }

    const auto *workflow = find_workflow_plan(plan, session.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error("persistence export manifest bootstrap workflow '" +
                                 session.workflow_canonical_name +
                                 "' does not exist in execution plan");
    }

    if (result.has_errors()) {
        return result;
    }

    std::vector<std::string> plan_execution_order;
    plan_execution_order.reserve(workflow->nodes.size());
    for (const auto &node : workflow->nodes) {
        plan_execution_order.push_back(node.name);
    }

    if (!vector_equals(snapshot.execution_order, plan_execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot execution_order does not match execution plan workflow order");
    }

    if (!vector_equals(replay.execution_order, session.execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view execution_order does not match runtime session");
    }

    if (!is_prefix(replay.execution_order, snapshot.execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap replay view execution_order must be a prefix of scheduler snapshot execution_order");
    }

    if (!is_prefix(snapshot.cursor.completed_prefix, replay.execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap scheduler snapshot completed_prefix must be a prefix of replay view execution_order");
    }

    if (record.snapshot_status != snapshot.snapshot_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record snapshot_status does not match scheduler snapshot");
    }

    if (!vector_equals(record.execution_order, snapshot.execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record execution_order does not match scheduler snapshot");
    }

    if (!vector_equals(record.cursor.persistable_prefix, snapshot.cursor.completed_prefix)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record persistable_prefix does not match scheduler snapshot completed_prefix");
    }

    if (record.cursor.resume_candidate_node_name != snapshot.cursor.next_candidate_node_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap checkpoint record resume_candidate_node_name does not match scheduler snapshot");
    }

    if (!vector_equals(descriptor.execution_order, record.execution_order)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor execution_order does not match checkpoint record");
    }

    if (!vector_equals(descriptor.cursor.exportable_prefix, record.cursor.persistable_prefix)) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor exportable_prefix does not match checkpoint record persistable_prefix");
    }

    if (descriptor.cursor.next_export_candidate_node_name !=
        record.cursor.resume_candidate_node_name) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor next_export_candidate_node_name does not match checkpoint record");
    }

    if (descriptor.checkpoint_status != record.checkpoint_status) {
        result.diagnostics.error(
            "persistence export manifest bootstrap persistence descriptor checkpoint_status does not match checkpoint record");
    }

    if (result.has_errors()) {
        return result;
    }

    PersistenceExportManifest manifest{
        .format_version = std::string(kPersistenceExportManifestFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_runtime_session_format_version = session.format_version,
        .source_execution_journal_format_version = journal.format_version,
        .source_replay_view_format_version = replay.format_version,
        .source_scheduler_snapshot_format_version = snapshot.format_version,
        .source_checkpoint_record_format_version = record.format_version,
        .source_persistence_descriptor_format_version = descriptor.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = descriptor.workflow_canonical_name,
        .session_id = descriptor.session_id,
        .run_id = descriptor.run_id,
        .input_fixture = descriptor.input_fixture,
        .workflow_status = descriptor.workflow_status,
        .checkpoint_status = descriptor.checkpoint_status,
        .persistence_status = descriptor.persistence_status,
        .workflow_failure_summary = descriptor.workflow_failure_summary,
        .export_package_identity =
            build_export_package_identity(plan.source_package_identity,
                                          descriptor.workflow_canonical_name,
                                          descriptor.session_id,
                                          descriptor.cursor.exportable_prefix_size),
        .planned_durable_identity = descriptor.planned_durable_identity,
        .manifest_boundary_kind = ManifestBoundaryKind::LocalBundleOnly,
        .artifact_bundle = build_default_artifact_bundle(),
        .manifest_ready = false,
        .next_required_artifact_kind = std::nullopt,
        .manifest_status = PersistenceExportManifestStatus::Blocked,
        .store_import_blocker = std::nullopt,
    };

    switch (descriptor.persistence_status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        manifest.manifest_status = PersistenceExportManifestStatus::ReadyToImport;
        manifest.manifest_ready = true;
        break;
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        manifest.manifest_status = PersistenceExportManifestStatus::Blocked;
        manifest.next_required_artifact_kind = ExportArtifactKind::PersistenceDescriptor;
        if (descriptor.persistence_blocker.has_value()) {
            manifest.store_import_blocker = StoreImportBlocker{
                .kind = to_store_import_blocker_kind(descriptor.persistence_blocker->kind),
                .message = descriptor.persistence_blocker->message,
                .logical_artifact_name = "persistence-descriptor.json",
            };
        }
        break;
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        manifest.manifest_status = PersistenceExportManifestStatus::TerminalCompleted;
        manifest.manifest_ready = true;
        break;
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        manifest.manifest_status = PersistenceExportManifestStatus::TerminalFailed;
        if (descriptor.persistence_blocker.has_value()) {
            manifest.store_import_blocker = StoreImportBlocker{
                .kind = to_store_import_blocker_kind(descriptor.persistence_blocker->kind),
                .message = descriptor.persistence_blocker->message,
                .logical_artifact_name = std::nullopt,
            };
        }
        break;
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        manifest.manifest_status = PersistenceExportManifestStatus::TerminalPartial;
        if (descriptor.persistence_blocker.has_value()) {
            manifest.store_import_blocker = StoreImportBlocker{
                .kind = to_store_import_blocker_kind(descriptor.persistence_blocker->kind),
                .message = descriptor.persistence_blocker->message,
                .logical_artifact_name = std::nullopt,
            };
        }
        break;
    }

    const auto manifest_validation = validate_persistence_export_manifest(manifest);
    result.diagnostics.append(manifest_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.manifest = std::move(manifest);
    return result;
}

} // namespace ahfl::persistence_export
