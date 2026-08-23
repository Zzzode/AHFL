#include "runtime/engine/workflow_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/evaluator/evaluator.hpp"

namespace ahfl::runtime {

namespace {

struct RuntimeNodePlan {
    WorkflowNodeId id;
    AgentId agent;
    const ir::WorkflowNode *source{nullptr};
    const ir::AgentDecl *agent_decl{nullptr};
    const ir::FlowDecl *flow_decl{nullptr};
    std::vector<WorkflowNodeId> dependencies{};
};

struct RuntimeWorkflowPlan {
    WorkflowId workflow;
    std::vector<RuntimeNodePlan> nodes;
    std::vector<WorkflowNodeId> execution_order;
    bool dependencies_valid{true};
};

[[nodiscard]] std::optional<std::string>
validate_recovery_snapshot(const WorkflowRecoverySnapshot &snapshot,
                           const RuntimeWorkflowPlan &plan) {
    if (!snapshot.workflow.valid() || snapshot.workflow != plan.workflow) {
        return "recovery snapshot workflow ID does not match the selected workflow";
    }
    if (!snapshot.checkpoint.valid()) {
        return "recovery snapshot checkpoint ID is invalid";
    }

    std::set<WorkflowNodeId> recovered_nodes;
    for (const auto &state : snapshot.completed_nodes) {
        if (!state.node.valid() || state.node.index() >= plan.nodes.size()) {
            return "recovery snapshot contains an unknown workflow node ID";
        }
        if (!state.agent.valid() || state.agent != plan.nodes[state.node.index()].agent) {
            return "recovery snapshot agent ID does not match the workflow plan";
        }
        if (!recovered_nodes.insert(state.node).second) {
            return "recovery snapshot contains a duplicate workflow node ID";
        }
    }
    for (const auto &state : snapshot.completed_nodes) {
        for (const auto dependency : plan.nodes[state.node.index()].dependencies) {
            if (!recovered_nodes.contains(dependency)) {
                return "recovery snapshot is not closed over workflow dependencies";
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] const ir::AgentDecl *find_agent_by_ref(const ir::ProgramIndex &index,
                                                     const ir::SymbolRef &reference) {
    if (const auto *declaration = index.find_decl_by_symbol_ref(reference);
        declaration != nullptr) {
        return std::get_if<ir::AgentDecl>(declaration);
    }
    return index.find_agent(ir::symbol_canonical_name(reference));
}

[[nodiscard]] const ir::FlowDecl *find_flow_by_agent(const ir::ProgramIndex &index,
                                                     const ir::AgentDecl *agent,
                                                     const ir::SymbolRef &reference) {
    if (agent != nullptr && agent->symbol_ref.id.has_value()) {
        for (const auto *flow : index.flows()) {
            if (flow->target_ref.id == agent->symbol_ref.id) {
                return flow;
            }
        }
    }
    return index.find_flow_for_agent(ir::symbol_canonical_name(reference));
}

[[nodiscard]] RuntimeWorkflowPlan
build_runtime_plan(const ir::WorkflowDecl &workflow,
                   const ir::ProgramIndex &index,
                   ExecutionMetadataStore &metadata) {
    RuntimeWorkflowPlan plan;
    plan.workflow = metadata.add_workflow(
        ir::symbol_canonical_name(workflow.symbol_ref, workflow.name));
    plan.nodes.reserve(workflow.nodes.size());

    std::unordered_map<std::string, WorkflowNodeId> node_ids;
    node_ids.reserve(workflow.nodes.size());
    std::unordered_map<std::string, AgentId> agent_ids;
    agent_ids.reserve(workflow.nodes.size());

    for (const auto &node : workflow.nodes) {
        const auto target_name = std::string(ir::symbol_canonical_name(node.target_ref));
        auto agent_found = agent_ids.find(target_name);
        AgentId agent_id;
        if (agent_found == agent_ids.end()) {
            agent_id = metadata.add_agent(target_name);
            agent_ids.emplace(target_name, agent_id);
        } else {
            agent_id = agent_found->second;
        }

        const WorkflowNodeId node_id =
            metadata.add_node(node.name, plan.workflow, agent_id);
        node_ids.emplace(node.name, node_id);
        const auto *agent = find_agent_by_ref(index, node.target_ref);
        plan.nodes.push_back(RuntimeNodePlan{
            .id = node_id,
            .agent = agent_id,
            .source = &node,
            .agent_decl = agent,
            .flow_decl = find_flow_by_agent(index, agent, node.target_ref),
        });
    }

    std::vector<std::size_t> remaining_dependencies(plan.nodes.size(), 0);
    std::vector<std::vector<WorkflowNodeId>> successors(plan.nodes.size());
    for (auto &node : plan.nodes) {
        node.dependencies.reserve(node.source->after.size());
        for (const auto &dependency_name : node.source->after) {
            const auto dependency = node_ids.find(dependency_name);
            if (dependency == node_ids.end()) {
                plan.dependencies_valid = false;
                continue;
            }
            node.dependencies.push_back(dependency->second);
            successors[dependency->second.index()].push_back(node.id);
        }
        remaining_dependencies[node.id.index()] = node.dependencies.size();
    }

    std::vector<WorkflowNodeId> ready;
    ready.reserve(plan.nodes.size());
    for (const auto &node : plan.nodes) {
        if (remaining_dependencies[node.id.index()] == 0) {
            ready.push_back(node.id);
        }
    }

    for (std::size_t index_in_ready = 0; index_in_ready < ready.size(); ++index_in_ready) {
        const auto node = ready[index_in_ready];
        plan.execution_order.push_back(node);
        for (const auto successor : successors[node.index()]) {
            auto &remaining = remaining_dependencies[successor.index()];
            if (remaining > 0) {
                --remaining;
            }
            if (remaining == 0) {
                ready.push_back(successor);
            }
        }
    }

    if (plan.execution_order.size() != plan.nodes.size()) {
        plan.dependencies_valid = false;
    }
    return plan;
}

[[nodiscard]] DiagnosticId append_diagnostics(WorkflowResult &result,
                                              const DiagnosticBag &diagnostics) {
    const DiagnosticId id{result.diagnostics.entries().size()};
    result.diagnostics.append(diagnostics);
    return id;
}

[[nodiscard]] DiagnosticId add_runtime_error(WorkflowResult &result, std::string message) {
    const DiagnosticId id{result.diagnostics.entries().size()};
    result.diagnostics.error().message(std::move(message)).emit();
    return id;
}

[[nodiscard]] RuntimeValueId add_runtime_value(WorkflowResult &result, Value value) {
    const RuntimeValueId id{result.values.size()};
    result.values.push_back(std::move(value));
    return id;
}

void finalize_report(WorkflowResult &result) {
    auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = std::move(*report);
        return;
    }
    result.diagnostics.error()
        .message("runtime execution event stream violated the accepted lifecycle contract")
        .emit();
}

[[nodiscard]] RunTerminalStatus terminal_status(WorkflowFailureKind failure) {
    switch (failure) {
    case WorkflowFailureKind::Cancelled:
        return RunTerminalStatus::Cancelled;
    case WorkflowFailureKind::Interrupted:
        return RunTerminalStatus::Interrupted;
    case WorkflowFailureKind::NodeFailed:
    case WorkflowFailureKind::DependencyFailed:
    case WorkflowFailureKind::EvaluationFailed:
    case WorkflowFailureKind::BudgetRejected:
        return RunTerminalStatus::Failed;
    }
    return RunTerminalStatus::Failed;
}

[[nodiscard]] CapabilityFailureKind capability_failure_kind(CapabilityCallStatus status) {
    switch (status) {
    case CapabilityCallStatus::Success:
        return CapabilityFailureKind::Error;
    case CapabilityCallStatus::Error:
    case CapabilityCallStatus::CircuitOpen:
        return CapabilityFailureKind::Error;
    case CapabilityCallStatus::Timeout:
        return CapabilityFailureKind::Timeout;
    case CapabilityCallStatus::RetryExhausted:
        return CapabilityFailureKind::RetryExhausted;
    }
    return CapabilityFailureKind::Error;
}

} // namespace

bool WorkflowResult::has_errors() const {
    return diagnostics.has_error();
}

WorkflowStatus WorkflowResult::status() const noexcept {
    if (report.status == RunTerminalStatus::Completed) {
        return WorkflowStatus::Completed;
    }
    if (!report.failure_kind.has_value()) {
        return WorkflowStatus::NodeFailed;
    }
    switch (*report.failure_kind) {
    case WorkflowFailureKind::DependencyFailed:
        return WorkflowStatus::DependencyFailed;
    case WorkflowFailureKind::EvaluationFailed:
        return WorkflowStatus::EvalError;
    case WorkflowFailureKind::NodeFailed:
    case WorkflowFailureKind::BudgetRejected:
    case WorkflowFailureKind::Cancelled:
    case WorkflowFailureKind::Interrupted:
        return WorkflowStatus::NodeFailed;
    }
    return WorkflowStatus::NodeFailed;
}

const Value *WorkflowResult::value(RuntimeValueId id) const noexcept {
    if (!id.valid() || id.index() >= values.size()) {
        return nullptr;
    }
    return &values[id.index()];
}

const Value *WorkflowResult::output() const noexcept {
    return report.output.has_value() ? value(*report.output) : nullptr;
}

WorkflowRuntime::WorkflowRuntime(const ir::Program &program, WorkflowRuntimeConfig config)
    : program_(program), index_(program_), config_(std::move(config)) {}

const ir::WorkflowDecl *WorkflowRuntime::find_workflow(const std::string &name) const {
    return index_.find_workflow(name);
}

const ir::AgentDecl *WorkflowRuntime::find_agent(const std::string &name) const {
    return index_.find_agent(name);
}

const ir::FlowDecl *WorkflowRuntime::find_flow(const std::string &agent_name) const {
    return index_.find_flow_for_agent(agent_name);
}

evaluator::EvalResult WorkflowRuntime::eval_workflow_expression(
    const ir::Expr &expr,
    const Value &workflow_input,
    const std::vector<std::optional<RuntimeValueId>> &node_outputs,
    const WorkflowResult &result,
    const ContextualCapabilityInvoker *runtime_invoker,
    const CapabilityInvocationContext &context) const {
    evaluator::EvalContext ctx;

    ctx.bind_local("input", evaluator::clone_value(workflow_input));

    if (const auto *sv = std::get_if<evaluator::StructValue>(&workflow_input.node)) {
        for (const auto &[field_name, field_val] : sv->fields) {
            if (field_val) {
                ctx.set_input(field_name, evaluator::clone_value(*field_val));
            }
        }
    }

    for (std::size_t index = 0; index < node_outputs.size(); ++index) {
        if (!node_outputs[index].has_value()) {
            continue;
        }
        const WorkflowNodeId node_id{index};
        const auto *metadata = result.metadata.node(node_id);
        const auto *node_value = result.value(*node_outputs[index]);
        if (metadata == nullptr || node_value == nullptr) {
            continue;
        }
        ctx.bind_local(metadata->display_name, evaluator::clone_value(*node_value));
        if (const auto *sv = std::get_if<evaluator::StructValue>(&node_value->node)) {
            for (const auto &[field_name, field_val] : sv->fields) {
                if (field_val) {
                    ctx.set_node_output(
                        metadata->display_name, field_name, evaluator::clone_value(*field_val));
                }
            }
        }
    }

    if (runtime_invoker != nullptr && *runtime_invoker) {
        return eval_expr_with_capabilities(expr, ctx, *runtime_invoker, context);
    }
    if (config_.capability_invoker.has_value()) {
        return eval_expr_with_capabilities(expr, ctx, *config_.capability_invoker);
    }
    return evaluator::eval_expr(expr, ctx);
}

WorkflowResult WorkflowRuntime::run(const std::string &workflow_name, Value input) {
    WorkflowResult result;
    const auto started_at = std::chrono::steady_clock::now();
    const RunId run_id{0};
    auto emit = [&](auto payload) {
        const auto offset = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started_at);
        (void)result.events.append(std::move(payload), offset);
    };

    const ir::WorkflowDecl *workflow = find_workflow(workflow_name);
    if (workflow == nullptr) {
        const auto workflow_id = result.metadata.add_workflow(workflow_name);
        emit(RunStarted{.run = run_id});
        emit(WorkflowStarted{.run = run_id, .workflow = workflow_id});
        const auto diagnostic =
            add_runtime_error(result, "workflow '" + workflow_name + "' not found in program");
        emit(WorkflowFailed{
            .workflow = workflow_id,
            .diagnostic = diagnostic,
            .kind = WorkflowFailureKind::NodeFailed,
        });
        emit(RunCompleted{.run = run_id, .status = RunTerminalStatus::Failed});
        finalize_report(result);
        return result;
    }

    auto plan = build_runtime_plan(*workflow, index_, result.metadata);
    for (const auto *capability : index_.capabilities()) {
        (void)result.metadata.add_capability(
            ir::symbol_canonical_name(capability->symbol_ref, capability->name),
            capability->symbol_ref.id);
    }
    const ProviderId runtime_provider = result.metadata.add_provider("runtime");

    emit(RunStarted{.run = run_id});
    const auto resume_checkpoint =
        config_.recovery_snapshot.has_value()
            ? std::optional{config_.recovery_snapshot->checkpoint}
            : config_.resume_checkpoint;
    if (resume_checkpoint.has_value()) {
        emit(RunResumed{.run = run_id, .checkpoint = *resume_checkpoint});
    }
    emit(WorkflowStarted{.run = run_id, .workflow = plan.workflow});

    if (!plan.dependencies_valid) {
        const auto diagnostic = add_runtime_error(
            result,
            "workflow '" + workflow_name + "' has a dependency cycle or unresolvable node");
        emit(WorkflowFailed{
            .workflow = plan.workflow,
            .diagnostic = diagnostic,
            .kind = WorkflowFailureKind::DependencyFailed,
        });
        emit(RunCompleted{.run = run_id, .status = RunTerminalStatus::Failed});
        finalize_report(result);
        return result;
    }

    if (config_.recovery_snapshot.has_value()) {
        if (const auto error = validate_recovery_snapshot(*config_.recovery_snapshot, plan);
            error.has_value()) {
            const auto diagnostic = add_runtime_error(result, *error);
            emit(WorkflowFailed{
                .workflow = plan.workflow,
                .diagnostic = diagnostic,
                .kind = WorkflowFailureKind::EvaluationFailed,
            });
            emit(RunCompleted{.run = run_id, .status = RunTerminalStatus::Failed});
            finalize_report(result);
            return result;
        }
    }

    for (std::size_t slot = 0; slot < plan.execution_order.size(); ++slot) {
        const auto node_id = plan.execution_order[slot];
        const auto &node = plan.nodes[node_id.index()];
        emit(NodeScheduled{
            .workflow = plan.workflow,
            .node = node.id,
            .dependencies = node.dependencies,
            .execution_slot = slot,
        });
    }

    std::vector<std::optional<RuntimeValueId>> node_outputs(plan.nodes.size());
    std::vector<bool> restored_nodes(plan.nodes.size(), false);
    std::vector<bool> completed_nodes(plan.nodes.size(), false);
    std::vector<bool> failed_nodes(plan.nodes.size(), false);
    std::vector<std::optional<CapabilityFailureKind>>
        node_capability_failures(plan.nodes.size());
    std::optional<WorkflowFailureKind> workflow_failure;
    std::optional<DiagnosticId> workflow_diagnostic;

    if (config_.recovery_snapshot.has_value()) {
        for (const auto &state : config_.recovery_snapshot->completed_nodes) {
            std::optional<RuntimeValueId> output;
            if (state.output.has_value()) {
                output = add_runtime_value(result, evaluator::clone_value(*state.output));
                node_outputs[state.node.index()] = output;
            }
            restored_nodes[state.node.index()] = true;
            completed_nodes[state.node.index()] = true;
            emit(NodeRestored{
                .node = state.node,
                .agent = state.agent,
                .output = output,
                .checkpoint = config_.recovery_snapshot->checkpoint,
            });
        }
    }

    ContextualCapabilityInvoker runtime_invoker;
    if (config_.contextual_capability_invoker.has_value() ||
        config_.capability_invoker.has_value()) {
        runtime_invoker =
            [this, &result, &emit, &node_capability_failures, runtime_provider](
                const CapabilityInvocationContext &context,
                const std::string &name,
                const std::vector<Value> &arguments) -> CapabilityCallResult {
            std::optional<CapabilityId> capability;
            if (context.source_capability_symbol_id.has_value()) {
                capability = result.metadata.capability_for_source_symbol(
                    *context.source_capability_symbol_id);
            }
            if (!capability.has_value()) {
                capability =
                    result.metadata.add_capability(name, context.source_capability_symbol_id);
            }

            const auto invocation =
                result.metadata.add_invocation(context.workflow_node_id, *capability);
            auto invocation_context = context;
            invocation_context.capability_id = *capability;
            invocation_context.invocation_id = invocation;

            if (config_.capability_invoked_hook) {
                config_.capability_invoked_hook(context.agent_id, name);
            }

            CapabilityCallResult call_result;
            if (config_.contextual_capability_invoker.has_value()) {
                call_result =
                    (*config_.contextual_capability_invoker)(invocation_context, name, arguments);
            } else {
                call_result = (*config_.capability_invoker)(name, arguments);
            }
            if (config_.capability_result_observer) {
                config_.capability_result_observer(invocation_context, call_result);
            }

            const auto attempts = std::max<std::size_t>(call_result.attempts, 1U);
            InvocationId previous;
            for (std::size_t attempt = 1; attempt <= attempts; ++attempt) {
                const auto attempt_invocation =
                    attempt == 1
                        ? invocation
                        : result.metadata.add_invocation(context.workflow_node_id, *capability);
                if (attempt > 1) {
                    emit(CapabilityRetryScheduled{
                        .previous_invocation = previous,
                        .next_invocation = attempt_invocation,
                        .next_attempt = attempt,
                    });
                }
                emit(CapabilityStarted{
                    .invocation = attempt_invocation,
                    .node = context.workflow_node_id,
                    .capability = *capability,
                    .provider = runtime_provider,
                    .attempt = attempt,
                });

                if (attempt == attempts && call_result.usage.has_value()) {
                    emit(CapabilityUsageRecorded{
                        .invocation = attempt_invocation,
                        .prompt_tokens = call_result.usage->prompt_tokens,
                        .completion_tokens = call_result.usage->completion_tokens,
                        .total_tokens = call_result.usage->total_tokens,
                        .total_cost_usd = call_result.usage->total_cost_usd,
                        .cost_estimated = call_result.usage->cost_estimated,
                        .notices = call_result.usage->notices,
                    });
                }

                const bool terminal_success =
                    attempt == attempts && call_result.status == CapabilityCallStatus::Success;
                if (!terminal_success) {
                    emit(CapabilityFailed{
                        .invocation = attempt_invocation,
                        .kind = call_result.failure_kind.value_or(
                            capability_failure_kind(call_result.status)),
                        .diagnostic = std::nullopt,
                        .attempts = attempt,
                        .retryable = attempt < attempts ||
                                     call_result.status == CapabilityCallStatus::Timeout ||
                                     call_result.status == CapabilityCallStatus::RetryExhausted,
                    });
                }
                previous = attempt_invocation;
            }

            if (call_result.provider_degraded) {
                const auto degraded = result.metadata.add_provider(
                    call_result.degraded_provider_name.empty()
                        ? std::string_view{"degraded"}
                        : std::string_view{call_result.degraded_provider_name});
                const auto selected = result.metadata.add_provider(
                    call_result.selected_provider_name.empty()
                        ? std::string_view{"fallback"}
                        : std::string_view{call_result.selected_provider_name});
                emit(ProviderDegraded{
                    .invocation = previous,
                    .provider = degraded,
                    .fallback_provider = selected,
                    .reason = ProviderDegradationReason::RetryExhausted,
                });
            }

            if (call_result.status == CapabilityCallStatus::Success) {
                std::optional<RuntimeValueId> output;
                if (call_result.value.has_value()) {
                    output =
                        add_runtime_value(result, evaluator::clone_value(*call_result.value));
                }
                emit(CapabilityCompleted{
                    .invocation = previous,
                    .output = output,
                    .attempts = call_result.attempts,
                    .cache_hit = call_result.cache_hit,
                });
            } else if (context.workflow_node_id.valid() &&
                       context.workflow_node_id.index() < node_capability_failures.size()) {
                node_capability_failures[context.workflow_node_id.index()] =
                    call_result.failure_kind.value_or(
                        capability_failure_kind(call_result.status));
            }
            return call_result;
        };
    }

    for (std::size_t slot = 0; slot < plan.execution_order.size(); ++slot) {
        const auto node_id = plan.execution_order[slot];
        const auto &node = plan.nodes[node_id.index()];
        if (restored_nodes[node.id.index()]) {
            continue;
        }
        const bool cancelled =
            config_.cancellation_requested && config_.cancellation_requested();
        const bool interrupted =
            config_.interruption_requested && config_.interruption_requested();
        if (cancelled || interrupted) {
            for (std::size_t pending_slot = slot; pending_slot < plan.execution_order.size();
                 ++pending_slot) {
                const auto pending = plan.execution_order[pending_slot];
                emit(NodeSkipped{.node = pending, .blocking_dependencies = {}});
                failed_nodes[pending.index()] = true;
            }
            workflow_failure = cancelled ? WorkflowFailureKind::Cancelled
                                         : WorkflowFailureKind::Interrupted;
            workflow_diagnostic = add_runtime_error(
                result, cancelled ? "workflow execution cancelled" : "workflow execution interrupted");
            if (cancelled) {
                emit(RunCancellationRequested{.run = run_id});
            } else {
                emit(RunInterrupted{.run = run_id});
            }
            break;
        }
        bool dep_failed = false;
        std::vector<WorkflowNodeId> blocking_dependencies;
        for (const auto dependency : node.dependencies) {
            if (failed_nodes[dependency.index()]) {
                dep_failed = true;
                blocking_dependencies.push_back(dependency);
            }
        }

        if (dep_failed) {
            failed_nodes[node.id.index()] = true;
            emit(NodeSkipped{
                .node = node.id,
                .blocking_dependencies = std::move(blocking_dependencies),
            });
            if (!workflow_failure.has_value()) {
                workflow_failure = WorkflowFailureKind::DependencyFailed;
                workflow_diagnostic = add_runtime_error(
                    result, "workflow node skipped because a dependency failed");
            }
            continue;
        }

        evaluator::Value node_input = evaluator::make_none();
        CapabilityInvocationContext node_context{
            .workflow_name = workflow_name,
            .workflow_node_name = node.source->name,
            .agent_name =
                std::string(ir::symbol_canonical_name(node.source->target_ref)),
            .state_name = {},
            .workflow_node_execution_index = slot,
            .has_workflow_node_context = true,
            .run_id = run_id,
            .workflow_id = plan.workflow,
            .workflow_node_id = node.id,
            .agent_id = node.agent,
        };
        if (node.source->input) {
            auto eval_result = eval_workflow_expression(
                *node.source->input,
                input,
                node_outputs,
                result,
                runtime_invoker ? &runtime_invoker : nullptr,
                node_context);
            if (eval_result.has_errors()) {
                const auto diagnostic = append_diagnostics(result, eval_result.diagnostics);
                failed_nodes[node.id.index()] = true;
                emit(NodeFailed{
                    .node = node.id,
                    .diagnostic = diagnostic,
                    .kind = NodeFailureKind::EvaluationFailed,
                });
                if (!workflow_failure.has_value()) {
                    workflow_failure = WorkflowFailureKind::EvaluationFailed;
                    workflow_diagnostic = diagnostic;
                }
                continue;
            }
            node_input = std::move(eval_result.value);
        }

        if (node.agent_decl == nullptr || node.flow_decl == nullptr) {
            const auto *agent_metadata = result.metadata.agent(node.agent);
            const auto diagnostic = add_runtime_error(
                result,
                "agent '" +
                    (agent_metadata != nullptr ? agent_metadata->display_name : std::string{}) +
                    "' declaration or flow not found");
            failed_nodes[node.id.index()] = true;
            emit(NodeFailed{
                .node = node.id,
                .diagnostic = diagnostic,
                .kind = NodeFailureKind::AgentFailed,
            });
            if (!workflow_failure.has_value()) {
                workflow_failure = WorkflowFailureKind::NodeFailed;
                workflow_diagnostic = diagnostic;
            }
            continue;
        }

        emit(NodeStarted{.node = node.id, .agent = node.agent});
        const std::string agent_name =
            std::string(ir::symbol_canonical_name(node.source->target_ref));
        const std::string node_name = node.source->name;
        AgentRuntime agent_rt(*node.agent_decl, *node.flow_decl, config_.default_agent_quota);
        agent_rt.set_invocation_context(node_context);
        agent_rt.set_state_entered_observer(
            [this, &result, &emit, node_id = node.id, &agent_name, &node_name](
                AgentId agent, std::string_view state_name) -> AgentStateId {
            const auto state = result.metadata.add_agent_state(agent, state_name);
            emit(AgentStateEntered{
                .node = node_id,
                .agent = agent,
                .state = state,
            });
            if (config_.state_entered_hook) {
                config_.state_entered_hook(agent, agent_name, node_name, state_name);
            }
            return state;
        });
        if (runtime_invoker) {
            agent_rt.set_contextual_capability_invoker(runtime_invoker);
        }
        if (config_.agent_input_hook) {
            config_.agent_input_hook(node.agent, agent_name, node_name, node_input);
        }
        AgentResult agent_result = agent_rt.run(std::move(node_input));

        if (agent_result.status == AgentStatus::Completed) {
            std::optional<RuntimeValueId> output_id;
            if (agent_result.output.has_value()) {
                if (config_.node_completed_hook) {
                    config_.node_completed_hook(node.agent, node_name, *agent_result.output);
                }
                output_id = add_runtime_value(result, std::move(*agent_result.output));
                node_outputs[node.id.index()] = output_id;
            }
            result.diagnostics.append(agent_result.diagnostics);
            completed_nodes[node.id.index()] = true;
            const auto checkpoint =
                config_.checkpoint_after_node
                    ? config_.checkpoint_after_node(node.id)
                    : std::optional<CheckpointId>{};
            if (checkpoint.has_value() && config_.recovery_store != nullptr) {
                WorkflowRecoverySnapshot snapshot{
                    .workflow = plan.workflow,
                    .checkpoint = *checkpoint,
                };
                for (const auto &completed_node : plan.nodes) {
                    if (!completed_nodes[completed_node.id.index()]) {
                        continue;
                    }
                    RecoveredNodeState state{
                        .node = completed_node.id,
                        .agent = completed_node.agent,
                    };
                    if (node_outputs[completed_node.id.index()].has_value()) {
                        const auto *value =
                            result.value(*node_outputs[completed_node.id.index()]);
                        if (value != nullptr) {
                            state.output = evaluator::clone_value(*value);
                        }
                    }
                    snapshot.completed_nodes.push_back(std::move(state));
                }
                if (!config_.recovery_store->save(snapshot).has_value()) {
                    completed_nodes[node.id.index()] = false;
                    failed_nodes[node.id.index()] = true;
                    const auto diagnostic = add_runtime_error(
                        result, "failed to persist workflow recovery checkpoint");
                    emit(NodeFailed{
                        .node = node.id,
                        .diagnostic = diagnostic,
                        .kind = NodeFailureKind::EvaluationFailed,
                    });
                    if (!workflow_failure.has_value()) {
                        workflow_failure = WorkflowFailureKind::EvaluationFailed;
                        workflow_diagnostic = diagnostic;
                    }
                    continue;
                }
            }
            emit(NodeCompleted{.node = node.id, .output = output_id});
            if (checkpoint.has_value()) {
                emit(CheckpointSaved{.run = run_id, .checkpoint = *checkpoint});
            }
        } else {
            auto diagnostic = append_diagnostics(result, agent_result.diagnostics);
            if (agent_result.diagnostics.entries().empty()) {
                diagnostic = add_runtime_error(result, "agent execution failed");
            }
            failed_nodes[node.id.index()] = true;
            const bool budget_rejected =
                node_capability_failures[node.id.index()] ==
                CapabilityFailureKind::BudgetRejected;
            emit(NodeFailed{
                .node = node.id,
                .diagnostic = diagnostic,
                .kind = budget_rejected ? NodeFailureKind::BudgetRejected
                                        : NodeFailureKind::AgentFailed,
            });
            if (!workflow_failure.has_value()) {
                workflow_failure = budget_rejected ? WorkflowFailureKind::BudgetRejected
                                                   : WorkflowFailureKind::NodeFailed;
                workflow_diagnostic = diagnostic;
            }
        }
    }

    std::optional<RuntimeValueId> workflow_output;
    if (!workflow_failure.has_value() && workflow->return_value) {
        CapabilityInvocationContext context;
        context.workflow_name = workflow_name;
        context.run_id = run_id;
        context.workflow_id = plan.workflow;
        auto return_result = eval_workflow_expression(
            *workflow->return_value,
            input,
            node_outputs,
            result,
            runtime_invoker ? &runtime_invoker : nullptr,
            context);
        if (return_result.has_errors()) {
            workflow_failure = WorkflowFailureKind::EvaluationFailed;
            workflow_diagnostic = append_diagnostics(result, return_result.diagnostics);
        } else {
            workflow_output = add_runtime_value(result, std::move(return_result.value));
        }
    }

    if (workflow_failure.has_value()) {
        const auto diagnostic =
            workflow_diagnostic.has_value()
                ? *workflow_diagnostic
                : add_runtime_error(result, "workflow execution failed without a diagnostic");
        emit(WorkflowFailed{
            .workflow = plan.workflow,
            .diagnostic = diagnostic,
            .kind = *workflow_failure,
        });
        emit(RunCompleted{.run = run_id, .status = terminal_status(*workflow_failure)});
    } else {
        emit(WorkflowCompleted{.workflow = plan.workflow, .output = workflow_output});
        emit(RunCompleted{.run = run_id, .status = RunTerminalStatus::Completed});
    }
    finalize_report(result);
    return result;
}

} // namespace ahfl::runtime
