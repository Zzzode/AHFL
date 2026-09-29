#include "runtime/engine/workflow_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/capability_eval.hpp"
#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/engine/core_wire_codec_recovery.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/value/value_json.hpp"

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
    // from `replay_source` instead of live-invoked, and pending_ordinal receives
    // the host-supplied resume result.
    bool replaying{false};
    // RFC 0026 C2b stage3 (P0-13): a READ-ONLY pointer into the recovery snapshot's
    // suspended memo. Replay hits validate + decode the ORIGINAL persisted entry
    // here (never pre-cloned into `memo`), so clone_value can never launder a
    // hostile null-child collection before the trust gate. Each successful hit
    // appends a FRESH NativeOnly canonical entry (the decoded Value) to `memo`,
    // rebuilding a dense prefix for a subsequent suspension — the persisted source
    // provenance (Legacy/Sidecar) is NEVER copied into `memo`.
    const std::vector<CapabilityMemoEntry> *replay_source{nullptr};

    void reset() {
        next_ordinal = 0;
        memo.clear();
        suspended = false;
        pending_cap_id = 0;
        pending_ordinal = 0;
        replaying = false;
        replay_source = nullptr;
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

// RFC 0026 C2b stage3: the immutable per-capability result wire-schema binding
// cache, keyed by CapabilityDecl.symbol_ref.id. Built once at construction. Each
// id maps to a BindingCacheEntry that is EITHER a verified binding OR a
// schema_failure string (a fixed prefix + compiler/schema diagnostic code:message
// — never runtime payload). `global_failure` is set when the whole type
// environment failed to build, in which case every lookup reports that failure.
// A cap with no cache entry at all is a genuine miss (unknown/absent id), kept
// DISTINCT from a stored schema failure so consume and tests can tell them apart.
struct BindingCacheEntry {
    std::optional<ir::core::VerifiedWireSchemaBinding> binding;
    std::string schema_failure; // non-empty iff projection failed; no payload
};

struct WireResultBindingCache {
    std::string global_failure; // non-empty iff the type environment failed to build
    std::unordered_map<std::size_t, BindingCacheEntry> by_symbol_id;

    // Tri-state lookup outcome for a capability declaration symbol id.
    enum class Status { Binding, MissingId, SchemaFailure };
    struct Lookup {
        Status status{Status::MissingId};
        const ir::core::VerifiedWireSchemaBinding *binding{nullptr};
        std::string failure; // set for SchemaFailure (or the global env failure)
    };

    [[nodiscard]] Lookup find(std::size_t cap_decl_symbol_id) const {
        if (!global_failure.empty()) {
            return {Status::SchemaFailure, nullptr, global_failure};
        }
        const auto it = by_symbol_id.find(cap_decl_symbol_id);
        if (it == by_symbol_id.end()) {
            return {Status::MissingId, nullptr, {}};
        }
        if (it->second.binding.has_value()) {
            return {Status::Binding, &*it->second.binding, {}};
        }
        return {Status::SchemaFailure, nullptr, it->second.schema_failure};
    }
};

namespace {

// Compose a schema-only failure string from the first error diagnostic (fixed
// prefix + code:message). Never includes runtime payload / observed values.
[[nodiscard]] std::string
schema_failure_text(const std::vector<ir::core::CoreLowerDiagnostic> &diagnostics) {
    for (const auto &d : diagnostics) {
        if (d.severity == ir::core::CoreDiagnosticSeverity::Error) {
            return "wire-schema projection failed: " + d.code + ": " + d.message;
        }
    }
    return "wire-schema projection failed";
}

// Build the immutable binding cache from the program. Never throws / never errors
// construction: a failed environment sets global_failure; a capability whose
// result type does not project (or whose SymbolId collides) stores a schema
// failure; a capability without a symbol id is skipped (a later hit fails closed
// on the missing identity). Fail-closed is applied lazily at consume, so an
// unused capability never blocks a run.
[[nodiscard]] std::shared_ptr<const WireResultBindingCache>
build_wire_binding_cache(const ir::Program &program, const ir::ProgramIndex &index) {
    auto cache = std::make_shared<WireResultBindingCache>();
    auto env_result = ir::core::build_core_type_environment(program);
    if (!env_result.ok() || !env_result.environment.has_value()) {
        cache->global_failure = schema_failure_text(env_result.diagnostics);
        if (cache->global_failure.empty()) {
            cache->global_failure = "wire-schema type environment build failed";
        }
        return cache;
    }
    const auto &environment = *env_result.environment;
    for (const auto *cap : index.capabilities()) {
        if (cap == nullptr || !cap->symbol_ref.id.has_value()) {
            // No name/0 fallback: a capability without a stable symbol id gets no
            // entry, so a memo/pending hit that resolves to it is a MissingId miss.
            continue;
        }
        const std::size_t id = *cap->symbol_ref.id;
        // Reject a SymbolId collision (theoretically impossible, but never allow
        // unordered_map last-write-wins to silently pick one binding): force a
        // stored schema failure for that id.
        if (cache->by_symbol_id.contains(id)) {
            cache->by_symbol_id[id] =
                BindingCacheEntry{std::nullopt, "wire-schema projection failed: duplicate "
                                                "capability symbol id"};
            continue;
        }
        auto migration =
            ir::core::migrate_type_ref_to_wire_binding(cap->return_type_ref, environment);
        if (migration.ok() && migration.binding.has_value()) {
            cache->by_symbol_id.emplace(
                id, BindingCacheEntry{std::move(*migration.binding), {}});
        } else {
            cache->by_symbol_id.emplace(
                id, BindingCacheEntry{std::nullopt, schema_failure_text(migration.diagnostics)});
        }
    }
    return cache;
}

// RFC 0026 C2b stage3 (P0-13/17/18): the trusted outcome of decoding a persisted
// memo/pending result — the canonical native Value plus the RESOLVED presence bit.
// `present==false` means a valueless success (the caller must return
// CapabilityCallResult.value=nullopt so the evaluator still sees NoneValue), while
// `canonical` still holds the schema-canonical Value (Unit) for the fresh
// NativeOnly memo append. Carrying both preserves the historical observable
// (valueless -> None at the call site) without losing the canonical shape.
struct TrustedMemoResult {
    runtime::Value canonical;
    bool present{true};
};

// RFC 0026 C2b stage3 (P0-13/17/19): validate a NATIVE trusted result (a
// programmatic memo entry's Value or a host-supplied pending Value) against its
// binding, on the ORIGINAL value WITHOUT cloning first — so a malformed
// null-child List/Set/Struct/Map can never be laundered into a shorter "valid"
// collection before the trust gate. Returns the canonical Value + resolved
// presence, cloning ONLY after validation succeeds.
[[nodiscard]] std::expected<TrustedMemoResult, std::string>
validate_native_trusted_result(const runtime::Value &original, bool present,
                               const ir::core::VerifiedWireSchemaBinding &binding) {
    const bool is_bare_none = std::holds_alternative<runtime::NoneValue>(original.node);
    const bool is_unit = std::holds_alternative<runtime::UnitValue>(original.node);
    if (!present) {
        // Valueless: only NoneValue / UnitValue whose wire is exactly null, and
        // only under an exact Unit root. Canonicalize to Unit, keep present=false.
        if (!(is_bare_none || is_unit) || runtime::value_to_json(original) != "null") {
            return std::unexpected(std::string("valueless memo result is not a null Unit"));
        }
        auto probe = wire_codec::validate_value(runtime::make_unit(), binding);
        if (!probe.valid) {
            return std::unexpected(probe.error);
        }
        return TrustedMemoResult{runtime::make_unit(), false};
    }
    // present==true: the established compat case (true + bare None) is a legacy
    // valueless success — normalize to Unit + present=false ONLY under an exact
    // Unit root (never a strict-None reject, never present=true).
    if (is_bare_none) {
        auto probe = wire_codec::validate_value(runtime::make_unit(), binding);
        if (!probe.valid) {
            return std::unexpected(probe.error);
        }
        return TrustedMemoResult{runtime::make_unit(), false};
    }
    auto validation = wire_codec::validate_value(original, binding);
    if (!validation.valid) {
        return std::unexpected(validation.error);
    }
    return TrustedMemoResult{runtime::clone_value(original), true};
}

// Decode a persisted memo/pending result under its verified binding into a
// TrustedMemoResult. `binding` is for the capability this entry resolves to
// (identity already confirmed by the caller). Runs the structural three-state gate
// FIRST, then the source-specific decode/validate. Returns a schema-only error
// string (no payload echo) on any fail-closed condition. The native `entry.result`
// projection is trusted ONLY for a NativeOnly entry.
[[nodiscard]] std::expected<TrustedMemoResult, std::string>
decode_persisted_memo_result(const CapabilityMemoEntry &entry,
                             const ir::core::VerifiedWireSchemaBinding &binding) {
    // Structural state gate (same well-formedness as save/load; consume is a real
    // fail-closed gate, not merely a construction convenience).
    if (!memo_result_state_well_formed(entry)) {
        return std::unexpected(std::string("memo entry result state is ill-formed"));
    }
    switch (entry.source) {
    case PersistedMemoResultSource::NativeOnly:
        // validate the ORIGINAL entry.result in place (no pre-clone laundering).
        return validate_native_trusted_result(entry.result, *entry.result_present, binding);
    case PersistedMemoResultSource::ExactSidecar: {
        auto dom = ahfl::json::parse_json(*entry.authoritative_json);
        if (!dom.has_value() || !*dom) {
            return std::unexpected(std::string("memo entry authoritative wire result is malformed"));
        }
        const bool present = *entry.result_present; // well-formed => engaged
        auto decoded = wire_codec::decode_json(**dom, binding);
        if (!decoded.ok()) {
            return std::unexpected(decoded.error);
        }
        if (!present && !std::holds_alternative<runtime::UnitValue>(decoded.value->node)) {
            return std::unexpected(
                std::string("valueless memo result is not a Unit under its schema"));
        }
        return TrustedMemoResult{std::move(*decoded.value), present};
    }
    case PersistedMemoResultSource::LegacyV2: {
        auto dom = ahfl::json::parse_json(*entry.authoritative_json);
        if (!dom.has_value() || !*dom) {
            return std::unexpected(std::string("memo entry authoritative wire result is malformed"));
        }
        // The pre-sidecar bytes may encode an integral Float as a bare int; the
        // recovery-internal legacy decoder accepts SignedInteger -> Float.
        auto decoded = wire_codec::decode_json_legacy_v2(**dom, binding);
        if (!decoded.ok()) {
            return std::unexpected(decoded.error);
        }
        // Presence is irreversibly unknown for pre-sidecar bytes; resolve by the
        // chosen historical default: a decoded Unit -> present=false (old replay
        // presented NoneValue), any other value -> present=true.
        const bool present = !std::holds_alternative<runtime::UnitValue>(decoded.value->node);
        return TrustedMemoResult{std::move(*decoded.value), present};
    }
    }
    return std::unexpected(std::string("memo entry has an unknown result source"));
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
    : program_(program), index_(program_), config_(std::move(config)),
      wire_binding_cache_(build_wire_binding_cache(program_, index_)) {}

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

    ctx.bind_local("input", runtime::clone_value(workflow_input));

    if (const auto *sv = std::get_if<runtime::StructValue>(&workflow_input.node)) {
        for (const auto &[field_name, field_val] : sv->fields) {
            if (field_val) {
                ctx.set_input(field_name, runtime::clone_value(*field_val));
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
        ctx.bind_local(metadata->display_name, runtime::clone_value(*node_value));
        if (const auto *sv = std::get_if<runtime::StructValue>(&node_value->node)) {
            for (const auto &[field_name, field_val] : sv->fields) {
                if (field_val) {
                    ctx.set_node_output(
                        metadata->display_name, field_name, runtime::clone_value(*field_val));
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
                output = add_runtime_value(result, runtime::clone_value(*state.output));
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
            // Strict wire hash: a non-encodable argument (a closure) fails the
            // capability call closed — hashing it as empty bytes would ledger a
            // digest every such call shares and corrupt the replay coordinate
            // gate below.
            const auto arg_hash_or = runtime::hash_values(arguments);
            if (!arg_hash_or.has_value()) {
                CapabilityCallResult unencodable;
                unencodable.status = CapabilityCallStatus::Error;
                unencodable.error_message =
                    "capability '" + name +
                    "' received a non-wire argument (a closure cannot cross the "
                    "capability frame boundary)";
                unencodable.diagnostic_code =
                    std::string(error_codes::backend::ExecutionError.id);
                if (context.workflow_node_id.valid() &&
                    context.workflow_node_id.index() < node_capability_failures.size()) {
                    node_capability_failures[context.workflow_node_id.index()] =
                        CapabilityFailureKind::Error;
                }
                return unencodable;
            }
            const std::uint64_t arg_hash = *arg_hash_or;

            // RFC 0022 (C7): resume replay. While replaying, calls at ordinals
            // below the pending one are served from the memo (never re-invoked),
            // and the pending ordinal receives the host-supplied resume result.
            // Both are matched by ordinal (the stable per-node key) and
            // cross-checked on cap_id + arg_hash — a mismatch is fail-closed
            // (non-deterministic replay), never a silent live re-invoke.
            if (node_memo.replaying && memo_ordinal < node_memo.pending_ordinal) {
                // Coordinate gate FIRST (unchanged diagnostic priority): find the
                // ORIGINAL persisted entry by ordinal in the read-only replay
                // source; a missing entry or cap_id/arg_hash mismatch is a
                // non-deterministic replay divergence.
                const auto *source = node_memo.replay_source;
                const CapabilityMemoEntry *entry = nullptr;
                if (source != nullptr) {
                    for (const auto &e : *source) {
                        if (e.ordinal == memo_ordinal) {
                            entry = &e;
                            break;
                        }
                    }
                }
                if (entry == nullptr || entry->cap_id != cap_symbol_id ||
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
                // Identity gate: presence-checked source symbol id, a resolvable
                // decl, and its symbol id must equal the replay entry's cap_id.
                // Fail closed BEFORE any decode/clone so a hostile entry can never
                // borrow another same-name capability's binding.
                auto fail = [&](std::string msg) -> CapabilityCallResult {
                    CapabilityCallResult bad;
                    bad.status = CapabilityCallStatus::Error;
                    bad.error_message = std::move(msg);
                    bad.diagnostic_code = std::string(error_codes::backend::ExecutionError.id);
                    if (context.workflow_node_id.valid() &&
                        context.workflow_node_id.index() < node_capability_failures.size()) {
                        node_capability_failures[context.workflow_node_id.index()] =
                            CapabilityFailureKind::Error;
                    }
                    return bad;
                };
                if (!context.source_capability_symbol_id.has_value()) {
                    return fail("durable resume memo entry has no capability identity (ordinal " +
                                std::to_string(memo_ordinal) + ")");
                }
                const auto *cap = index_.find_capability(name);
                if (cap == nullptr || !cap->symbol_ref.id.has_value() ||
                    *cap->symbol_ref.id != entry->cap_id) {
                    return fail("durable resume memo capability identity mismatch (ordinal " +
                                std::to_string(memo_ordinal) + ")");
                }
                // Binding lookup (identity already confirmed): a stored schema
                // failure / MissingId is fail-closed with the schema-only text.
                const auto lookup = wire_binding_cache_->find(entry->cap_id);
                if (lookup.status != WireResultBindingCache::Status::Binding) {
                    std::string msg =
                        "durable resume memo result is not decodable under its schema (ordinal " +
                        std::to_string(memo_ordinal) + ")";
                    if (!lookup.failure.empty()) {
                        msg += ": " + lookup.failure;
                    }
                    return fail(std::move(msg));
                }
                // Decode the ORIGINAL persisted entry under its binding (exact or
                // legacy per source). The native projection is never trusted here.
                auto decoded = decode_persisted_memo_result(*entry, *lookup.binding);
                if (!decoded.has_value()) {
                    return fail("durable resume memo Value type mismatch for capability '" + name +
                                "' (ordinal " + std::to_string(memo_ordinal) +
                                "): recovery snapshot is corrupt");
                }
                // Append a FRESH NativeOnly canonical entry to the dense prefix
                // (never the persisted source/authoritative_json), carrying the
                // RESOLVED presence bit, so a subsequent suspension re-serializes a
                // correct prefix.
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = memo_ordinal,
                    .cap_id = cap_symbol_id,
                    .arg_hash = arg_hash,
                    .result = runtime::clone_value(decoded->canonical),
                    .source = PersistedMemoResultSource::NativeOnly,
                    .authoritative_json = std::nullopt,
                    .result_present = decoded->present,
                });
                CapabilityCallResult memo_hit;
                memo_hit.status = CapabilityCallStatus::Success;
                // Preserve the historical observable: a valueless success returns
                // no value (evaluator sees NoneValue); a present result returns it.
                if (decoded->present) {
                    memo_hit.value = std::move(decoded->canonical);
                }
                memo_hit.cache_hit = true;
                return memo_hit;
            }
            if (node_memo.replaying && memo_ordinal == node_memo.pending_ordinal) {
                // The previously-pending call. P0-15: verify the pending-call
                // IDENTITY before reading / validating / cloning the host result, and
                // before leaving replay mode, so a divergent pending ordinal can
                // never inject another capability's result. pending record carries
                // no arg_hash, so capability identity is the available integrity gate.
                auto fail = [&](std::string msg) -> CapabilityCallResult {
                    CapabilityCallResult bad;
                    bad.status = CapabilityCallStatus::Error;
                    bad.error_message = std::move(msg);
                    bad.diagnostic_code = std::string(error_codes::backend::ExecutionError.id);
                    if (context.workflow_node_id.valid() &&
                        context.workflow_node_id.index() < node_capability_failures.size()) {
                        node_capability_failures[context.workflow_node_id.index()] =
                            CapabilityFailureKind::Error;
                    }
                    return bad;
                };
                if (!context.source_capability_symbol_id.has_value()) {
                    return fail("durable resume pending call has no capability identity");
                }
                if (cap_symbol_id != node_memo.pending_cap_id) {
                    return fail("durable resume pending-call identity mismatch");
                }
                const auto *cap = index_.find_capability(name);
                if (cap == nullptr || !cap->symbol_ref.id.has_value() ||
                    *cap->symbol_ref.id != node_memo.pending_cap_id) {
                    return fail("durable resume pending-call identity mismatch");
                }
                const auto lookup = wire_binding_cache_->find(node_memo.pending_cap_id);
                if (lookup.status != WireResultBindingCache::Status::Binding) {
                    std::string msg = "durable resume pending result is not decodable under its "
                                      "schema";
                    if (!lookup.failure.empty()) {
                        msg += ": " + lookup.failure;
                    }
                    return fail(std::move(msg));
                }
                // Identity + binding confirmed. Resolve the pending result source
                // NOW (still in replay mode), after identity (P0-15) and binding
                // lookup — this source-state gate runs ONLY here, inside the replay
                // pending-ordinal branch, never as a global/ctor admission. The
                // native programmatic Value and the raw wire JSON are mutually
                // exclusive; supplying both is a fail-closed conflict.
                const bool has_native = config_.resume_pending_result.has_value();
                const bool has_raw = config_.resume_pending_result_wire_json.has_value();
                if (has_native && has_raw) {
                    return fail("durable resume received both a native and a raw wire "
                                "pending result for capability '" +
                                name + "'");
                }
                if (!has_native && !has_raw) {
                    CapabilityCallResult missing;
                    missing.status = CapabilityCallStatus::Error;
                    missing.error_message =
                        "durable resume is missing the pending capability result";
                    missing.diagnostic_code =
                        std::string(error_codes::backend::ExecutionError.id);
                    return missing;
                }

                TrustedMemoResult trusted{runtime::make_unit(), false};
                if (has_raw) {
                    // Raw wire path: a PRESENT flag is ALWAYS present=true. Parse the
                    // bytes and decode EXACTLY under the verified binding (schema-only
                    // errors, no payload echo). No native None/Unit compat and no
                    // `!present` valueless handling ride this token: raw `null`
                    // decodes to canonical Unit / Option None per the binding, both
                    // present=true.
                    auto dom = ahfl::json::parse_json(*config_.resume_pending_result_wire_json);
                    if (!dom.has_value() || !*dom) {
                        return fail("durable resume pending result is not valid wire JSON");
                    }
                    auto decoded = wire_codec::decode_json(**dom, *lookup.binding);
                    if (!decoded.ok()) {
                        return fail(decoded.error);
                    }
                    trusted = TrustedMemoResult{std::move(*decoded.value), true};
                } else {
                    // Native programmatic path: validate the ORIGINAL Value in place
                    // (P0-13: never clone before the trust gate — a malformed
                    // null-child collection must not be laundered). A NativeOnly
                    // true+None under a Unit binding is the established legacy-valueless
                    // case -> canonical Unit + present=false, not a strict-None reject.
                    auto decoded = validate_native_trusted_result(
                        *config_.resume_pending_result, /*present=*/true, *lookup.binding);
                    if (!decoded.has_value()) {
                        return fail("durable resume pending-result type mismatch for capability '" +
                                    name + "': host supplied a value of the wrong type");
                    }
                    trusted = std::move(*decoded);
                }
                // All gates passed: append the fresh entry FIRST, then leave replay
                // mode. Every failure above kept replaying=true so a corrected resume
                // can retry the same pending call (P0-3 ordering).
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = memo_ordinal,
                    .cap_id = cap_symbol_id,
                    .arg_hash = arg_hash,
                    .result = runtime::clone_value(trusted.canonical),
                    .source = PersistedMemoResultSource::NativeOnly,
                    .authoritative_json = std::nullopt,
                    .result_present = trusted.present,
                });
                node_memo.replaying = false;
                CapabilityCallResult resumed;
                resumed.status = CapabilityCallStatus::Success;
                if (trusted.present) {
                    resumed.value = std::move(trusted.canonical);
                }
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
            //
            // RFC 0026 C2b stage3 (presence-not-value): capability identity presence
            // is decided by source_capability_symbol_id.has_value(), NEVER by
            // cap_symbol_id != 0 — SymbolId 0 is a legal identity (same ruling as the
            // resume id=0 path). Gating on != 0 would silently skip the write-ahead
            // intent for a legitimate id=0 DurableWrite/FinancialWrite, leaving its
            // effect non-idempotent across a crash. The dereferenced SymbolId
            // (including 0) still flows into the idempotency key below.
            if (config_.durable_write_intent_sink &&
                context.source_capability_symbol_id.has_value()) {
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
                        add_runtime_value(result, runtime::clone_value(*call_result.value));
                }
                // RFC 0022 (C5): record the completed call in the node memo so a
                // later suspension in the same node can replay it deterministically
                // instead of re-invoking. Keyed by the stable per-node ordinal;
                // cap_id + arg_hash are integrity cross-checks asserted on replay.
                // RFC 0026 C2b stage3 (P0-17): a NativeOnly entry; the presence bit
                // records whether the live call actually produced a value (a
                // valueless success stores a NoneValue placeholder + present=false,
                // and does NOT change call_result / CapabilityCompleted.output).
                node_memo.memo.push_back(CapabilityMemoEntry{
                    .ordinal = memo_ordinal,
                    .cap_id = cap_symbol_id,
                    .arg_hash = arg_hash,
                    .result = call_result.value.has_value()
                                  ? runtime::clone_value(*call_result.value)
                                  : runtime::make_none(),
                    .source = PersistedMemoResultSource::NativeOnly,
                    .authoritative_json = std::nullopt,
                    .result_present = call_result.value.has_value(),
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

        runtime::Value node_input = runtime::make_none();
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
            // RFC 0026 C2b stage3 (P0-13): point at the persisted memo READ-ONLY;
            // do NOT pre-clone it into node_memo.memo (clone_value would silently
            // drop null List/Set/Struct children, laundering a hostile snapshot
            // before the per-ordinal trust gate). Each replay hit validates + decodes
            // the ORIGINAL entry and appends a fresh NativeOnly canonical entry.
            node_memo.replay_source = &record.memo;
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
        // RFC 0022 (C6): capture the node input before it is moved into the agent.
        // NOTE (RFC 0026 C2b stage3 accuracy): this snapshot is PERSISTED into the v2
        // record for format completeness / potential observation, but resume does NOT
        // restore execution from it — the node-input expression is RE-EVALUATED on
        // resume and its capability calls replayed from the memo (see the resume path
        // at the top of this loop). It is informational today, NOT a trust authority;
        // a reader must not assume it drives resume (residual risk if that changes).
        runtime::Value node_input_snapshot = runtime::clone_value(node_input);
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
            // RFC 0026 C2b stage3 (P0-21): a resuming node MUST reach and consume
            // its recorded pending capability call. If the recovered control flow
            // completes WITHOUT ever hitting the pending ordinal, `replaying` is
            // still true here — the replay diverged from the recorded run. Fail
            // closed BEFORE any node_completed_hook / checkpoint / NodeCompleted,
            // rather than silently completing. This only intercepts an
            // otherwise-success completion; it never overrides an eval/agent failure
            // (those are handled above) and performs no live invoke.
            if (resuming_node && node_memo.replaying) {
                // This branch is NOT routed through capability_eval, so build the
                // diagnostic with the EXECUTION_ERROR code directly (add_runtime_error
                // sets no code). No CapabilityFailed is emitted — there was no real
                // capability invocation. Preserve the completed agent's non-error
                // diagnostics first (the normal Completed path appends them), so a
                // warning/notice is not silently dropped by failing here.
                result.diagnostics.append(agent_result.diagnostics);
                const DiagnosticId diagnostic{result.diagnostics.entries().size()};
                result.diagnostics.error()
                    .message("durable resume replay diverged: the recorded pending capability "
                             "call (ordinal " +
                             std::to_string(node_memo.pending_ordinal) +
                             ") was never reached before the node completed")
                    .code(std::string(error_codes::backend::ExecutionError.id))
                    .emit();
                failed_nodes[node.id.index()] = true;
                if (node.id.valid() && node.id.index() < node_capability_failures.size()) {
                    node_capability_failures[node.id.index()] = CapabilityFailureKind::Error;
                }
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
                            state.output = runtime::clone_value(*value);
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
                    node_state.output = runtime::clone_value(*value);
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
