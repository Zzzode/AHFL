#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ahfl/ir/ir.hpp"
#include "ahfl/support/diagnostics.hpp"

namespace ahfl::handoff {

inline constexpr std::string_view kFormatVersion = "ahfl.native-package.v1";
inline constexpr std::string_view kExecutionPlanFormatVersion = "ahfl.execution-plan.v1";

enum class ExecutableKind {
    Agent,
    Workflow,
};

enum class WorkflowNodeStartConditionKind {
    Immediate,
    AfterDependenciesCompleted,
};

enum class WorkflowNodeCompletionConditionKind {
    TargetReachedFinalState,
};

enum class PolicyObligationKind {
    Requires,
    Ensures,
    Invariant,
    Forbid,
    WorkflowSafety,
    WorkflowLiveness,
};

struct ExecutableRef {
    ExecutableKind kind{ExecutableKind::Workflow};
    std::string canonical_name;

    [[nodiscard]] friend bool operator==(const ExecutableRef &lhs,
                                         const ExecutableRef &rhs) noexcept = default;
};

struct PackageIdentity {
    std::string format_version{std::string(kFormatVersion)};
    std::string name;
    std::string version;
};

struct PackageMetadata {
    std::optional<PackageIdentity> identity;
    std::optional<ExecutableRef> entry_target;
    std::vector<ExecutableRef> export_targets;
    std::unordered_map<std::string, std::string> capability_binding_keys;
};

struct AgentExecutable {
    ir::DeclarationProvenance provenance;
    std::string canonical_name;
    std::string input_type;
    std::string context_type;
    std::string output_type;
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<std::string> capabilities;
};

struct WorkflowDependencyEdge {
    std::string from_node;
    std::string to_node;
};

struct WorkflowExecutionGraph {
    std::vector<std::string> entry_nodes;
    std::vector<WorkflowDependencyEdge> dependency_edges;
};

struct WorkflowNodeLifecycleSummary {
    WorkflowNodeStartConditionKind start_condition{WorkflowNodeStartConditionKind::Immediate};
    WorkflowNodeCompletionConditionKind completion_condition{
        WorkflowNodeCompletionConditionKind::TargetReachedFinalState};
    bool completion_latched{true};
    std::string target_initial_state;
    std::vector<std::string> target_final_states;
};

struct WorkflowExecutionNode {
    std::string name;
    std::string target;
    ir::WorkflowExprSummary input_summary;
    std::vector<std::string> after;
    WorkflowNodeLifecycleSummary lifecycle;
};

struct WorkflowExecutable {
    ir::DeclarationProvenance provenance;
    std::string canonical_name;
    std::string input_type;
    std::string output_type;
    WorkflowExecutionGraph execution_graph;
    std::vector<WorkflowExecutionNode> nodes;
    std::size_t safety_clause_count{0};
    std::size_t liveness_clause_count{0};
    ir::WorkflowExprSummary return_summary;
};

using ExecutableTarget = std::variant<AgentExecutable, WorkflowExecutable>;

struct CapabilityBindingSlot {
    std::string capability_name;
    std::string binding_key;
    std::vector<ExecutableRef> required_by_targets;
};

struct PolicyObligation {
    ExecutableRef owner_target;
    PolicyObligationKind kind{PolicyObligationKind::Requires};
    std::size_t clause_index{0};
    std::vector<std::string> observation_symbols;
};

struct Package {
    PackageMetadata metadata;
    std::vector<ExecutableTarget> executable_targets;
    std::vector<CapabilityBindingSlot> capability_binding_slots;
    std::vector<PolicyObligation> policy_obligations;
    std::vector<ir::FormalObservation> formal_observations;
};

struct PackageReaderObservationSummary {
    std::size_t total{0};
    std::size_t called_capability{0};
    std::size_t embedded_bool_expr{0};
};

struct PackageReaderSummary {
    std::string format_version{std::string(kFormatVersion)};
    std::optional<PackageIdentity> identity;
    std::optional<ExecutableRef> entry_target;
    std::vector<ExecutableRef> export_targets;
    std::vector<CapabilityBindingSlot> capability_binding_slots;
    std::vector<PolicyObligation> policy_obligations;
    PackageReaderObservationSummary formal_observations;
};

struct ExecutionPlannerBootstrapNode {
    std::string name;
    std::string target;
    std::vector<std::string> after;
    WorkflowNodeLifecycleSummary lifecycle;
    ir::WorkflowExprSummary input_summary;
};

struct ExecutionPlannerBootstrap {
    std::string workflow_canonical_name;
    std::string input_type;
    std::string output_type;
    std::vector<std::string> entry_nodes;
    std::vector<WorkflowDependencyEdge> dependency_edges;
    std::vector<ExecutionPlannerBootstrapNode> nodes;
    ir::WorkflowExprSummary return_summary;
};

struct CapabilityBindingReference {
    std::string capability_name;
    std::string binding_key;

