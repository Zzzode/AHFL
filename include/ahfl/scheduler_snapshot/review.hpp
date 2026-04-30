#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/scheduler_snapshot/snapshot.hpp"

namespace ahfl::scheduler_snapshot {

inline constexpr std::string_view kSchedulerDecisionSummaryFormatVersion =
    "ahfl.scheduler-review.v1";

enum class SchedulerNextActionKind {
    RunReadyNode,
    AwaitDependencies,
    WorkflowCompleted,
    InvestigateFailure,
    PreservePartialState,
};

struct SchedulerReviewReadyNode {
    std::string node_name;
    std::string target;
    std::size_t execution_index{0};
    std::vector<std::string> planned_dependencies;
    std::vector<std::string> satisfied_dependencies;
};

struct SchedulerReviewBlockedNode {
    std::string node_name;
    std::string target;
    std::optional<std::size_t> execution_index;
    SchedulerBlockedReasonKind blocked_reason{SchedulerBlockedReasonKind::WaitingOnDependencies};
    std::vector<std::string> missing_dependencies;
    bool may_become_ready{true};
};

struct SchedulerDecisionSummary {
    std::string format_version{std::string(kSchedulerDecisionSummaryFormatVersion)};
    std::string source_scheduler_snapshot_format_version{
        std::string(kSchedulerSnapshotFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    SchedulerSnapshotStatus snapshot_status{SchedulerSnapshotStatus::Waiting};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::size_t completed_prefix_size{0};
    std::vector<std::string> completed_prefix;
    std::optional<std::string> next_candidate_node_name;
    bool checkpoint_friendly{true};
    std::vector<SchedulerReviewReadyNode> ready_nodes;
    std::vector<SchedulerReviewBlockedNode> blocked_nodes;
    std::optional<std::string> terminal_reason;
    SchedulerNextActionKind next_action{SchedulerNextActionKind::AwaitDependencies};
    std::string next_step_recommendation;
};

struct SchedulerDecisionSummaryValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct SchedulerDecisionSummaryResult {
    std::optional<SchedulerDecisionSummary> summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] SchedulerDecisionSummaryValidationResult
validate_scheduler_decision_summary(const SchedulerDecisionSummary &summary);

[[nodiscard]] SchedulerDecisionSummaryResult
build_scheduler_decision_summary(const SchedulerSnapshot &snapshot);

} // namespace ahfl::scheduler_snapshot
