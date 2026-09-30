// RFC 0026 KR6.8 WH-4 fix-forward P1-2/P1-3: the shared lifecycle helper
// implementation. See the header comment for the full contract.

#include "runtime/wasm_host/wasm_lifecycle.hpp"
#include "runtime/wasm_host/wasm_error_codes.hpp"

#include "ahfl/runtime/execution_report.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace {

namespace bd = ::ahfl::backends;

// Add a runtime value to the result and return its id.
[[nodiscard]] RuntimeValueId add_value(WorkflowResult &result, Value value) {
    const RuntimeValueId id{result.values.size()};
    result.values.push_back(std::move(value));
    return id;
}

// Add an error diagnostic to the result and return its id.
[[nodiscard]] DiagnosticId
add_error(WorkflowResult &result, std::string code, std::string message) {
    const DiagnosticId id{result.diagnostics.entries().size()};
    result.diagnostics.error()
        .code(std::move(code))
        .message(std::move(message))
        .emit();
    return id;
}

// Build the report from the event stream via the SAME projection the
// evaluator uses (finalize_report in workflow_runtime.cpp). Returns false
// if the event stream violated the lifecycle contract.
bool build_report(WorkflowResult &result) {
    auto report = build_execution_report(result.events.events());
    if (report.has_value()) {
        result.report = std::move(*report);
        return true;
    }
    result.diagnostics.error()
        .code(std::string{wasm_diag::kEventStreamInvalid})
        .message("wasm lane event stream violated the accepted lifecycle contract")
        .emit();
    return false;
}

// Per-runner agent id + per-node metadata, resolved from the descriptor.
struct WorkflowMetadataPlan {
    WorkflowId workflow;
    // Indexed by descriptor.agents runner index.
    std::vector<AgentId> agent_by_runner;
    // Indexed by descriptor.nodes (schedule order).
    std::vector<WorkflowNodeId> node_by_schedule;
    // node_id (dense source-order) -> WorkflowNodeId.
    std::unordered_map<std::uint32_t, WorkflowNodeId> node_by_id;
    // Capability canonical name -> CapabilityId (import ordinal order).
    std::unordered_map<std::string, CapabilityId> capability_by_name;
    // The runtime provider (matches the evaluator's "runtime" provider).
    ProviderId provider;
};

// Populate the metadata store from the descriptor. Agents are assigned by
// first-appearance by NAME in node_id (source) order, matching the
// evaluator's build_runtime_plan. Nodes are added in node_id order so the
// dense WorkflowNodeId equals the source-order index.
[[nodiscard]] WorkflowMetadataPlan
populate_workflow_metadata(WorkflowResult &result,
                           const bd::CoreWasmExecutionDescriptor &descriptor) {
    WorkflowMetadataPlan plan;
    plan.workflow = result.metadata.add_workflow(descriptor.workflow_name);

    // Sort nodes by node_id (source order) for first-appearance agent +
    // node id assignment.
    std::vector<std::size_t> source_order(descriptor.nodes.size());
    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
        source_order[i] = i;
    }
    std::sort(source_order.begin(), source_order.end(),
              [&](std::size_t a, std::size_t b) {
                  return descriptor.nodes[a].node_id <
                         descriptor.nodes[b].node_id;
              });

    // Agents: first-appearance by name in node_id order.
    std::unordered_map<std::string, AgentId> agent_by_name;
    plan.agent_by_runner.resize(descriptor.agents.size());
    for (const auto idx : source_order) {
        const auto &node = descriptor.nodes[idx];
        if (node.runner >= descriptor.agents.size()) {
            continue; // fail-closed elsewhere; skip for metadata
        }
        const std::string &agent_name = descriptor.agents[node.runner].agent;
        auto [it, inserted] = agent_by_name.emplace(agent_name, AgentId{});
        if (inserted) {
            it->second = result.metadata.add_agent(agent_name);
        }
        plan.agent_by_runner[node.runner] = it->second;
    }

    // Nodes: in node_id order.
    plan.node_by_schedule.resize(descriptor.nodes.size());
    for (const auto idx : source_order) {
        const auto &node = descriptor.nodes[idx];
        const auto agent_id = node.runner < plan.agent_by_runner.size()
                                  ? plan.agent_by_runner[node.runner]
                                  : AgentId{};
        const auto node_id =
            result.metadata.add_node(node.name, plan.workflow, agent_id);
        plan.node_by_id[node.node_id] = node_id;
        plan.node_by_schedule[idx] = node_id;
    }

    // Capabilities: in import ordinal order.
    for (const auto &import : descriptor.imports) {
        const auto cap_id =
            result.metadata.add_capability(import.canonical_name);
        plan.capability_by_name[import.canonical_name] = cap_id;
    }

    plan.provider = result.metadata.add_provider("runtime");

    return plan;
}