    [[nodiscard]] friend bool operator==(const CapabilityBindingReference &lhs,
                                         const CapabilityBindingReference &rhs) noexcept = default;
};

struct WorkflowNodePlan {
    std::string name;
    std::string target;
    std::vector<std::string> after;
    WorkflowNodeLifecycleSummary lifecycle;
    ir::WorkflowExprSummary input_summary;
    std::vector<CapabilityBindingReference> capability_bindings;
};

struct WorkflowPlan {
    std::string workflow_canonical_name;
    std::string input_type;
    std::string output_type;
    std::vector<std::string> entry_nodes;
    std::vector<WorkflowDependencyEdge> dependency_edges;
    std::vector<WorkflowNodePlan> nodes;
    ir::WorkflowExprSummary return_summary;
};

struct ExecutionPlan {
    std::string format_version{std::string(kExecutionPlanFormatVersion)};
    std::optional<PackageIdentity> source_package_identity;
    std::optional<std::string> entry_workflow_canonical_name;
    std::vector<WorkflowPlan> workflows;
};

struct PackageMetadataValidationResult {
    PackageMetadata metadata;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct PackageReaderSummaryResult {
    PackageReaderSummary summary;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ExecutionPlannerBootstrapResult {
    std::optional<ExecutionPlannerBootstrap> bootstrap;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ExecutionPlanResult {
    std::optional<ExecutionPlan> plan;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ExecutionPlanValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] PackageMetadataValidationResult
validate_package_metadata(const ir::Program &program, PackageMetadata metadata);

[[nodiscard]] Package lower_package(const ir::Program &program, PackageMetadata metadata = {});

[[nodiscard]] PackageReaderSummaryResult build_package_reader_summary(const Package &package);

[[nodiscard]] ExecutionPlannerBootstrapResult
build_execution_planner_bootstrap(const Package &package, std::string_view workflow_canonical_name);

[[nodiscard]] ExecutionPlannerBootstrapResult
build_entry_execution_planner_bootstrap(const Package &package);

[[nodiscard]] ExecutionPlanValidationResult validate_execution_plan(const ExecutionPlan &plan);

[[nodiscard]] ExecutionPlanResult build_execution_plan(const Package &package);

[[nodiscard]] const AgentExecutable *find_agent_executable(const Package &package,
                                                           std::string_view canonical_name);

[[nodiscard]] const WorkflowExecutable *find_workflow_executable(const Package &package,
                                                                 std::string_view canonical_name);

[[nodiscard]] const CapabilityBindingSlot *
find_capability_binding_slot(const Package &package, std::string_view capability_name);

[[nodiscard]] const PolicyObligation *find_policy_obligation(const Package &package,
                                                             std::string_view owner_canonical_name,
                                                             PolicyObligationKind kind,
                                                             std::size_t clause_index);

} // namespace ahfl::handoff
