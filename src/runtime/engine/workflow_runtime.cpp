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
// Same strategy as dry_run/runner.cpp:
//   - Count the remaining dependencies of each node
//   - Enqueue dependency-free nodes first
//   - After processing each node, decrement the dependency count of all successors that depend on it
//   - Once a count reaches zero, the node can be enqueued
// ============================================================================

std::vector<const ir::WorkflowNode *>
WorkflowRuntime::topological_sort(const ir::WorkflowDecl &workflow) const {
    return index_.topological_order(workflow);
}

// ============================================================================
// eval_workflow_expression
// Build EvalContext:
//   - Inject each field of the workflow input (StructValue) into the input scope
//   - Inject each field of the completed node output (StructValue) into the node_output scope
// Then evaluate through the unified runtime seam; CallExpr is supported when a capability invoker is configured
// ============================================================================

evaluator::EvalResult WorkflowRuntime::eval_workflow_expression(
    const ir::Expr &expr,
    const Value &workflow_input,
    const std::unordered_map<std::string, Value> &node_outputs,
    const CapabilityInvocationContext &context) const {
    evaluator::EvalContext ctx;

    // Bind the whole workflow input as local "input" so `input` resolves as a simple path
    ctx.bind_local("input", evaluator::clone_value(workflow_input));

    // Inject the workflow input: assume a StructValue and add its fields to the input scope
    if (const auto *sv = std::get_if<evaluator::StructValue>(&workflow_input.node)) {
        for (const auto &[field_name, field_val] : sv->fields) {
            if (field_val) {
                ctx.set_input(field_name, evaluator::clone_value(*field_val));
            }
        }
    }

    // Inject each node's output:
    // 1. Add fields to the node_output scope (supports node_name.field paths)
    // 2. Bind the whole output as local (supports node_name as a simple path, e.g. return: summary)
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

    if (config_.contextual_capability_invoker.has_value()) {
        return eval_expr_with_capabilities(
            expr, ctx, *config_.contextual_capability_invoker, context);
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

    // Step 1: Look up the WorkflowDecl
    const ir::WorkflowDecl *workflow = find_workflow(workflow_name);
    if (workflow == nullptr) {
        result.status = WorkflowStatus::NodeFailed;
        result.diagnostics.error()
            .message("workflow '" + workflow_name + "' not found in program")
            .emit();
        return result;
    }

    // Step 2: For an empty workflow, evaluate return_value directly (if any)
    if (workflow->nodes.empty()) {
        if (workflow->return_value) {
            std::unordered_map<std::string, Value> empty_outputs;
            CapabilityInvocationContext context;
            context.workflow_name = workflow_name;
            auto return_result =
                eval_workflow_expression(*workflow->return_value, input, empty_outputs, context);
            if (return_result.has_errors()) {
                result.status = WorkflowStatus::EvalError;
                result.diagnostics.append(return_result.diagnostics);
            } else {
                result.output = std::move(return_result.value);
            }
        }
        return result;
    }

    // Step 3: Topological sort
    auto sorted_nodes = topological_sort(*workflow);

    // Incomplete topological sort -> a cycle exists
    if (sorted_nodes.size() != workflow->nodes.size()) {
        result.status = WorkflowStatus::DependencyFailed;
        result.diagnostics.error()
            .message("workflow '" + workflow_name + "' has a dependency cycle or unresolvable node")
            .emit();
        return result;
    }

    // Step 4: Execute each node in order
    std::unordered_map<std::string, Value> node_outputs;
    std::unordered_set<std::string> failed_nodes;

    for (std::size_t idx = 0; idx < sorted_nodes.size(); ++idx) {
        const ir::WorkflowNode *node = sorted_nodes[idx];
        const auto target = std::string(ir::symbol_canonical_name(node->target_ref));

        // Check whether any dependency failed
        bool dep_failed = false;
        for (const auto &dep_name : node->after) {
            if (failed_nodes.contains(dep_name)) {
                dep_failed = true;
                break;
            }
        }

        if (dep_failed) {
            // Dependency failed, skip this node
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

        // Step 4a: Evaluate the node's input expression
        evaluator::Value node_input = evaluator::make_none();
        CapabilityInvocationContext node_context{
            .workflow_name = workflow_name,
            .workflow_node_name = node->name,
            .agent_name = target,
            .state_name = {},
            .workflow_node_execution_index = idx,
            .has_workflow_node_context = true,
        };
        if (node->input) {
            auto eval_result =
                eval_workflow_expression(*node->input, input, node_outputs, node_context);
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

        // Step 4b: Look up the AgentDecl and FlowDecl
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

        // Step 4c: Build and run the AgentRuntime
        AgentRuntime agent_rt(*agent_decl, *flow_decl, config_.default_agent_quota);
        agent_rt.set_invocation_context(node_context);
        if (config_.contextual_capability_invoker.has_value()) {
            agent_rt.set_contextual_capability_invoker(*config_.contextual_capability_invoker);
        } else if (config_.capability_invoker.has_value()) {
            agent_rt.set_capability_invoker(*config_.capability_invoker);
        }
        AgentResult agent_result = agent_rt.run(std::move(node_input));

        result.execution_order.push_back(node->name);
        result.diagnostics.append(agent_result.diagnostics);

        if (agent_result.status == AgentStatus::Completed) {
            // Success: save the output
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
            // Failure: mark the workflow status
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

    // Step 5: Evaluate the workflow return value (only when there are no errors)
    if (result.status == WorkflowStatus::Completed && workflow->return_value) {
        CapabilityInvocationContext context;
        context.workflow_name = workflow_name;
        auto return_result =
            eval_workflow_expression(*workflow->return_value, input, node_outputs, context);
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