// Emit the lifecycle event stream for a workflow run. Events are emitted in
// the SAME order as the evaluator's WorkflowRuntime::run:
//   RunStarted, WorkflowStarted, NodeScheduled (all, in schedule order),
//   per node: NodeStarted, AgentStateEntered (per state), terminal,
//   WorkflowCompleted|WorkflowFailed, RunCompleted.
void emit_workflow_events(WorkflowResult &result,
                          const bd::CoreWasmExecutionDescriptor &descriptor,
                          const WorkflowMetadataPlan &plan,
                          WasmWorkflowRunFacts &facts) {
    const auto now = std::chrono::nanoseconds{0};
    auto emit = [&](auto payload) {
        (void)result.events.append(std::move(payload), now);
    };

    emit(RunStarted{.run = RunId{0}});
    emit(WorkflowStarted{.run = RunId{0}, .workflow = plan.workflow});

    // NodeScheduled for ALL nodes in schedule (Kahn) order, with
    // dependencies (mapped to WorkflowNodeId) + execution_slot.
    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
        const auto &node = descriptor.nodes[i];
        std::vector<WorkflowNodeId> deps;
        deps.reserve(node.dependencies.size());
        for (const auto dep_node_id : node.dependencies) {
            auto it = plan.node_by_id.find(dep_node_id);
            if (it != plan.node_by_id.end()) {
                deps.push_back(it->second);
            }
        }
        emit(NodeScheduled{
            .workflow = plan.workflow,
            .node = plan.node_by_schedule[i],
            .dependencies = std::move(deps),
            .execution_slot = i,
        });
    }

    // Per-node: NodeStarted, AgentStateEntered (per state), terminal.
    for (std::size_t i = 0; i < descriptor.nodes.size(); ++i) {
        const auto &node = descriptor.nodes[i];
        const auto node_id = plan.node_by_schedule[i];
        const auto agent_id = node.runner < plan.agent_by_runner.size()
                                  ? plan.agent_by_runner[node.runner]
                                  : AgentId{};

        WasmNodeRunFacts default_facts;
        WasmNodeRunFacts &node_facts =
            i < facts.nodes.size() ? facts.nodes[i] : default_facts;

        if (node_facts.terminal == WasmNodeRunFacts::Terminal::Skipped) {
            std::vector<WorkflowNodeId> blocking;
            blocking.reserve(node_facts.blocking_dependencies.size());
            for (const auto dep : node_facts.blocking_dependencies) {
                auto it = plan.node_by_id.find(dep);
                if (it != plan.node_by_id.end()) {
                    blocking.push_back(it->second);
                }
            }
            emit(NodeSkipped{
                .node = node_id,
                .blocking_dependencies = std::move(blocking),
            });
            continue;
        }

        emit(NodeStarted{.node = node_id, .agent = agent_id});

        // AgentStateEntered: add states lazily (in entry order) to match
        // the evaluator's insertion order for AgentStateId.
        for (const auto &state_entry : node_facts.states) {
            const auto state_id = result.metadata.add_agent_state(
                agent_id, state_entry.state_name);
            emit(AgentStateEntered{
                .node = node_id,
                .agent = agent_id,
                .state = state_id,
            });
        }

        // Capability lifecycle: emit CapabilityStarted / CapabilityCompleted
        // for each capability call from this node, matching the evaluator's
        // event order (between AgentStateEntered and the node terminal).
        for (auto &cap_call : facts.capability_calls) {
            auto node_it = plan.node_by_id.find(cap_call.node_id);
            if (node_it == plan.node_by_id.end() ||
                node_it->second != node_id) {
                continue;
            }
            const auto cap_id =
                plan.capability_by_name.count(cap_call.capability_name)
                    ? plan.capability_by_name.at(cap_call.capability_name)
                    : CapabilityId{};
            const auto invocation =
                result.metadata.add_invocation(node_id, cap_id);
            emit(CapabilityStarted{
                .invocation = invocation,
                .node = node_id,
                .capability = cap_id,
                .provider = plan.provider,
                .attempt = 1,
            });
            std::optional<RuntimeValueId> output_id;
            if (cap_call.success && cap_call.output.has_value()) {
                output_id = add_value(result, std::move(*cap_call.output));
            }
            emit(CapabilityCompleted{
                .invocation = invocation,
                .output = output_id,
                .attempts = 1,
                .cache_hit = false,
            });
        }

        if (node_facts.terminal == WasmNodeRunFacts::Terminal::Completed) {
            std::optional<RuntimeValueId> output_id;
            if (node_facts.output.has_value()) {
                output_id = add_value(result, std::move(*node_facts.output));
            }
            emit(NodeCompleted{.node = node_id, .output = output_id});
        } else {
            const auto diag_id =
                add_error(result, node_facts.failure_code,
                          node_facts.failure_message);
            emit(NodeFailed{
                .node = node_id,
                .diagnostic = diag_id,
                .kind = node_facts.failure_kind,
            });
        }
    }

    // Workflow terminal.
    if (facts.status == RunTerminalStatus::Completed) {
        std::optional<RuntimeValueId> output_id;
        if (facts.workflow_output.has_value()) {
            output_id = add_value(result, std::move(*facts.workflow_output));
        }
        emit(WorkflowCompleted{.workflow = plan.workflow, .output = output_id});
    } else {
        const auto diag_id =
            add_error(result, facts.failure_code, facts.failure_message);
        emit(WorkflowFailed{
            .workflow = plan.workflow,
            .diagnostic = diag_id,
            .kind = facts.failure_kind.value_or(WorkflowFailureKind::NodeFailed),
        });
    }

    emit(RunCompleted{.run = RunId{0}, .status = facts.status});
}

} // namespace

