#include "ahfl/runtime/workflow_runtime.hpp"

#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "ahfl/evaluator/evaluator.hpp"

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
    : program_(program), config_(std::move(config)) {}

// ============================================================================
// Lookup Helpers
// ============================================================================

const ir::WorkflowDecl *WorkflowRuntime::find_workflow(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (const auto *wd = std::get_if<ir::WorkflowDecl>(&decl)) {
            if (wd->name == name) {
                return wd;
            }
        }
    }
    return nullptr;
}

const ir::AgentDecl *WorkflowRuntime::find_agent(const std::string &name) const {
    for (const auto &decl : program_.declarations) {
        if (const auto *ad = std::get_if<ir::AgentDecl>(&decl)) {
            if (ad->name == name) {
                return ad;
            }
        }
    }
    return nullptr;
}

const ir::FlowDecl *WorkflowRuntime::find_flow(const std::string &agent_name) const {
    for (const auto &decl : program_.declarations) {
        if (const auto *fd = std::get_if<ir::FlowDecl>(&decl)) {
            if (fd->target == agent_name) {
                return fd;
            }
        }
    }
    return nullptr;
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
    // 统计每个 node 的初始未满足依赖数
    std::unordered_map<std::string, std::size_t> remaining_deps;
    for (const auto &node : workflow.nodes) {
        remaining_deps.emplace(node.name, node.after.size());
    }

    // 先将无依赖的 node 入队
    std::vector<const ir::WorkflowNode *> ready;
    ready.reserve(workflow.nodes.size());
    for (const auto &node : workflow.nodes) {
        if (node.after.empty()) {
            ready.push_back(&node);
        }
    }

    std::vector<const ir::WorkflowNode *> order;
    order.reserve(workflow.nodes.size());

    std::unordered_set<std::string> processed;

    for (std::size_t i = 0; i < ready.size(); ++i) {
        const ir::WorkflowNode *node = ready[i];
        if (processed.contains(node->name)) {
            continue;
        }
        processed.insert(node->name);
        order.push_back(node);

        // 将依赖于当前节点且依赖全部满足的后继节点入队
        for (const auto &candidate : workflow.nodes) {
            if (processed.contains(candidate.name)) {
                continue;
            }
            bool unlocked = false;
            for (const auto &dep : candidate.after) {
                if (dep != node->name) {
                    continue;
                }
                auto &rem = remaining_deps[candidate.name];
                if (rem > 0U) {
                    rem -= 1U;
                }
                unlocked = true;
            }
            if (unlocked && remaining_deps[candidate.name] == 0U) {
                ready.push_back(&candidate);
            }
        }
    }

    return order;
}

// ============================================================================
// eval_node_input
// 构建 EvalContext：
//   - workflow input（StructValue）的各字段注入 input scope
//   - 已完成 node 的输出（StructValue）的各字段注入 node_output scope
// 然后调用 eval_expr 求值
// ============================================================================

evaluator::Value
WorkflowRuntime::eval_node_input(const ir::Expr &input_expr,
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

    auto eval_result = evaluator::eval_expr(input_expr, ctx);
    if (eval_result.has_errors()) {
        return evaluator::make_none();
    }
    return std::move(eval_result.value);
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
            result.output = eval_node_input(*workflow->return_value, input, empty_outputs);
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
                .target = node->target,
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
            evaluator::EvalContext eval_ctx;
            // 将整体 workflow input 绑定为 local "input"，
            // 以便 node 表达式中直接引用 `input` 路径时能解析
            eval_ctx.bind_local("input", evaluator::clone_value(input));
            // 注入 workflow input 的各字段到 input scope
            if (const auto *sv = std::get_if<evaluator::StructValue>(&input.node)) {
                for (const auto &[field_name, field_val] : sv->fields) {
                    if (field_val) {
                        eval_ctx.set_input(field_name, evaluator::clone_value(*field_val));
                    }
                }
            }
            // 注入已完成 node 的输出（同时绑定为 local 和 node_output scope）
            for (const auto &[prev_node_name, prev_val] : node_outputs) {
                eval_ctx.bind_local(prev_node_name, evaluator::clone_value(prev_val));
                if (const auto *sv = std::get_if<evaluator::StructValue>(&prev_val.node)) {
                    for (const auto &[field_name, field_val] : sv->fields) {
                        if (field_val) {
                            eval_ctx.set_node_output(
                                prev_node_name, field_name, evaluator::clone_value(*field_val));
                        }
                    }
                }
            }

            auto eval_result = evaluator::eval_expr(*node->input, eval_ctx);
            if (eval_result.has_errors()) {
                result.status = WorkflowStatus::EvalError;
                result.diagnostics.append(eval_result.diagnostics);
                failed_nodes.insert(node->name);
                result.node_results.push_back(NodeExecutionResult{
                    .node_name = node->name,
                    .target = node->target,
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
        const ir::AgentDecl *agent_decl = find_agent(node->target);
        const ir::FlowDecl *flow_decl = find_flow(node->target);

        if (agent_decl == nullptr || flow_decl == nullptr) {
            result.status = WorkflowStatus::NodeFailed;
            result.diagnostics.error()
                .message("agent '" + node->target + "' declaration or flow not found")
                .emit();
            failed_nodes.insert(node->name);
            result.node_results.push_back(NodeExecutionResult{
                .node_name = node->name,
                .target = node->target,
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
                .target = node->target,
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
                .target = node->target,
                .status = agent_result.status,
                .output = std::nullopt,
                .execution_index = idx,
            });
        }
    }

    // 步骤 5：求值 workflow 返回值（仅在无错误时）
    if (result.status == WorkflowStatus::Completed && workflow->return_value) {
        result.output = eval_node_input(*workflow->return_value, input, node_outputs);
    }

    return result;
}

} // namespace ahfl::runtime
