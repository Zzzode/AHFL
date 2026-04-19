#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::persistence_descriptor {

inline constexpr std::string_view kPersistenceDescriptorFormatVersion =
    "ahfl.persistence-descriptor.v1";

enum class PersistenceDescriptorStatus {
    ReadyToExport,
    Blocked,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class PersistenceBasisKind {
    LocalPlanningOnly,
    StoreAdjacent,
};

enum class PersistenceBlockerKind {
    WaitingOnCheckpointState,
    MissingPlannedDurableIdentity,
    WorkflowFailure,
    WorkflowPartial,
};

struct PersistenceBlocker {
    PersistenceBlockerKind kind{PersistenceBlockerKind::WaitingOnCheckpointState};
    std::string message;
    std::optional<std::string> node_name;
};

struct PersistenceCursor {
    std::size_t exportable_prefix_size{0};
    std::vector<std::string> exportable_prefix;
    std::optional<std::string> next_export_candidate_node_name;
    bool export_ready{false};
};

struct CheckpointPersistenceDescriptor {
    std::string format_version{std::string(kPersistenceDescriptorFormatVersion)};
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
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    checkpoint_record::CheckpointRecordStatus checkpoint_status{
        checkpoint_record::CheckpointRecordStatus::Blocked};
    PersistenceDescriptorStatus persistence_status{
        PersistenceDescriptorStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::vector<std::string> execution_order;
    std::string planned_durable_identity;
    PersistenceBasisKind export_basis_kind{PersistenceBasisKind::LocalPlanningOnly};
    PersistenceCursor cursor;
    std::optional<PersistenceBlocker> persistence_blocker;
};

struct PersistenceDescriptorValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PersistenceDescriptorResult {
    std::optional<CheckpointPersistenceDescriptor> descriptor;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] PersistenceDescriptorValidationResult
validate_persistence_descriptor(const CheckpointPersistenceDescriptor &descriptor);

[[nodiscard]] PersistenceDescriptorResult
build_persistence_descriptor(const handoff::ExecutionPlan &plan,
                             const runtime_session::RuntimeSession &session,
                             const execution_journal::ExecutionJournal &journal,
                             const replay_view::ReplayView &replay,
                             const scheduler_snapshot::SchedulerSnapshot &snapshot,
                             const checkpoint_record::CheckpointRecord &record);

} // namespace ahfl::persistence_descriptor
