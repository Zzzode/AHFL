#include "runtime/engine/workflow_runtime.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "ahfl/compiler/ir/identity.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/evaluator/evaluator.hpp"

namespace ahfl::runtime {

// ============================================================================
// WorkflowResult
// ============================================================================

bool WorkflowResult::has_errors() const {
    return diagnostics.has_error();
}

// ============================================================================
// WorkflowRuntime Construction
// ============================================================================

WorkflowRuntime::WorkflowRuntime(const ir::Program &program, WorkflowRuntimeConfig config)
    : program_(program), index_(program_), config_(std::move(config)) {}

// ============================================================================
// Lookup Helpers
// ============================================================================

const ir::WorkflowDecl *WorkflowRuntime::find_workflow(const std::string &name) const {
    return index_.find_workflow(name);
}

const ir::AgentDecl *WorkflowRuntime::find_agent(const std::string &name) const {
    return index_.find_agent(name);
}

const ir::FlowDecl *WorkflowRuntime::find_flow(const std::string &agent_name) const {
    return index_.find_flow_for_agent(agent_name);
}

// ============================================================================
// DAG Topological Sort
// 与 dry_run/runner.cpp 相同的策略：
//   - 统计每个 node 的剩余依赖数
//   - 先入队无依赖节点
//   - 每次处理一个节点后，减少依赖该节点的所有后继节点的计数
//   - 计数归零即可入队
// ============================================================================

std::vector<const ir::WorkflowNode *>
WorkflowRuntime::topological_sort(const ir::WorkflowDecl &workflow) const {
    return index_.topological_order(workflow);
}

// ============================================================================
// eval_workflow_expression
// 构建 EvalContext：
//   - workflow input（StructValue）的各字段注入 input scope
//   - 已完成 node 的输出（StructValue）的各字段注入 node_output scope
// 然后通过统一 runtime seam 求值；配置了 capability invoker 时支持 CallExpr
// ============================================================================

evaluator::EvalResult WorkflowRuntime::eval_workflow_expression(
    const ir::Expr &expr,
    const Value &workflow_input,
    const std::unordered_map<std::string, Value> &node_outputs) const {
    evaluator::EvalContext ctx;

    // 将整体 workflow input 绑定为 local "input"，支持 `input` 作为简单路径解析
    ctx.bind_local("input", evaluator::clone_value(workflow_input));

    // 注入 workflow input：假设为 StructValue，将字段加入 input scope
    if (const auto *sv = std::get_if<evaluator::StructValue>(&workflow_input.node)) {
        for (const auto &[field_name, field_val] : sv->fields) {
            if (field_val) {
                ctx.set_input(field_name, evaluator::clone_value(*field_val));
            }
        }
    }

    // 注入各 node 的输出：
    // 1. 将字段加入 node_output scope（支持 node_name.field 路径）
    // 2. 将整体输出绑定为 local（支持 node_name 作为简单路径，如 return: summary）
    for (const auto &[node_name, node_val] : node_outputs) {
        ctx.bind_local(node_name, evaluator::clone_value(node_val));
        if (const auto *sv = std::get_if<evaluator::StructValue>(&node_val.node)) {
            for (const auto &[field_name, field_val] : sv->fields) {
                if (field_val) {
                    ctx.set_node_output(node_name, field_name, evaluator::clone_value(*field_val));
                }
            }
        }
    }

    if (config_.capability_invoker.has_value()) {
        return eval_expr_with_capabilities(expr, ctx, *config_.capability_invoker);
    }
    return evaluator::eval_expr(expr, ctx);
}

// ============================================================================
// WorkflowRuntime::run
// ============================================================================

WorkflowResult WorkflowRuntime::run(const std::string &workflow_name, Value input) {
    WorkflowResult result;
    result.status = WorkflowStatus::Completed;

    // 步骤 1：查找 WorkflowDecl
    const ir::WorkflowDecl *workflow = find_workflow(workflow_name);
    if (workflow == nullptr) {
        result.status = WorkflowStatus::NodeFailed;
        result.diagnostics.error()
            .message("workflow '" + workflow_name + "' not found in program")
            .emit();
        return result;
    }

    // 步骤 2：空 workflow 直接求值 return_value（如果有）
    if (workflow->nodes.empty()) {
        if (workflow->return_value) {
            std::unordered_map<std::string, Value> empty_outputs;
            auto return_result =
                eval_workflow_expression(*workflow->return_value, input, empty_outputs);
            if (return_result.has_errors()) {
                result.status = WorkflowStatus::EvalError;
                result.diagnostics.append(return_result.diagnostics);
            } else {
                result.output = std::move(return_result.value);
            }
        }
        return result;
    }

    // 步骤 3：拓扑排序
    auto sorted_nodes = topological_sort(*workflow);

    // 拓扑排序不完整 -> 存在环
    if (sorted_nodes.size() != workflow->nodes.size()) {
        result.status = WorkflowStatus::DependencyFailed;
        result.diagnostics.error()
            .message("workflow '" + workflow_name + "' has a dependency cycle or unresolvable node")
            .emit();
        return result;
    }

    // 步骤 4：按序执行每个 node
    std::unordered_map<std::string, Value> node_outputs;
    std::unordered_set<std::string> failed_nodes;

    for (std::size_t idx = 0; idx < sorted_nodes.size(); ++idx) {
        const ir::WorkflowNode *node = sorted_nodes[idx];
        const auto target = std::string(ir::symbol_canonical_name(node->target_ref));

        // 检查依赖是否有失败
        bool dep_failed = false;
        for (const auto &dep_name : node->after) {
            if (failed_nodes.contains(dep_name)) {
                dep_failed = true;
                break;
            }
        }

        if (dep_failed) {
            // 依赖失败，跳过该 node
            failed_nodes.insert(node->name);
            result.node_results.push_back(NodeExecutionResult{
                .node_name = node->name,
                .target = target,
                .status = AgentStatus::Failed,
                .output = std::nullopt,
                .execution_index = idx,
            });
            result.execution_order.push_back(node->name);
            if (result.status == WorkflowStatus::Completed) {
                result.status = WorkflowStatus::DependencyFailed;
            }
            continue;
        }

        // 步骤 4a：求值 node 的 input 表达式
        evaluator::Value node_input = evaluator::make_none();
        if (node->input) {
            auto eval_result = eval_workflow_expression(*node->input, input, node_outputs);
            if (eval_result.has_errors()) {
                result.status = WorkflowStatus::EvalError;
                result.diagnostics.append(eval_result.diagnostics);
                failed_nodes.insert(node->name);
                result.node_results.push_back(NodeExecutionResult{
                    .node_name = node->name,
                    .target = target,
                    .status = AgentStatus::Failed,
                    .output = std::nullopt,
                    .execution_index = idx,
                });
                result.execution_order.push_back(node->name);
                continue;
            }
            node_input = std::move(eval_result.value);
        }

        // 步骤 4b：查找 AgentDecl 与 FlowDecl
        const ir::AgentDecl *agent_decl = find_agent(target);
        const ir::FlowDecl *flow_decl = find_flow(target);

        if (agent_decl == nullptr || flow_decl == nullptr) {
            result.status = WorkflowStatus::NodeFailed;
            result.diagnostics.error()
                .message("agent '" + target + "' declaration or flow not found")
                .emit();
            failed_nodes.insert(node->name);
            result.node_results.push_back(NodeExecutionResult{
                .node_name = node->name,
                .target = target,
                .status = AgentStatus::Failed,
                .output = std::nullopt,
                .execution_index = idx,
            });
            result.execution_order.push_back(node->name);
            continue;
        }

        // 步骤 4c：构建并运行 AgentRuntime
        AgentRuntime agent_rt(*agent_decl, *flow_decl, config_.default_agent_quota);
        if (config_.capability_invoker.has_value()) {
            agent_rt.set_capability_invoker(*config_.capability_invoker);
        }
        AgentResult agent_result = agent_rt.run(std::move(node_input));

        result.execution_order.push_back(node->name);
        result.diagnostics.append(agent_result.diagnostics);

        if (agent_result.status == AgentStatus::Completed) {
            // 成功：保存输出
            if (agent_result.output.has_value()) {
                node_outputs[node->name] = evaluator::clone_value(*agent_result.output);
            }
            result.node_results.push_back(NodeExecutionResult{
                .node_name = node->name,
                .target = target,
                .status = AgentStatus::Completed,
                .output = std::move(agent_result.output),
                .execution_index = idx,
            });
        } else {
            // 失败：标记 workflow 状态
            if (result.status == WorkflowStatus::Completed) {
                result.status = WorkflowStatus::NodeFailed;
            }
            failed_nodes.insert(node->name);
            result.node_results.push_back(NodeExecutionResult{
                .node_name = node->name,
                .target = target,
                .status = agent_result.status,
                .output = std::nullopt,
                .execution_index = idx,
            });
        }
    }

    // 步骤 5：求值 workflow 返回值（仅在无错误时）
    if (result.status == WorkflowStatus::Completed && workflow->return_value) {
        auto return_result = eval_workflow_expression(*workflow->return_value, input, node_outputs);
        if (return_result.has_errors()) {
            result.status = WorkflowStatus::EvalError;
            result.diagnostics.append(return_result.diagnostics);
        } else {
            result.output = std::move(return_result.value);
        }
    }

    return result;
}

} // namespace ahfl::runtime
