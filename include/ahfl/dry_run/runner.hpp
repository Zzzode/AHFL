#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/handoff/package.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::dry_run {

inline constexpr std::string_view kTraceFormatVersion = "ahfl.dry-run-trace.v1";
inline constexpr std::string_view kCapabilityMockSetFormatVersion =
    "ahfl.capability-mocks.v0.6";

enum class DryRunStatus {
    Completed,
};

struct DryRunRequest {
    std::string workflow_canonical_name;
    std::string input_fixture;
    std::optional<std::string> run_id;
};

struct CapabilityMock {
    std::optional<std::string> capability_name;
    std::optional<std::string> binding_key;
    std::string result_fixture;
    std::optional<std::string> invocation_label;
};

struct CapabilityMockSet {
    std::string format_version{std::string(kCapabilityMockSetFormatVersion)};
    std::vector<CapabilityMock> mocks;
};

struct CapabilityMockSetParseResult {
    std::optional<CapabilityMockSet> mock_set;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct DryRunNodeTrace {
    std::string node_name;
    std::string target;
    std::size_t execution_index{0};
    std::vector<std::string> satisfied_dependencies;
    handoff::WorkflowNodeLifecycleSummary lifecycle;
    ahfl::ir::WorkflowExprSummary input_summary;
    std::vector<handoff::CapabilityBindingReference> capability_bindings;
    std::vector<CapabilityMock> mock_results;
};

struct DryRunTrace {
    std::string format_version{std::string(kTraceFormatVersion)};
    std::string source_execution_plan_format_version;
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    DryRunStatus status{DryRunStatus::Completed};
    std::string input_fixture;
    std::optional<std::string> run_id;
    std::vector<std::string> execution_order;
    std::vector<DryRunNodeTrace> node_traces;
    ahfl::ir::WorkflowExprSummary return_summary;
};

struct DryRunResult {
    std::optional<DryRunTrace> trace;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] DryRunResult run_local_dry_run(const handoff::ExecutionPlan &plan,
                                             const DryRunRequest &request,
                                             const CapabilityMockSet &mock_set);

[[nodiscard]] CapabilityMockSetParseResult
parse_capability_mock_set_json(std::string_view content);

} // namespace ahfl::dry_run
