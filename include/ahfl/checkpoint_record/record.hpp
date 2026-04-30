#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::checkpoint_record {

inline constexpr std::string_view kCheckpointRecordFormatVersion = "ahfl.checkpoint-record.v1";

enum class CheckpointRecordStatus {
    ReadyToPersist,
    Blocked,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class CheckpointBasisKind {
    LocalOnly,
    DurableAdjacent,
};

enum class CheckpointResumeBlockerKind {
    NotCheckpointFriendly,
    WaitingOnSchedulerState,
    WorkflowFailure,
    WorkflowPartial,
};

struct CheckpointResumeBlocker {
    CheckpointResumeBlockerKind kind{CheckpointResumeBlockerKind::WaitingOnSchedulerState};
    std::string message;
    std::optional<std::string> node_name;
};

struct CheckpointCursor {
    std::size_t persistable_prefix_size{0};
    std::vector<std::string> persistable_prefix;
    std::optional<std::string> resume_candidate_node_name;
    bool resume_ready{false};
};

struct CheckpointRecord {
    std::string format_version{std::string(kCheckpointRecordFormatVersion)};
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
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    scheduler_snapshot::SchedulerSnapshotStatus snapshot_status{
        scheduler_snapshot::SchedulerSnapshotStatus::Waiting};
    CheckpointRecordStatus checkpoint_status{CheckpointRecordStatus::Blocked};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::vector<std::string> execution_order;
    CheckpointBasisKind basis_kind{CheckpointBasisKind::LocalOnly};
    bool checkpoint_friendly_source{true};
    CheckpointCursor cursor;
    std::optional<CheckpointResumeBlocker> resume_blocker;
};

struct CheckpointRecordValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct CheckpointRecordResult {
    std::optional<CheckpointRecord> record;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] CheckpointRecordValidationResult
validate_checkpoint_record(const CheckpointRecord &record);

[[nodiscard]] CheckpointRecordResult
build_checkpoint_record(const handoff::ExecutionPlan &plan,
                        const runtime_session::RuntimeSession &session,
                        const execution_journal::ExecutionJournal &journal,
                        const replay_view::ReplayView &replay,
                        const scheduler_snapshot::SchedulerSnapshot &snapshot);

} // namespace ahfl::checkpoint_record
