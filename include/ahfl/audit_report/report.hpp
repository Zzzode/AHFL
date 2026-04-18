#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/dry_run/runner.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::audit_report {

inline constexpr std::string_view kAuditReportFormatVersion = "ahfl.audit-report.v2";

enum class AuditConclusion {
    Passed,
    RuntimeFailed,
    Partial,
};

struct AuditPlanNodeSummary {
    std::string node_name;
    std::string target;
    std::vector<std::string> planned_dependencies;
    std::vector<handoff::CapabilityBindingReference> capability_bindings;
};

struct AuditPlanSummary {
    std::string source_execution_plan_format_version;
    std::vector<std::string> execution_order;
    std::vector<AuditPlanNodeSummary> nodes;
    ir::WorkflowExprSummary return_summary;
};

struct AuditSessionNodeSummary {
    std::string node_name;
    runtime_session::NodeSessionStatus final_status{
        runtime_session::NodeSessionStatus::Blocked};
    std::size_t execution_index{0};
    std::vector<std::string> satisfied_dependencies;
    std::vector<std::string> used_mock_selectors;
};

struct AuditSessionSummary {
    std::string source_runtime_session_format_version{
        std::string(runtime_session::kRuntimeSessionFormatVersion)};
    runtime_session::WorkflowSessionStatus workflow_status{
        runtime_session::WorkflowSessionStatus::Completed};
    std::vector<AuditSessionNodeSummary> nodes;
};

struct AuditJournalSummary {
    std::string source_execution_journal_format_version{
        std::string(execution_journal::kExecutionJournalFormatVersion)};
    std::size_t total_events{0};
    std::size_t node_ready_events{0};
    std::size_t node_started_events{0};
    std::size_t node_completed_events{0};
    std::size_t node_failed_events{0};
    std::size_t workflow_completed_events{0};
    std::size_t workflow_failed_events{0};
    std::vector<std::string> completed_node_order;
};

struct AuditTraceNodeSummary {
    std::string node_name;
    std::size_t execution_index{0};
    std::vector<std::string> mock_result_selectors;
};

struct AuditTraceSummary {
    std::string source_dry_run_trace_format_version{std::string(dry_run::kTraceFormatVersion)};
    dry_run::DryRunStatus status{dry_run::DryRunStatus::Completed};
    std::vector<std::string> execution_order;
    std::vector<AuditTraceNodeSummary> nodes;
    ir::WorkflowExprSummary return_summary;
};

struct AuditConsistencySummary {
    bool plan_matches_session{true};
    bool session_matches_journal{true};
    bool journal_matches_trace{true};
    bool trace_matches_replay{true};
};

struct AuditReport {
    std::string format_version{std::string(kAuditReportFormatVersion)};
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    AuditConclusion conclusion{AuditConclusion::Passed};
    AuditPlanSummary plan_summary;
    AuditSessionSummary session_summary;
    AuditJournalSummary journal_summary;
    AuditTraceSummary trace_summary;
    replay_view::ReplayConsistencySummary replay_consistency;
    AuditConsistencySummary audit_consistency;
};

struct AuditReportValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct AuditReportResult {
    std::optional<AuditReport> report;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] AuditReportValidationResult
validate_audit_report(const AuditReport &report);

[[nodiscard]] AuditReportResult
build_audit_report(const handoff::ExecutionPlan &plan,
                   const runtime_session::RuntimeSession &session,
                   const execution_journal::ExecutionJournal &journal,
                   const dry_run::DryRunTrace &trace);

} // namespace ahfl::audit_report
