#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/runtime_session/session.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::execution_journal {

inline constexpr std::string_view kExecutionJournalFormatVersion = "ahfl.execution-journal.v1";

enum class ExecutionJournalEventKind {
    SessionStarted,
    NodeBecameReady,
    NodeStarted,
    NodeCompleted,
    WorkflowCompleted,
};

struct ExecutionJournalEvent {
    ExecutionJournalEventKind kind{ExecutionJournalEventKind::SessionStarted};
    std::string workflow_canonical_name;
    std::optional<std::string> node_name;
    std::optional<std::size_t> execution_index;
    std::vector<std::string> satisfied_dependencies;
    std::vector<std::string> used_mock_selectors;
};

struct ExecutionJournal {
    std::string format_version{std::string(kExecutionJournalFormatVersion)};
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    std::string source_execution_plan_format_version;
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::vector<ExecutionJournalEvent> events;
};

struct ExecutionJournalValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ExecutionJournalResult {
    std::optional<ExecutionJournal> journal;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ExecutionJournalValidationResult
validate_execution_journal(const ExecutionJournal &journal);

[[nodiscard]] ExecutionJournalResult
build_execution_journal(const runtime_session::RuntimeSession &session);

} // namespace ahfl::execution_journal
