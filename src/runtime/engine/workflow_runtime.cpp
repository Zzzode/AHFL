#include "runtime/engine/workflow_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
#include "runtime/evaluator/value_json.hpp"

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

// RFC 0022 (durable resume): the running memo table for the node currently
// executing. Lives runtime-side (never rides in EvalResult — only the
// {cap_id, ordinal} signal propagates up the eval/exec/agent frames). Reset at
// the start of every node; the capability invoker closure captures it by
// reference. `next_ordinal` is a SEPARATE per-node counter, NOT the global
// retry-inflated InvocationId: it is the stable memo key across resume.
struct NodeMemoState {
    std::uint64_t next_ordinal{0};             // per-node ordinal generator (memo key)
    std::vector<CapabilityMemoEntry> memo{};   // append-only, in ordinal order
    bool suspended{false};                     // a pending call was reached this node
    std::size_t pending_cap_id{0};             // pending call's capability SymbolId
    std::uint64_t pending_ordinal{0};          // pending call's ordinal
    // Resume replay (C7): when true, ordinals below pending_ordinal are served
    // from `memo` instead of live-invoked, and pending_ordinal receives the
    // host-supplied resume result.
    bool replaying{false};

    void reset() {
        next_ordinal = 0;
        memo.clear();
        suspended = false;
        pending_cap_id = 0;
        pending_ordinal = 0;
        replaying = false;
    }
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

