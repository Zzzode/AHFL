#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/runtime/execution_event.hpp"
#include "runtime/value/value.hpp"
#include "tooling/dap/breakpoints.hpp"

#ifdef AHFL_ENABLE_BACKEND_WASM
#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"
#endif

namespace ahfl::dap {

class DapServer;
class BreakpointManager;

/// The kind of step a DebugSession has armed (RFC 0015 Slice 4).
enum class StepKind {
    None,  ///< No step pending; execution runs until a breakpoint or completion.
    Over,  ///< DAP `next`: pause at the next state transition away from the origin.
    Into,  ///< DAP `stepIn`: same as Over until capability step-into lands.
    Out    ///< DAP `stepOut`: pause when the workflow nesting depth decreases.
};

/// State machine for DAP stepping. A step is armed by a `next` / `stepIn` /
/// `stepOut` request while the session is paused and consumed by the first
/// debug hook (state entry / capability invocation) that satisfies it.
struct DebugStepper {
    StepKind pending{StepKind::None};
    /// For StepOver / StepIn: the (agent, state) the step was armed from. The
    /// step completes at the first state transition to a different position.
    std::string step_over_agent;
    std::string step_over_state;
    /// For StepOut: the workflow nesting depth the step was armed from. The
    /// step completes when the depth drops below this value. Always 0 until
    /// sub-workflow debugging lands.
    std::size_t step_out_depth{0};
};

/// A single entry in the DAP call stack (RFC 0015 Slice 5).
///
/// Frames are synthesized by the debug hooks installed on the wasm-backed
/// workflow runtime: a workflow frame at launch, a node frame synthesized on
/// state entry (the wasm lane has no agent_input hook, so the node frame is
/// pushed when the first state of a node is entered), a state frame on every
/// state entry, and a capability frame on every capability invocation. The
/// runtime exposes no exit events, so frames use replacement semantics rather
/// than push/pop pairs: a state entry for the same agent replaces the top
/// state frame (a transition), a state entry for a different agent (or a new
/// node) pops the previous node's frames, and a capability invocation
/// replaces the top capability frame. This models the sequential node-by-node
/// execution of the workflow runtime honestly; true call/return frame
/// nesting arrives with sub-workflow support.
struct DebugFrame {
    enum class Kind { Workflow, Node, State, Capability };

    Kind kind{Kind::State};
    /// Display name: "agent::state" for state frames, the node / capability /
    /// workflow name otherwise.
    std::string name;
    /// Source file path. Empty for frames without a known source location
    /// (capability call sites are not exposed by the runtime hooks).
    std::string source_file;
    /// 1-based source line; 0 when unknown.
    int line{0};
    /// Debug agent id string ("1", "2", ...); empty for the workflow frame.
    std::string agent_id;
    /// Canonical agent name; empty for the workflow frame.
    std::string agent_name;
    /// Workflow node name; empty for capability and workflow frames.
    std::string node_name;
    /// State name; only set for state frames.
    std::string state_name;
};

/// DebugSession owns the debug lifecycle for one launched workflow: source
/// compilation, the execution thread, pause/resume coordination, the frame
/// stack and DAP event emission (RFC 0015).
///
/// The workflow runs on a dedicated worker thread. Debug hooks installed on
/// the WorkflowRuntime update the frame stack and variable store, consult the
/// BreakpointManager and, on a hit, block the worker inside the hook until
/// resume() or disconnect() is called.
class DebugSession {
  public:
    DebugSession(DapServer &server, BreakpointManager &breakpoints);
    ~DebugSession();

    DebugSession(const DebugSession &) = delete;
    DebugSession &operator=(const DebugSession &) = delete;
    DebugSession(DebugSession &&) = delete;
    DebugSession &operator=(DebugSession &&) = delete;

    /// Compile and launch the workflow described by a DAP `launch` request
    /// body: a JSON object with "program" (path to a .ahfl source file),
    /// "workflow" (workflow name) and optional "input" (JSON value parsed as
    /// the workflow input) fields. Returns the response body for the launch
    /// response; failures are reported as `output` + `terminated` events.
    [[nodiscard]] std::string launch(const std::string &config_json);

