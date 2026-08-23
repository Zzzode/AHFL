#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "ahfl/runtime/execution_metadata.hpp"
#include "ahfl/runtime/execution_report.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

// Workflow execution status
enum class WorkflowStatus {
    Completed,
    NodeFailed,
    DependencyFailed,
    EvalError,
};

// Workflow execution result
struct WorkflowResult {
    ExecutionMetadataStore metadata;
    ExecutionEventStore events;
    ExecutionReport report;
    std::vector<Value> values;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const;
    [[nodiscard]] WorkflowStatus status() const noexcept;
    [[nodiscard]] const Value *value(RuntimeValueId id) const noexcept;
    [[nodiscard]] const Value *output() const noexcept;
};

// Workflow runtime configuration
struct WorkflowRuntimeConfig {
    QuotaConfig default_agent_quota;
    std::optional<CapabilityInvoker> capability_invoker;
    std::optional<ContextualCapabilityInvoker> contextual_capability_invoker;
    std::function<bool()> cancellation_requested;
    std::function<bool()> interruption_requested;
    std::optional<CheckpointId> resume_checkpoint;
    std::function<std::optional<CheckpointId>(WorkflowNodeId)> checkpoint_after_node;
    std::optional<WorkflowRecoverySnapshot> recovery_snapshot;
    WorkflowRecoveryStore *recovery_store{nullptr};
    std::function<void(const CapabilityInvocationContext &, const CapabilityCallResult &)>
        capability_result_observer;
    // Debug/test hook invoked after the runtime records an agent state entry.
    // Runs on the workflow execution thread; a debugger may block inside this
    // hook to implement pause (RFC 0015).
    std::function<void(AgentId, std::string_view)> state_entered_hook;
};

// Workflow runtime
class WorkflowRuntime {
  public:
    WorkflowRuntime(const ir::Program &program, WorkflowRuntimeConfig config = {});

    // Execute the specified workflow
    [[nodiscard]] WorkflowResult run(const std::string &workflow_name, Value input);

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;
    WorkflowRuntimeConfig config_;

    // Source-boundary declaration lookup. Runtime execution materializes these
    // names into strong IDs before scheduling starts.
    [[nodiscard]] const ir::WorkflowDecl *find_workflow(const std::string &name) const;
    [[nodiscard]] const ir::AgentDecl *find_agent(const std::string &name) const;
    [[nodiscard]] const ir::FlowDecl *find_flow(const std::string &agent_name) const;

    [[nodiscard]] evaluator::EvalResult eval_workflow_expression(
        const ir::Expr &expr,
        const Value &workflow_input,
        const std::vector<std::optional<RuntimeValueId>> &node_outputs,
        const WorkflowResult &result,
        const ContextualCapabilityInvoker *runtime_invoker,
        const CapabilityInvocationContext &context = {}) const;
};

} // namespace ahfl::runtime