bool finalize_wasm_workflow_run(
    WorkflowResult &result,
    const bd::CoreWasmExecutionDescriptor &descriptor,
    WasmWorkflowRunFacts facts) {
    const auto plan = populate_workflow_metadata(result, descriptor);
    emit_workflow_events(result, descriptor, plan, facts);
    return build_report(result);
}

bool finalize_wasm_agent_run(
    WorkflowResult &result,
    const bd::CoreWasmExecutionDescriptor &descriptor,
    const std::vector<std::string> &walk_states,
    std::optional<Value> output,
    RunTerminalStatus status,
    std::optional<WorkflowFailureKind> failure_kind,
    std::string failure_code,
    std::string failure_message) {
    const auto now = std::chrono::nanoseconds{0};
    auto emit = [&](auto payload) {
        (void)result.events.append(std::move(payload), now);
    };

    // Metadata: synthetic single-node workflow named after the agent.
    const auto workflow_id =
        result.metadata.add_workflow(descriptor.agent_name);
    const auto agent_id = result.metadata.add_agent(descriptor.agent_name);
    const auto node_id =
        result.metadata.add_node(descriptor.agent_name, workflow_id, agent_id);
    for (const auto &import : descriptor.imports) {
        (void)result.metadata.add_capability(import.canonical_name);
    }

    emit(RunStarted{.run = RunId{0}});
    emit(WorkflowStarted{.run = RunId{0}, .workflow = workflow_id});
    emit(NodeScheduled{
        .workflow = workflow_id,
        .node = node_id,
        .dependencies = {},
        .execution_slot = 0,
    });

    if (status == RunTerminalStatus::Completed) {
        emit(NodeStarted{.node = node_id, .agent = agent_id});
        for (const auto &state_name : walk_states) {
            const auto state_id =
                result.metadata.add_agent_state(agent_id, state_name);
            emit(AgentStateEntered{
                .node = node_id,
                .agent = agent_id,
                .state = state_id,
            });
        }
        std::optional<RuntimeValueId> output_id;
        if (output.has_value()) {
            output_id = add_value(result, std::move(*output));
        }
        emit(NodeCompleted{.node = node_id, .output = output_id});
        emit(WorkflowCompleted{.workflow = workflow_id, .output = output_id});
    } else {
        const auto diag_id =
            add_error(result, std::move(failure_code),
                      std::move(failure_message));
        emit(NodeStarted{.node = node_id, .agent = agent_id});
        for (const auto &state_name : walk_states) {
            const auto state_id =
                result.metadata.add_agent_state(agent_id, state_name);
            emit(AgentStateEntered{
                .node = node_id,
                .agent = agent_id,
                .state = state_id,
            });
        }
        emit(NodeFailed{
            .node = node_id,
            .diagnostic = diag_id,
            .kind = NodeFailureKind::AgentFailed,
        });
        emit(WorkflowFailed{
            .workflow = workflow_id,
            .diagnostic = diag_id,
            .kind = failure_kind.value_or(WorkflowFailureKind::NodeFailed),
        });
    }

    emit(RunCompleted{.run = RunId{0}, .status = status});
    return build_report(result);
}

} // namespace ahfl::runtime::wasm_host
