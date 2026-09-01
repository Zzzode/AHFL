#pragma once

#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
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
#include "runtime/engine/native_host_binding.hpp"
#include "runtime/engine/workflow_recovery.hpp"
#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

// RFC 0026 C2b stage3: opaque immutable per-capability result wire-schema binding
// cache; defined in workflow_runtime.cpp so the core wire types stay out of this
// header. WorkflowRuntime holds one by shared_ptr<const> (see below).
struct WireResultBindingCache;

// Workflow execution status
enum class WorkflowStatus {
    Completed,
    NodeFailed,
    DependencyFailed,
    EvalError,
    // RFC 0022 (durable resume): a node suspended on a pending capability call.
    // Not a failure — WorkflowResult::suspended holds the resume record.
    Suspended,
};

// Workflow execution result
struct WorkflowResult {
    ExecutionMetadataStore metadata;
    ExecutionEventStore events;
    ExecutionReport report;
    std::vector<Value> values;
    DiagnosticBag diagnostics;
    // RFC 0022 (durable resume): present iff status() == Suspended. The resume
    // record for the suspended node — its input Value plus the memo table of
    // capability results already produced — so the run can be continued by
    // passing this snapshot back via WorkflowRuntimeConfig::recovery_snapshot
    // together with resume_pending_result.
    std::optional<WorkflowRecoverySnapshot> suspended{};

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
    // Native projection of the ahfl_host.h capability ABI (RFC 0021 slice 2).
    // When set, the runtime derives its capability dispatch from this
    // function-pointer table via make_native_capability_invoker(). Takes
    // precedence over contextual_capability_invoker / capability_invoker so a
    // host that speaks the published ABI need not also wire a std::function.
    std::optional<NativeHostBinding> native_host_binding;
    std::function<bool()> cancellation_requested;
    std::function<bool()> interruption_requested;
    std::optional<CheckpointId> resume_checkpoint;
    std::function<std::optional<CheckpointId>(WorkflowNodeId)> checkpoint_after_node;
    std::optional<WorkflowRecoverySnapshot> recovery_snapshot;
    WorkflowRecoveryStore *recovery_store{nullptr};
    // RFC 0022 (durable resume): the host-supplied result for the capability
    // call that previously suspended the workflow. Set together with
    // recovery_snapshot to resume: the runtime re-runs the suspended node from
    // its captured input, replays completed calls from the memo, and injects
    // this value at the pending ordinal instead of re-invoking the host.
    std::optional<Value> resume_pending_result;
    std::function<void(const CapabilityInvocationContext &, const CapabilityCallResult &)>
        capability_result_observer;
    // RFC 0022 slice 4 (exactly-once): invoked with (idempotency_key, capability
    // name) right BEFORE a capability whose effect level is >= durable_write is
    // dispatched — the write-ahead "committed, result-pending" intent. The host
    // persists the key so that on resume it can dedup an effect that committed
    // before a crash. Called only for durable_write / financial_write effects.
    std::function<void(std::uint64_t idempotency_key, std::string_view capability_name)>
        durable_write_intent_sink;
    // Debug/test hook invoked after the runtime records an agent state entry.
    // Runs on the workflow execution thread; a debugger may block inside this
    // hook to implement pause (RFC 0015). `agent_name` / `node_name` are the
    // canonical agent name and the workflow node name executing it, so a
    // debugger can build human-readable stack frames without re-deriving
    // runtime metadata IDs.
    std::function<void(AgentId,
                       std::string_view agent_name,
                       std::string_view node_name,
                       std::string_view state_name)>
        state_entered_hook;
    // Debug/test hook invoked right before a capability call is dispatched to
    // the configured invoker. Runs on the workflow execution thread; a
    // debugger may block inside this hook to implement pause (RFC 0015).
    std::function<void(AgentId, std::string_view)> capability_invoked_hook;
    // Debug/test hook invoked with the evaluated node input right before the
    // node's agent starts executing. Runs on the workflow execution thread;
    // a debugger may block inside this hook to implement pause (RFC 0015).
    // The only way to observe the live agent input, which is otherwise moved
    // into AgentRuntime::run and never exposed again.
    std::function<void(AgentId,
                       std::string_view agent_name,
                       std::string_view node_name,
                       const Value &)>
        agent_input_hook;
    // Debug/test hook invoked with a node's output value right after the
    // node's agent completes successfully. Runs on the workflow execution
    // thread (RFC 0015). Exposes live node results for the debugger's
    // Workflow scope; the value is consumed by the runtime afterwards.
    std::function<void(AgentId, std::string_view node_name, const Value &)> node_completed_hook;
    // RFC 0012 slice 2: injectable monotonic clock for execution-event offsets.
    // Every event's monotonic_offset is measured against the first reading taken
    // at the start of run(). Defaults to std::chrono::steady_clock::now; tests
    // inject a deterministic clock so golden output does not depend on wall time
    // (Test Plan #3). Must be monotonic non-decreasing; it never sources
    // wall-clock timestamps (those are optional run metadata only).
    std::function<std::chrono::steady_clock::time_point()> monotonic_clock;
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
    // RFC 0026 C2b stage3: an immutable, SymbolId-keyed cache of per-capability
    // result wire-schema bindings, built ONCE at construction from the program's
    // type environment. Each entry is either a verified binding or a schema-only
    // failure; a global environment-build failure yields an all-failure cache. It
    // is consumed read-only during resume (durable memo/pending trust decode) and
    // never blocks a run for an unused capability. Opaque here so the core wire
    // types stay out of this header (defined in workflow_runtime.cpp).
    std::shared_ptr<const WireResultBindingCache> wire_binding_cache_;

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
