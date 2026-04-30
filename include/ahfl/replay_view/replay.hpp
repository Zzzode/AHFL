#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::replay_view {

inline constexpr std::string_view kReplayViewFormatVersion = "ahfl.replay-view.v2";

enum class ReplayStatus {
    Consistent,
    RuntimeFailed,
    Partial,
};

struct ReplayConsistencySummary {
    bool plan_matches_session{true};
    bool session_matches_journal{true};
    bool journal_matches_execution_order{true};
};

struct ReplayNodeProgression {
    std::string node_name;
    std::string target;
    std::size_t execution_index{0};
    std::vector<std::string> planned_dependencies;
    std::vector<std::string> satisfied_dependencies;
    bool saw_node_became_ready{false};
    bool saw_node_started{false};
    bool saw_node_completed{false};
    bool saw_node_failed{false};
    std::optional<runtime_session::RuntimeFailureSummary> failure_summary;
    std::vector<std::string> used_mock_selectors;
    runtime_session::NodeSessionStatus final_status{runtime_session::NodeSessionStatus::Blocked};
};

struct ReplayView {
    std::string format_version{std::string(kReplayViewFormatVersion)};
    std::string source_execution_plan_format_version;
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    std::string source_execution_journal_format_version{
        std::string(execution_journal::kExecutionJournalFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    ReplayStatus replay_status{ReplayStatus::Consistent};
    std::optional<runtime_session::RuntimeFailureSummary> workflow_failure_summary;
    std::vector<std::string> execution_order;
    std::vector<ReplayNodeProgression> nodes;
    ReplayConsistencySummary consistency;
};

struct ReplayViewResult {
    std::optional<ReplayView> replay;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ReplayViewValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ReplayViewValidationResult validate_replay_view(const ReplayView &replay);

[[nodiscard]] ReplayViewResult
build_replay_view(const handoff::ExecutionPlan &plan,
                  const runtime_session::RuntimeSession &session,
                  const execution_journal::ExecutionJournal &journal);

} // namespace ahfl::replay_view
