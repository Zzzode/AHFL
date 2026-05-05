#pragma once

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/evaluator/eval_context.hpp"
#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/runtime/agent_runtime.hpp"
#include "ahfl/support/diagnostics.hpp"

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

// Capability 调用回调 (用于 CallExpr 在 agent 中的执行)
using CapabilityInvoker =
    std::function<evaluator::Value(const std::string &name,
                                   const std::vector<evaluator::Value> &args)>;

// Workflow Runtime 配置
struct WorkflowRuntimeConfig {
    QuotaConfig default_agent_quota;
    std::optional<CapabilityInvoker> capability_invoker; // v0.55 会提供真实实现
};

// Workflow Runtime
class WorkflowRuntime {
  public:
    WorkflowRuntime(const ir::Program &program, WorkflowRuntimeConfig config = {});

    // 执行指定 workflow
    [[nodiscard]] WorkflowResult run(const std::string &workflow_name, Value input);

  private:
    const ir::Program &program_;
    WorkflowRuntimeConfig config_;

    // 查找声明
    [[nodiscard]] const ir::WorkflowDecl *find_workflow(const std::string &name) const;
    [[nodiscard]] const ir::AgentDecl *find_agent(const std::string &name) const;
    [[nodiscard]] const ir::FlowDecl *find_flow(const std::string &agent_name) const;

    // DAG 调度
    [[nodiscard]] std::vector<const ir::WorkflowNode *>
    topological_sort(const ir::WorkflowDecl &workflow) const;

    // 求值 node input 表达式
    [[nodiscard]] evaluator::Value
    eval_node_input(const ir::Expr &input_expr,
                    const Value &workflow_input,
                    const std::unordered_map<std::string, Value> &node_outputs) const;
};

} // namespace ahfl::runtime