    // RFC 0022 (C7): validate the resume record. The suspended node must exist,
    // match its plan agent, be disjoint from the completed set (it did not
    // complete — it is re-run on resume), and have all its dependencies in the
    // completed set (dependency closure), so the re-run sees every input.
    if (snapshot.suspended.has_value()) {
        const auto &suspended = *snapshot.suspended;
        if (!suspended.node.valid() || suspended.node.index() >= plan.nodes.size()) {
            return "recovery snapshot suspended node ID is unknown";
        }
        if (!suspended.agent.valid() ||
            suspended.agent != plan.nodes[suspended.node.index()].agent) {
            return "recovery snapshot suspended agent ID does not match the workflow plan";
        }
        if (recovered_nodes.contains(suspended.node)) {
            return "recovery snapshot suspended node is also marked completed";
        }
        for (const auto dependency : plan.nodes[suspended.node.index()].dependencies) {
            if (!recovered_nodes.contains(dependency)) {
                return "recovery snapshot suspended node is not closed over its dependencies";
            }
        }
        std::set<std::uint64_t> memo_ordinals;
        for (const auto &entry : suspended.memo) {
            if (entry.ordinal >= suspended.pending_ordinal) {
                return "recovery snapshot memo ordinal is not below the pending ordinal";
            }
            if (!memo_ordinals.insert(entry.ordinal).second) {
                return "recovery snapshot memo contains a duplicate ordinal";
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

// RFC 0022 slice 4 (exactly-once): a stable per-invocation idempotency key. Same
// FNV-1a mix used for arg hashing, folded over the invocation coordinate. Must be
// reproducible across resume — every input is index/id-based (workflow, node, the
// stable per-node ordinal, capability SymbolId) plus the resolved-argument hash —
// so a host can dedup a durable_write effect that committed before a crash.
[[nodiscard]] std::uint64_t compute_idempotency_key(std::size_t workflow_index,
                                                    std::size_t node_index,
                                                    std::uint64_t ordinal,
                                                    std::size_t cap_symbol_id,
                                                    std::uint64_t arg_hash) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFULL;
            hash *= kPrime;
        }
    };
    mix(static_cast<std::uint64_t>(workflow_index));
    mix(static_cast<std::uint64_t>(node_index));
    mix(ordinal);
    mix(static_cast<std::uint64_t>(cap_symbol_id));
    mix(arg_hash);
    return hash;
}

// RFC 0022 slice 5 (fail-closed): does a runtime Value structurally match a
// declared capability return type? Used to validate an injected resume result
// and every memo Value before it re-enters evaluation. A mismatch means the
// snapshot is corrupt / the host replied with the wrong shape — fail closed
// (abort with a diagnostic), never coerce, never fall back to a live call.
// `Unresolved`/`Any` accept anything (the type checker already vetted the
// program; this guards the resume boundary, not the source).
[[nodiscard]] bool value_matches_return_type(const evaluator::Value &value,
                                             const ir::TypeRef &type) {
    using namespace ahfl::evaluator;
    switch (type.kind) {
    case ir::TypeRefKind::Unresolved:
    case ir::TypeRefKind::Any:
    case ir::TypeRefKind::Never:
        return true;
    case ir::TypeRefKind::Unit:
        return std::holds_alternative<UnitValue>(value.node) ||
               std::holds_alternative<NoneValue>(value.node);
    case ir::TypeRefKind::Bool:
        return std::holds_alternative<BoolValue>(value.node);
    case ir::TypeRefKind::Int:
    case ir::TypeRefKind::BoundedInt:
        return std::holds_alternative<IntValue>(value.node);
    case ir::TypeRefKind::Float:
        return std::holds_alternative<FloatValue>(value.node);
    case ir::TypeRefKind::String:
    case ir::TypeRefKind::BoundedString:
        return std::holds_alternative<StringValue>(value.node);
    case ir::TypeRefKind::UUID:
        return std::holds_alternative<UuidValue>(value.node);
    case ir::TypeRefKind::Timestamp:
        return std::holds_alternative<TimestampValue>(value.node);
    case ir::TypeRefKind::Duration:
        return std::holds_alternative<DurationValue>(value.node);
    case ir::TypeRefKind::Decimal:
        return std::holds_alternative<DecimalValue>(value.node);
    case ir::TypeRefKind::Struct:
        return std::holds_alternative<StructValue>(value.node);
    case ir::TypeRefKind::Enum:
        return std::holds_alternative<EnumValue>(value.node);
    case ir::TypeRefKind::Fn:
        return std::holds_alternative<CallableValue>(value.node);
    }
    return false;
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
    switch (status) {    case CapabilityCallStatus::Success:
        return CapabilityFailureKind::Error;
    case CapabilityCallStatus::Error:
    case CapabilityCallStatus::CircuitOpen:
        return CapabilityFailureKind::Error;
    case CapabilityCallStatus::Timeout:
        return CapabilityFailureKind::Timeout;
    case CapabilityCallStatus::RetryExhausted:
        return CapabilityFailureKind::RetryExhausted;
    case CapabilityCallStatus::Pending:
        // Pending is not a failure; it is handled on the suspend path before
        // failure classification. Reaching here means a Pending result was
        // misrouted — classify as Error (fail-closed) rather than assert.
        return CapabilityFailureKind::Error;
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
    if (report.status == RunTerminalStatus::Suspended) {
        return WorkflowStatus::Suspended;
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
    // RFC 0012 slice 2: read time through the injectable monotonic clock so
    // event offsets are deterministic under a fake clock in tests.
    const auto now = [this]() -> std::chrono::steady_clock::time_point {
        return config_.monotonic_clock ? config_.monotonic_clock()
                                       : std::chrono::steady_clock::now();
    };
    const auto started_at = now();
    const RunId run_id{0};
    auto emit = [&](auto payload) {
        const auto offset =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now() - started_at);
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
    // RFC 0022 (C6): set when a node suspends on a pending capability call. When
    // present the run terminates as Suspended (not Failed) and a v2 resume record
    // is built from the suspended node's input + running memo.
    std::optional<SuspendedNodeState> workflow_suspended;

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

    // Derive the capability dispatch source. A native host binding (the
    // ahfl_host.h ABI projection, RFC 0021 slice 2) takes precedence and is
    // adapted into a ContextualCapabilityInvoker; otherwise fall back to a
    // directly-wired contextual/plain invoker. This gives one uniform seam
    // downstream regardless of how the host connected.
    std::optional<ContextualCapabilityInvoker> effective_contextual_invoker =
        config_.contextual_capability_invoker;
    if (config_.native_host_binding.has_value()) {
        effective_contextual_invoker =
            make_native_capability_invoker(*config_.native_host_binding);
    }

    ContextualCapabilityInvoker runtime_invoker;
    // RFC 0022 slice 3 (C5): the memo table for the node currently executing.
    // Reset at the top of every node's execution; the invoker below captures it
    // by reference and appends one entry per completed capability call, keyed by
    // a stable per-node ordinal.
    NodeMemoState node_memo;
    if (effective_contextual_invoker.has_value() || config_.capability_invoker.has_value()) {
        runtime_invoker =
            [this, &result, &emit, &node_capability_failures, &node_memo, runtime_provider,
             contextual_invoker = std::move(effective_contextual_invoker)](
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

            // RFC 0022 (C5): assign the stable per-node ordinal BEFORE the global
            // (retry-inflated) InvocationId. The ordinal is the memo key and must
            // be reproducible across resume; the InvocationId is telemetry-only.
            const std::uint64_t memo_ordinal = node_memo.next_ordinal++;
            const std::size_t cap_symbol_id =
                context.source_capability_symbol_id.value_or(0);
            const std::uint64_t arg_hash = evaluator::hash_values(arguments);

            // RFC 0022 (C7): resume replay. While replaying, calls at ordinals
            // below the pending one are served from the memo (never re-invoked),
            // and the pending ordinal receives the host-supplied resume result.
            // Both are matched by ordinal (the stable per-node key) and
            // cross-checked on cap_id + arg_hash — a mismatch is fail-closed
            // (non-deterministic replay), never a silent live re-invoke.
            if (node_memo.replaying && memo_ordinal < node_memo.pending_ordinal) {
                const auto entry = std::find_if(
                    node_memo.memo.begin(), node_memo.memo.end(),
                    [&](const CapabilityMemoEntry &e) { return e.ordinal == memo_ordinal; });
                if (entry == node_memo.memo.end() || entry->cap_id != cap_symbol_id ||
                    entry->arg_hash != arg_hash) {
                    CapabilityCallResult mismatch;
                    mismatch.status = CapabilityCallStatus::Error;
                    mismatch.error_message =
                        "durable resume replay diverged from the recorded memo (ordinal " +
                        std::to_string(memo_ordinal) + ")";
                    mismatch.diagnostic_code =
                        std::string(error_codes::backend::ExecutionError.id);
                    if (context.workflow_node_id.valid() &&
                        context.workflow_node_id.index() < node_capability_failures.size()) {
                        node_capability_failures[context.workflow_node_id.index()] =
                            CapabilityFailureKind::Error;
                    }
                    return mismatch;
                }
                CapabilityCallResult memo_hit;
                memo_hit.status = CapabilityCallStatus::Success;
                memo_hit.value = evaluator::clone_value(entry->result);
                memo_hit.cache_hit = true;
                // RFC 0022 slice 5: fail closed if the memo Value's shape does not
                // match the capability's declared return type (corrupt snapshot).
                if (const auto *cap = index_.find_capability(name);
                    cap != nullptr && memo_hit.value.has_value() &&
                    !value_matches_return_type(*memo_hit.value, cap->return_type_ref)) {
                    CapabilityCallResult bad;
                    bad.status = CapabilityCallStatus::Error;
                    bad.error_message =
                        "durable resume memo Value type mismatch for capability '" + name +
                        "' (ordinal " + std::to_string(memo_ordinal) +
                        "): recovery snapshot is corrupt";
                    bad.diagnostic_code = std::string(error_codes::backend::ExecutionError.id);
                    if (context.workflow_node_id.valid() &&
                        context.workflow_node_id.index() < node_capability_failures.size()) {
                        node_capability_failures[context.workflow_node_id.index()] =
                            CapabilityFailureKind::Error;
                    }
                    return bad;
                }
                return memo_hit;
            }
            if (node_memo.replaying && memo_ordinal == node_memo.pending_ordinal) {
                // The previously-pending call: inject the host-supplied result and
                // leave replay mode so later calls run live again.
                node_memo.replaying = false;
                if (!config_.resume_pending_result.has_value()) {
                    CapabilityCallResult missing;
                    missing.status = CapabilityCallStatus::Error;
                    missing.error_message =
                        "durable resume is missing the pending capability result";
                    missing.diagnostic_code =
                        std::string(error_codes::backend::ExecutionError.id);
                    return missing;
                }
                // RFC 0022 slice 5: fail closed if the injected result's shape does
                // not match the capability's declared return type — never coerce.
                if (const auto *cap = index_.find_capability(name);
                    cap != nullptr &&
                    !value_matches_return_type(*config_.resume_pending_result,
                                               cap->return_type_ref)) {
                    CapabilityCallResult bad;
                    bad.status = CapabilityCallStatus::Error;
                    bad.error_message =
                        "durable resume pending-result type mismatch for capability '" + name +
                        "': host supplied a value of the wrong type";
                    bad.diagnostic_code = std::string(error_codes::backend::ExecutionError.id);
                    if (context.workflow_node_id.valid() &&
                        context.workflow_node_id.index() < node_capability_failures.size()) {
                        node_capability_failures[context.workflow_node_id.index()] =
                            CapabilityFailureKind::Error;
                    }
                    return bad;
                }
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = memo_ordinal,
                    .cap_id = cap_symbol_id,
                    .arg_hash = arg_hash,
                    .result = evaluator::clone_value(*config_.resume_pending_result),
                });
                CapabilityCallResult resumed;
                resumed.status = CapabilityCallStatus::Success;
                resumed.value = evaluator::clone_value(*config_.resume_pending_result);
                return resumed;
            }

            const auto invocation =
                result.metadata.add_invocation(context.workflow_node_id, *capability);
            auto invocation_context = context;
            invocation_context.capability_id = *capability;
            invocation_context.invocation_id = invocation;
            // RFC 0022 slice 4 (exactly-once): stamp the reproducible idempotency
            // key so the host can dedup effects across a crash/resume.
            invocation_context.idempotency_key = compute_idempotency_key(
                context.workflow_id.valid() ? context.workflow_id.index() : 0,
                context.workflow_node_id.valid() ? context.workflow_node_id.index() : 0,
                memo_ordinal, cap_symbol_id, arg_hash);

            // RFC 0022 slice 4: for an effect at level >= durable_write, persist a
            // write-ahead "committed, result-pending" intent BEFORE dispatch, so a
            // crash between the host effect and the memo append is recoverable: on
            // resume the host sees the same idempotency key and dedups rather than
            // re-committing. Read-only / external-side-effect calls skip this.
            if (config_.durable_write_intent_sink && cap_symbol_id != 0) {
                if (const auto *cap = index_.find_capability(name);
                    cap != nullptr &&
                    (cap->effect.kind == ir::CapabilityEffectKind::DurableWrite ||
                     cap->effect.kind == ir::CapabilityEffectKind::FinancialWrite)) {
                    config_.durable_write_intent_sink(invocation_context.idempotency_key, name);
                }
            }

            if (config_.capability_invoked_hook) {
                config_.capability_invoked_hook(context.agent_id, name);
            }

            CapabilityCallResult call_result;
            if (contextual_invoker.has_value()) {
                call_result = (*contextual_invoker)(invocation_context, name, arguments);
            } else {
                call_result = (*config_.capability_invoker)(name, arguments);
            }
            if (config_.capability_result_observer) {
                config_.capability_result_observer(invocation_context, call_result);
            }

            // RFC 0022 (C5): a pending call suspends the node. Stamp the memo
            // coordinate onto both the running memo state and the result so the
            // CallEvalFn boundary can build an EvalResult::suspension, then return
            // BEFORE the failure-classification loop — Pending is not a failure,
            // so no CapabilityFailed event and no node_capability_failures entry.
            // No CapabilityStarted is emitted either: the invocation has no
            // terminal yet (it completes on resume), so opening its lifecycle here
            // would leave a dangling Start. The node-level NodeSuspended terminal
            // records the pause instead.
            if (call_result.status == CapabilityCallStatus::Pending) {
                node_memo.suspended = true;
                node_memo.pending_cap_id = cap_symbol_id;
                node_memo.pending_ordinal = memo_ordinal;
                call_result.pending_cap_id = cap_symbol_id;
                call_result.pending_ordinal = memo_ordinal;
                return call_result;
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
                // RFC 0022 (C5): record the completed call in the node memo so a
                // later suspension in the same node can replay it deterministically
                // instead of re-invoking. Keyed by the stable per-node ordinal;
                // cap_id + arg_hash are integrity cross-checks asserted on replay.
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = memo_ordinal,
                    .cap_id = cap_symbol_id,
                    .arg_hash = arg_hash,
                    .result = call_result.value.has_value()
                                  ? evaluator::clone_value(*call_result.value)
                                  : evaluator::make_none(),
                });
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
        // RFC 0022 (C5): fresh memo table per node. Ordinals restart at 0 so the
        // memo key is stable and reproducible when this node is later resumed.
        node_memo.reset();
        // RFC 0022 (C7): is this the node that suspended? If so, seed replay
        // state so the invoker serves completed calls from the memo and injects
        // the host-supplied result at the pending ordinal.
        const bool resuming_node =
            config_.recovery_snapshot.has_value() &&
            config_.recovery_snapshot->suspended.has_value() &&
            config_.recovery_snapshot->suspended->node == node.id;
        if (resuming_node) {
            const auto &record = *config_.recovery_snapshot->suspended;
            node_memo.replaying = true;
            node_memo.pending_ordinal = record.pending_ordinal;
            node_memo.pending_cap_id = record.pending_cap_id;
            for (const auto &entry : record.memo) {
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = entry.ordinal,
                    .cap_id = entry.cap_id,
                    .arg_hash = entry.arg_hash,
                    .result = evaluator::clone_value(entry.result),
                });
            }
        }
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
            // RFC 0022 (C6): a capability nested in the node-input expression
            // suspended. Checked before has_errors() (suspension never rides in
            // the diagnostic bag). No node_input is captured — resume re-runs the
            // node-input expression, replaying the memo. Not a failure.
            if (eval_result.is_suspended()) {
                emit(NodeSuspended{
                    .node = node.id,
                    .agent = node.agent,
                    .pending_cap_id = node_memo.pending_cap_id,
                    .pending_ordinal = node_memo.pending_ordinal,
                });
                workflow_suspended = SuspendedNodeState{
                    .node = node.id,
                    .agent = node.agent,
                    .node_input = std::nullopt,
                    .pending_cap_id = node_memo.pending_cap_id,
                    .pending_ordinal = node_memo.pending_ordinal,
                    .memo = std::move(node_memo.memo),
                };
                break;
            }
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
        // RFC 0022 (C6): preserve the node input before it is moved into the
        // agent — a suspension needs it verbatim to re-run the node on resume.
        evaluator::Value node_input_snapshot = evaluator::clone_value(node_input);
        AgentResult agent_result = agent_rt.run(std::move(node_input));