    /// Request the workflow to stop and join the execution thread. Emits a
    /// `terminated` event when the stop was caused by the disconnect itself.
    void disconnect();

    /// Resume execution after a stop (DAP `continue`). Cancels any pending
    /// step.
    void resume();

    /// Step commands (DAP `next` / `stepIn` / `stepOut`): arm the stepping
    /// state machine from the current paused position and resume execution.
    /// The session pauses again at the first debug hook satisfying the step
    /// kind, emitting a `stopped` event with reason "step" (RFC 0015 Slice 4).
    void step_over();
    void step_in();
    void step_out();

    [[nodiscard]] bool is_running() const noexcept;

    /// DAP `stackTrace`: serialize the current frame stack, top frame first,
    /// with stable 1-based frame ids.
    [[nodiscard]] std::string stack_trace_json() const;

    /// DAP `scopes`: the RFC 0015 scope scheme for the agent of the given
    /// frame. State frames and node frames expose "Agent State", "Context",
    /// "Input/Output" and "Workflow" scopes; the workflow frame exposes only
    /// the "Workflow" scope. variablesReference ids are deterministic:
    /// 100+agent / 200+agent / 300+agent / 400 (RFC 0015 variable scopes).
    [[nodiscard]] std::string scopes_json(int frame_id) const;

    /// DAP `variables`: expand the value registered under
    /// `variables_reference`. Scope references (100-499) expand to the
    /// scope's variables; chained references (>= 1000) expand a structured
    /// value's children (struct fields, enum payloads, list items).
    [[nodiscard]] std::string variables_json(int variables_reference);

    /// DAP `evaluate`: evaluate `expression` in the paused context of the
    /// given frame (RFC 0015 Slice 6). The debugger holds live runtime values
    /// (agent input/output, node results, workflow input) but no compiled
    /// `ir::Expr`, so the feasible evaluation surface is an identifier root
    /// followed by a chain of `.field` accesses, resolved against those live
    /// bindings using the runtime's own scope semantics: `input` / `output`
    /// name the frame agent's input / output value, an unqualified identifier
    /// names a workflow node result or an agent-input field, and each `.field`
    /// segment indexes into a struct field or a named enum payload. On success
    /// the result is serialized via `value_to_json`; a structured result gets a
    /// chained `variablesReference`. Malformed or unresolvable expressions
    /// return `{"error": "..."}`. Driving the full Evaluator would require a
    /// parse+resolve+typecheck+lower pipeline over a synthetic program, which
    /// the paused session cannot honestly reconstruct; this limitation is
    /// documented in the RFC Decision History.
    [[nodiscard]] std::string evaluate_json(const std::string &expression, int frame_id);

    /// WH-8 F5: classify newly-added line breakpoints through the descriptor
    /// and send unsolicited "breakpoint" events (deferred binding for late
    /// setBreakpoints requests). Called from the DAP server thread after
    /// updating the BreakpointManager. Reads the immutable descriptor; no
    /// session state is mutated beyond the event send. Caller must not hold
    /// mutex_.
    void report_breakpoint_liveness(
        const std::string &source_file,
        const std::vector<std::pair<int, int>> &id_lines);

