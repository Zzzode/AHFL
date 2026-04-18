#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::runtime_session {

inline constexpr std::string_view kRuntimeSessionFormatVersion = "ahfl.runtime-session.v1";

enum class WorkflowSessionStatus {
    Completed,
};

enum class NodeSessionStatus {
    Blocked,
    Ready,
    Completed,
};

struct RuntimeSessionOptions {
    bool sequential_mode{true};
};

struct RuntimeSessionMockUsage {
    std::string selector;
    std::optional<std::string> capability_name;
    std::optional<std::string> binding_key;
    std::string result_fixture;
    std::optional<std::string> invocation_label;
};

struct RuntimeSessionNode {
    std::string node_name;
    std::string target;
    NodeSessionStatus status{NodeSessionStatus::Blocked};
    std::size_t execution_index{0};
    std::vector<std::string> satisfied_dependencies;
    handoff::WorkflowNodeLifecycleSummary lifecycle;
    ahfl::ir::WorkflowExprSummary input_summary;
    std::vector<handoff::CapabilityBindingReference> capability_bindings;
    std::vector<RuntimeSessionMockUsage> used_mocks;
};

struct RuntimeSession {
    std::string format_version{std::string(kRuntimeSessionFormatVersion)};
    std::string source_execution_plan_format_version;
    std::optional<handoff::PackageIdentity> source_package_identity;
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    WorkflowSessionStatus workflow_status{WorkflowSessionStatus::Completed};
    std::string input_fixture;
    RuntimeSessionOptions options;
    std::vector<std::string> execution_order;
    std::vector<RuntimeSessionNode> nodes;
    ahfl::ir::WorkflowExprSummary return_summary;
};

struct RuntimeSessionResult {
    std::optional<RuntimeSession> session;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct RuntimeSessionValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] RuntimeSessionValidationResult
validate_runtime_session(const RuntimeSession &session);

[[nodiscard]] RuntimeSessionResult
build_runtime_session(const handoff::ExecutionPlan &plan,
                      const dry_run::DryRunRequest &request,
                      const dry_run::CapabilityMockSet &mock_set,
                      const RuntimeSessionOptions &options = {});

} // namespace ahfl::runtime_session