        // RFC 0022 (C6): the agent suspended on a pending capability call.
        // Checked before the Completed / failure branches: build the resume
        // record from the captured input + running memo and stop the run without
        // marking the node failed. Not a failure.
        if (agent_result.status == AgentStatus::Suspended) {
            result.diagnostics.append(agent_result.diagnostics);
            emit(NodeSuspended{
                .node = node.id,
                .agent = node.agent,
                .pending_cap_id = node_memo.pending_cap_id,
                .pending_ordinal = node_memo.pending_ordinal,
            });
            workflow_suspended = SuspendedNodeState{
                .node = node.id,
                .agent = node.agent,
                .node_input = std::move(node_input_snapshot),
                .pending_cap_id = node_memo.pending_cap_id,
                .pending_ordinal = node_memo.pending_ordinal,
                .memo = std::move(node_memo.memo),
            };
            break;
        }

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
    if (!workflow_failure.has_value() && !workflow_suspended.has_value() &&
        workflow->return_value) {
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

    if (workflow_suspended.has_value()) {
        // RFC 0022 (C6): build the v2 resume record — the completed nodes so far
        // plus the suspended node's input and memo — persist it, and terminate as
        // Suspended (not Failed). If persistence fails we cannot resume, so we
        // fail closed: downgrade to a NodeFailed terminal.
        WorkflowRecoverySnapshot snapshot{
            .workflow = plan.workflow,
            .checkpoint = CheckpointId{0},
            .suspended = std::move(*workflow_suspended),
        };
        for (const auto &completed_node : plan.nodes) {
            if (!completed_nodes[completed_node.id.index()]) {
                continue;
            }
            RecoveredNodeState node_state{
                .node = completed_node.id,
                .agent = completed_node.agent,
            };
            if (node_outputs[completed_node.id.index()].has_value()) {
                const auto *value = result.value(*node_outputs[completed_node.id.index()]);
                if (value != nullptr) {
                    node_state.output = evaluator::clone_value(*value);
                }
            }
            snapshot.completed_nodes.push_back(std::move(node_state));
        }

        const bool persisted = config_.recovery_store == nullptr ||
                               config_.recovery_store->save(snapshot).has_value();
        if (persisted) {
            emit(WorkflowSuspended{
                .workflow = plan.workflow,
                .node = snapshot.suspended->node,
                .pending_cap_id = snapshot.suspended->pending_cap_id,
                .pending_ordinal = snapshot.suspended->pending_ordinal,
            });
            emit(RunCompleted{.run = run_id, .status = RunTerminalStatus::Suspended});
            result.suspended = std::move(snapshot);
        } else {
            const auto diagnostic = add_runtime_error(
                result, "failed to persist workflow suspension resume record");
            emit(WorkflowFailed{
                .workflow = plan.workflow,
                .diagnostic = diagnostic,
                .kind = WorkflowFailureKind::NodeFailed,
            });
            emit(RunCompleted{.run = run_id,
                              .status = terminal_status(WorkflowFailureKind::NodeFailed)});
        }
    } else if (workflow_failure.has_value()) {
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
