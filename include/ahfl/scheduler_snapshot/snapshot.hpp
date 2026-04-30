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
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::scheduler_snapshot {

inline constexpr std::string_view kSchedulerSnapshotFormatVersion = "ahfl.scheduler-snapshot.v1";

enum class SchedulerSnapshotStatus {
    Runnable,
    Waiting,
    TerminalCompleted,
    TerminalFailed,
    TerminalPartial,
};

enum class SchedulerBlockedReasonKind {
    WaitingOnDependencies,
    WorkflowTerminalFailure,
    UpstreamPartial,
};

struct SchedulerReadyNode {
    std::string node_name;
    std::string target;
    std::size_t execution_index{0};
    std::vector<std::string> planned_dependencies;
    std::vector<std::string> satisfied_dependencies;
    ahfl::ir::WorkflowExprSummary input_summary;
    std::vector<handoff::CapabilityBindingReference> capability_bindings;
};

struct SchedulerBlockedNode {
    std::string node_name;
    std::string target;
    std::optional<std::size_t> execution_index;
    std::vector<std::string> planned_dependencies;
    std::vector<std::string> missing_dependencies;
    SchedulerBlockedReasonKind blocked_reason{SchedulerBlockedReasonKind::WaitingOnDependencies};
    bool may_become_ready{true};
    std::optional<runtime_session::RuntimeFailureSummary> blocking_failure_summary;
};

struct SchedulerCursor {
    std::size_t completed_prefix_size{0};
    std::vector<std::string> completed_prefix;
    std::optional<std::string> next_candidate_node_name;
    bool checkpoint_friendly{true};
};

struct SchedulerSnapshot {
    std::string format_version{std::string(kSchedulerSnapshotFormatVersion)};
    std::string source_execution_plan_format_version;
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    std::string source_execution_journal_format_version{
        std::string(execution_journal::kExecutionJournalFormatVersion)};
    std::string source_replay_view_format_version{
        std::string(replay_view::kReplayViewFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    SchedulerSnapshotStatus snapshot_status{SchedulerSnapshotStatus::Waiting};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::vector<std::string> execution_order;
    std::vector<SchedulerReadyNode> ready_nodes;
    std::vector<SchedulerBlockedNode> blocked_nodes;
    SchedulerCursor cursor;
};

struct SchedulerSnapshotValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct SchedulerSnapshotResult {
    std::optional<SchedulerSnapshot> snapshot;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] SchedulerSnapshotValidationResult
validate_scheduler_snapshot(const SchedulerSnapshot &snapshot);

[[nodiscard]] SchedulerSnapshotResult
build_scheduler_snapshot(const handoff::ExecutionPlan &plan,
                         const runtime_session::RuntimeSession &session,
                         const execution_journal::ExecutionJournal &journal,
                         const replay_view::ReplayView &replay);

} // namespace ahfl::scheduler_snapshot