  private:
    void execute(std::string workflow_name, ahfl::runtime::Value workflow_input);
    void on_state_entered(ahfl::runtime::AgentId agent,
                          std::string_view agent_name,
                          std::string_view node_name,
                          std::string_view state_name);
    // WH-8 (kr68 section 12.9.14): the LIVE debug channel. Fired at P6
    // import boundaries with the trace-ring prefix, before the capability
    // is dispatched. A blocking pause here pauses the run genuinely
    // inside the ImportCallback. Deduplicates against the post-run
    // state_entered_hook via live_count_ / post_count_.
    void on_state_entered_live(ahfl::runtime::AgentId agent,
                               std::string_view agent_name,
                               std::string_view node_name,
                               std::string_view state_name);
    void on_capability_invoked(ahfl::runtime::AgentId agent, std::string_view capability_name);
    /// Block the worker thread at a debug stop and emit a DAP `stopped`
    /// event. Sets paused_ under mutex_, sends the event, then waits on
    /// resume_cv_ until resume() clears paused_ or disconnect() sets
    /// stopping_. If stopping_ is already set, returns immediately without
    /// emitting an event (a disconnect in flight wins). Called only from the
    /// worker thread inside a debug hook. The caller must not hold mutex_.
    /// WH-8: a residual stopped-vs-disconnect ordering race (the stopped
    /// event may race with a concurrent disconnect's terminated event) is
    /// explicitly deferred by the coordinator; do not add behavior here.
    void pause(std::string reason, std::string description);
    /// Arm a step of `kind` from the last reported (agent, state) and resume
    /// the worker. Caller must not hold mutex_.
    void arm_step_and_resume(StepKind kind);
    [[nodiscard]] bool mark_terminated();
    void emit_terminated();
    void emit_output(std::string_view category, std::string_view text);

    /// Build the breakable source-line set from the compiled IR Program and
    /// register it with the BreakpointManager. Also builds the reverse
    /// state_name -> (file, line) and node_name -> (file, line) maps used to
    /// locate frames and check line breakpoints (RFC 0015 Slice 3), and the
    /// workflow declaration location for the root stack frame (Slice 5).
    void build_breakable_lines(const ahfl::SourceFile &source,
                               const std::string &source_path,
                               const std::string &workflow_name);

    /// WH-8 F4.3: one (agent_name, state_name) -> (file, line) record. Two
    /// agents in one file legitimately share state names (Init/Done); a
    /// single-key map would drop the second agent's line. A vector of
    /// records preserves all entries; lookups match on (agent, state) for
    /// frame synthesis and on (file, line) for classifier reverse lookup.
    /// F6.1: a single (file, line) may hold MULTIPLE records (the grammar
    /// separates stateHandlers by whitespace only), so the classifier
    /// gathers every record on a line before reaching a verdict.
    struct StateLineRecord {
        std::string agent_name;
        std::string state_name;
        std::string file;
        int line{0};
    };

    /// WH-8 (kr68 section 12.9.14.4): classify ONE state-handler record's
    /// liveness using the facade's CoreWasmExecutionDescriptor. Returns
    /// (verified, message) per the 7-row liveness table. F6.1: factored out
    /// of classify_line_liveness_locked so a shared physical line can
    /// classify every handler on it. Caller must hold mutex_.
    [[nodiscard]] std::pair<bool, std::string>
    classify_record_liveness_locked(const StateLineRecord &record) const;

    /// WH-8 (kr68 section 12.9.14.4): classify a line breakpoint's liveness
    /// using the facade's CoreWasmExecutionDescriptor. Returns (verified,
    /// message) per the liveness table; F6.1 adds an 8th row for a line
    /// shared by handlers whose per-record verdicts differ. Caller must
    /// hold mutex_.
    [[nodiscard]] std::pair<bool, std::string>
    classify_line_liveness_locked(const std::string &source_path, int line) const;

    /// WH-8 (kr68 section 12.9.14.4): deferred breakpoint binding. After the
    /// facade is constructed, iterate over all registered line breakpoints,
    /// classify each one via the descriptor, and send unsolicited "breakpoint"
    /// events with the refined verified/message. Caller must not hold mutex_.
    void classify_and_report_breakpoints();

    /// Push the root workflow frame. Called once at launch after the source
    /// locations are known. Caller must not hold mutex_.
    void push_workflow_frame(std::string workflow_name);

    /// Register a structured value for chained DAP expansion and return its
    /// variablesReference. Scalar / empty values return 0 (no children).
    /// Caller must hold mutex_.
    [[nodiscard]] int register_variable_locked(const ahfl::runtime::Value &value);

    /// Serialize the children of a structured value as a DAP `variables`
    /// response body. Structured children receive their own
    /// variablesReference for chained expansion. Caller must hold mutex_.
    [[nodiscard]] std::string expand_value_locked(const ahfl::runtime::Value &value);

