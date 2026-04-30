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
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::store_import {

inline constexpr std::string_view kStoreImportDescriptorFormatVersion =
    "ahfl.store-import-descriptor.v1";

enum class StoreImportDescriptorStatus {
    ReadyToImport,
    Blocked,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class StoreImportBoundaryKind {
    LocalStagingOnly,
    AdapterConsumable,
};

enum class StagingArtifactKind {
    ExportManifest,
    PersistenceDescriptor,
    PersistenceReview,
    CheckpointRecord,
};

enum class StagingBlockerKind {
    WaitingOnExportManifest,
    MissingStoreImportCandidateIdentity,
    MissingStagingArtifactSet,
    WorkflowFailure,
    WorkflowPartial,
};

struct StagingArtifactEntry {
    StagingArtifactKind artifact_kind{StagingArtifactKind::ExportManifest};
    std::string logical_artifact_name;
    std::string source_format_version;
    bool required_for_import{true};
};

struct StagingArtifactSet {
    std::size_t entry_count{0};
    std::vector<StagingArtifactEntry> entries;
};

struct StagingBlocker {
    StagingBlockerKind kind{StagingBlockerKind::WaitingOnExportManifest};
    std::string message;
    std::optional<std::string> logical_artifact_name;
};

struct StoreImportDescriptor {
    std::string format_version{std::string(kStoreImportDescriptorFormatVersion)};
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
    std::string source_export_manifest_format_version{
        std::string(persistence_export::kPersistenceExportManifestFormatVersion)};
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
    persistence_export::PersistenceExportManifestStatus manifest_status{
        persistence_export::PersistenceExportManifestStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::string export_package_identity;
    std::string store_import_candidate_identity;
    std::string planned_durable_identity;
    StoreImportBoundaryKind descriptor_boundary_kind{StoreImportBoundaryKind::LocalStagingOnly};
    StagingArtifactSet staging_artifact_set;
    bool import_ready{false};
    std::optional<StagingArtifactKind> next_required_staging_artifact_kind;
    StoreImportDescriptorStatus descriptor_status{StoreImportDescriptorStatus::Blocked};
    std::optional<StagingBlocker> staging_blocker;
};

struct StoreImportDescriptorValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct StoreImportDescriptorResult {
    std::optional<StoreImportDescriptor> descriptor;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] StoreImportDescriptorValidationResult
validate_store_import_descriptor(const StoreImportDescriptor &descriptor);

[[nodiscard]] StoreImportDescriptorResult build_store_import_descriptor(
    const handoff::ExecutionPlan &plan,
    const runtime_session::RuntimeSession &session,
    const execution_journal::ExecutionJournal &journal,
    const replay_view::ReplayView &replay,
    const scheduler_snapshot::SchedulerSnapshot &snapshot,
    const checkpoint_record::CheckpointRecord &record,
    const persistence_descriptor::CheckpointPersistenceDescriptor &persistence_descriptor,
    const persistence_export::PersistenceExportManifest &manifest);

} // namespace ahfl::store_import
