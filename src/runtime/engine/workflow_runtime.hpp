#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/engine/agent_runtime.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

// Workflow 执行状态
enum class WorkflowStatus {
    Completed,
    NodeFailed,
    DependencyFailed,
    EvalError,
};

// 单个 Node 的执行结果
struct NodeExecutionResult {
    std::string node_name;
    std::string target;
    AgentStatus status;
    std::optional<Value> output;
    std::size_t execution_index;
};

// Workflow 执行结果
struct WorkflowResult {
    WorkflowStatus status;
    std::optional<Value> output;
    std::vector<std::string> execution_order;
    std::vector<NodeExecutionResult> node_results;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const;
};

// Workflow Runtime 配置
struct WorkflowRuntimeConfig {
    QuotaConfig default_agent_quota;
    std::optional<CapabilityInvoker> capability_invoker;
    std::optional<ContextualCapabilityInvoker> contextual_capability_invoker;
};

// Workflow Runtime
class WorkflowRuntime {
  public:
    WorkflowRuntime(const ir::Program &program, WorkflowRuntimeConfig config = {});

    // 执行指定 workflow
    [[nodiscard]] WorkflowResult run(const std::string &workflow_name, Value input);

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;
    WorkflowRuntimeConfig config_;

    // 查找声明
    [[nodiscard]] const ir::WorkflowDecl *find_workflow(const std::string &name) const;
    [[nodiscard]] const ir::AgentDecl *find_agent(const std::string &name) const;
    [[nodiscard]] const ir::FlowDecl *find_flow(const std::string &agent_name) const;

    // DAG 调度
    [[nodiscard]] std::vector<const ir::WorkflowNode *>
    topological_sort(const ir::WorkflowDecl &workflow) const;

    // 求值 workflow 表达式（node input 与 workflow return 共用同一个 runtime seam）
    [[nodiscard]] evaluator::EvalResult
    eval_workflow_expression(const ir::Expr &expr,
                             const Value &workflow_input,
                             const std::unordered_map<std::string, Value> &node_outputs,
                             const CapabilityInvocationContext &context = {}) const;
};

} // namespace ahfl::runtime