    /// WH-8: resolve a workflow node's input value from the IR node argument
    /// expression. The wasm lane has no agent_input_hook; the DAP infers the
    /// input from the workflow declaration: a `node n: Agent(input)` passes the
    /// workflow input, a `node n: Agent(pred)` passes the predecessor's output.
    /// Returns nullptr when the expression is not a simple path read.
    /// Caller must hold mutex_.
    [[nodiscard]] const ahfl::runtime::Value *
    resolve_node_input_locked(std::string_view node_name) const;

    DapServer &server_;
    BreakpointManager &breakpoints_;

    std::optional<ahfl::ir::Program> program_;
#ifdef AHFL_ENABLE_BACKEND_WASM
    std::unique_ptr<ahfl::runtime::wasm_runner::WasmWorkflowRuntime> runtime_;
#endif
    std::thread worker_;

    /// Source locations that carry an IR declaration (state handlers, agent
    /// declarations, workflow nodes). Line breakpoints verify against this set.
    BreakableLineSet breakable_lines_;
    /// WH-8 F4.3: every (agent_name, state_name) -> (file, line) record.
    /// The record type is declared above with the classifier helpers.
    std::vector<StateLineRecord> state_line_records_;
    /// Reverse map from workflow node name to its source location, so node
    /// frames can be located (RFC 0015 Slice 5).
    std::unordered_map<std::string, std::pair<std::string, int>> node_line_map_;
    /// Source location of the launched workflow declaration, for the root
    /// stack frame.
    std::pair<std::string, int> workflow_location_;

    mutable std::mutex mutex_;
    std::condition_variable resume_cv_;
    bool paused_ = false;
    bool stopping_ = false;
    bool terminated_emitted_ = false;

    /// Stepping state machine (RFC 0015 Slice 4). Guarded by mutex_.
    DebugStepper stepper_;
    /// Last (agent, state) reported by on_state_entered; the origin for step
    /// requests armed while paused. Guarded by mutex_.
    std::string last_agent_id_;
    std::string last_state_name_;

    /// Frame stack, bottom to top: [workflow, node?, state?, capability?].
    /// Guarded by mutex_.
    std::vector<DebugFrame> frames_;
    /// WH-8 (kr68 section 12.9.14): per-(node, state) live-firing counts for
    /// dedup. The live hook fires at import boundaries; the post-run hook
    /// fires the full schedule-ordered sequence. The post-run hook skips
    /// occurrences already fired live (post_count <= live_count). Guarded
    /// by mutex_.
    std::unordered_map<std::string, std::size_t> live_count_;
    std::unordered_map<std::string, std::size_t> post_count_;
    /// Live agent outputs observed via node_completed_hook, keyed by agent
    /// debug id. Guarded by mutex_.
    std::unordered_map<std::string, ahfl::runtime::Value> agent_outputs_;
    /// WH-8: the current workflow node for each agent debug id, updated on
    /// every state_entered hook. Used to resolve the agent's input value from
    /// the workflow's node argument expression. Guarded by mutex_.
    std::unordered_map<std::string, std::string> agent_node_;
    /// Live node results observed via node_completed_hook, keyed by node
    /// name, for the Workflow scope. Guarded by mutex_.
    std::unordered_map<std::string, ahfl::runtime::Value> node_results_;
    /// Workflow input (from the launch config) and output (set after run()
    /// returns). Guarded by mutex_.
    ahfl::runtime::Value workflow_input_;
    std::optional<ahfl::runtime::Value> workflow_output_;
    /// WH-8: the launched workflow's name, used to resolve node inputs from
    /// the IR workflow declaration. Guarded by mutex_.
    std::string workflow_name_;
    /// Registry for chained variable expansion: variablesReference -> cloned
    /// Value. Scope references (100-499) are computed, not stored. Guarded by
    /// mutex_.
    std::unordered_map<int, ahfl::runtime::Value> variable_store_;
    int next_var_ref_ = 1000;
};

} // namespace ahfl::dap
