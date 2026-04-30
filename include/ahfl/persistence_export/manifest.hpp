#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::persistence_export {

inline constexpr std::string_view kPersistenceExportManifestFormatVersion =
    "ahfl.persistence-export-manifest.v1";

enum class PersistenceExportManifestStatus {
    ReadyToImport,
    Blocked,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class ManifestBoundaryKind {
    LocalBundleOnly,
    StoreImportAdjacent,
};

enum class ExportArtifactKind {
    PersistenceDescriptor,
    PersistenceReview,
    CheckpointRecord,
};

enum class StoreImportBlockerKind {
    WaitingOnPersistenceState,
    MissingExportPackageIdentity,
    MissingArtifactBundle,
    WorkflowFailure,
    WorkflowPartial,
};

struct ExportArtifactEntry {
    ExportArtifactKind artifact_kind{ExportArtifactKind::PersistenceDescriptor};
    std::string logical_artifact_name;
    std::string source_format_version;
};

struct ExportArtifactBundle {
    std::size_t entry_count{0};
    std::vector<ExportArtifactEntry> entries;
};

struct StoreImportBlocker {
    StoreImportBlockerKind kind{StoreImportBlockerKind::WaitingOnPersistenceState};
    std::string message;
    std::optional<std::string> logical_artifact_name;
};

struct PersistenceExportManifest {
    std::string format_version{std::string(kPersistenceExportManifestFormatVersion)};
    std::string source_execution_plan_format_version{
        std::string(handoff::kExecutionPlanFormatVersion)};
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    std::string source_execution_journal_format_version{
        std::string(execution_journal::kExecutionJournalFormatVersion)};
    std::string source_replay_view_format_version{
        std::string(replay_view::kReplayViewFormatVersion)};
    std::string source_scheduler_snapshot_format_version{
        std::string(scheduler_snapshot::kSchedulerSnapshotFormatVersion)};
    std::string source_checkpoint_record_format_version{
        std::string(checkpoint_record::kCheckpointRecordFormatVersion)};
    std::string source_persistence_descriptor_format_version{
        std::string(persistence_descriptor::kPersistenceDescriptorFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    checkpoint_record::CheckpointRecordStatus checkpoint_status{
        checkpoint_record::CheckpointRecordStatus::Blocked};
    persistence_descriptor::PersistenceDescriptorStatus persistence_status{
        persistence_descriptor::PersistenceDescriptorStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string planned_durable_identity;
    ManifestBoundaryKind manifest_boundary_kind{ManifestBoundaryKind::LocalBundleOnly};
    ExportArtifactBundle artifact_bundle;
    bool manifest_ready{false};
    std::optional<ExportArtifactKind> next_required_artifact_kind;
    PersistenceExportManifestStatus manifest_status{PersistenceExportManifestStatus::Blocked};
    std::optional<StoreImportBlocker> store_import_blocker;
};

struct PersistenceExportManifestValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PersistenceExportManifestResult {
    std::optional<PersistenceExportManifest> manifest;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] PersistenceExportManifestValidationResult
validate_persistence_export_manifest(const PersistenceExportManifest &manifest);

[[nodiscard]] PersistenceExportManifestResult build_persistence_export_manifest(
    const handoff::ExecutionPlan &plan,
    const runtime_session::RuntimeSession &session,
    const execution_journal::ExecutionJournal &journal,
    const replay_view::ReplayView &replay,
    const scheduler_snapshot::SchedulerSnapshot &snapshot,
    const checkpoint_record::CheckpointRecord &record,
    const persistence_descriptor::CheckpointPersistenceDescriptor &descriptor);

} // namespace ahfl::persistence_export
