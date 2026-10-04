#include "tooling/dap/debug_session.hpp"

#include <algorithm>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "base/json/json_value.hpp"
#include "runtime/value/value.hpp"
#include "runtime/value/value_json.hpp"
#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/dap_server.hpp"

namespace ahfl::dap {

namespace {

#ifdef AHFL_ENABLE_BACKEND_WASM
struct CompileResult {
    std::optional<ir::Program> program;
    // The parsed source file, retained so IR SourceRange offsets can be mapped
    // back to 1-based source lines for breakpoint verification (RFC 0015 Slice 3).
    std::optional<SourceFile> source;
    std::string error;
};
#endif

[[nodiscard]] std::string json_escape(std::string_view value) {
    return ahfl::json::serialize_json(
        *ahfl::json::JsonValue::make_string(std::string(value)));
}

#ifdef AHFL_ENABLE_BACKEND_WASM
[[nodiscard]] std::string json_string_field(const json::JsonValue &object, std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return {};
    }
    const auto value = field->as_string();
    return value ? std::string(*value) : std::string{};
}

// Compile a single .ahfl source file through the full frontend pipeline.
// Mirrors the pipeline used by the CLI and the runtime e2e tests; a shared
// helper would be a worthwhile follow-up refactor.
[[nodiscard]] CompileResult compile_source_file(const std::string &path) {
    CompileResult result;

    const Frontend frontend;
    const auto parse_result = frontend.parse_file(path);
    if (parse_result.has_errors() || !parse_result.program) {
        std::ostringstream out;
        parse_result.diagnostics.render(out);
        result.error = "failed to parse " + path + ":\n" + out.str();
        return result;
    }
    result.source = parse_result.source;

    const Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        std::ostringstream out;
        resolve_result.diagnostics.render(out);
        result.error = "failed to resolve " + path + ":\n" + out.str();
        return result;
    }

    const TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        std::ostringstream out;
        type_check_result.diagnostics.render(out);
        result.error = "type errors in " + path + ":\n" + out.str();
        return result;
    }

    const Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        std::ostringstream out;
        validation_result.diagnostics.render(out);
        result.error = "validation errors in " + path + ":\n" + out.str();
        return result;
    }

    result.program = lower_program_ir(*parse_result.program, resolve_result, type_check_result);
    return result;
}
#endif // AHFL_ENABLE_BACKEND_WASM

[[nodiscard]] std::string agent_debug_id(const runtime::AgentId agent) {
    return std::to_string(agent.index() + 1);
}

// Human-readable type name for a runtime value, used as the DAP variable
// `type` field.
[[nodiscard]] std::string value_type_name(const runtime::Value &v) {
    return std::visit(
        Overloaded{
            [](const runtime::StructValue &sv) -> std::string { return sv.type_name; },
            [](const runtime::EnumValue &ev) -> std::string { return ev.enum_name; },
            [](const runtime::ListValue &) -> std::string { return "List"; },
            [](const runtime::SetValue &) -> std::string { return "Set"; },
            [](const runtime::MapValue &) -> std::string { return "Map"; },
            [](const runtime::NoneValue &) -> std::string { return "None"; },
            [](const runtime::BoolValue &) -> std::string { return "Bool"; },
            [](const runtime::IntValue &) -> std::string { return "Int"; },
            [](const runtime::FloatValue &) -> std::string { return "Float"; },
            [](const runtime::StringValue &) -> std::string { return "String"; },
            [](const runtime::DecimalValue &) -> std::string { return "Decimal"; },
            [](const runtime::DurationValue &) -> std::string { return "Duration"; },
            [](const runtime::UuidValue &) -> std::string { return "Uuid"; },
            [](const runtime::TimestampValue &) -> std::string { return "Timestamp"; },
            [](const runtime::UnitValue &) -> std::string { return "Unit"; },
        },
        v.node);
}

// Whether a value has children that can be expanded via a chained
// variables request.
[[nodiscard]] bool is_expandable(const runtime::Value &v) {
    return std::visit(
        Overloaded{
            [](const runtime::StructValue &sv) { return !sv.fields.empty(); },
            [](const runtime::EnumValue &ev) {
                return !ev.payload.empty() || !ev.named_payload.empty();
            },
            [](const runtime::ListValue &lv) { return !lv.items.empty(); },
            [](const runtime::SetValue &sv) { return !sv.items.empty(); },
            [](const runtime::MapValue &mv) { return !mv.entries.empty(); },
            [](const auto &) { return false; },
        },
        v.node);
}

} // namespace

DebugSession::DebugSession(DapServer &server, BreakpointManager &breakpoints)
    : server_(server), breakpoints_(breakpoints) {}

DebugSession::~DebugSession() {
    disconnect();
}

std::string DebugSession::launch(const std::string &config_json) {
#ifndef AHFL_ENABLE_BACKEND_WASM
    // WH-8 (kr68 section 12.9.9): the DAP requires the wasm backend. When
    // the backend is compiled out, refuse to launch with a byte-pinned
    // error message.
    (void)config_json;
    emit_output("stderr", "debug adapter requires WASM backend support");
    if (mark_terminated()) {
        emit_terminated();
    }
    return "{}";
#else
    std::string program_path;
    std::string workflow_name;
    std::optional<runtime::Value> workflow_input;
    if (const auto parsed = json::parse_json(config_json);
        parsed.has_value() && *parsed && (*parsed)->is_object()) {
        program_path = json_string_field(**parsed, "program");
        workflow_name = json_string_field(**parsed, "workflow");
        if (const auto *input_field = (*parsed)->get("input"); input_field != nullptr) {
            const std::string input_json = ahfl::json::serialize_json(*input_field);
            workflow_input = runtime::value_from_json(input_json);
            if (!workflow_input.has_value()) {
                emit_output("stderr", "launch config \"input\" is not a valid AHFL value");
            }
        }
    }

    if (program_path.empty()) {
        emit_output("stderr", "launch config requires a \"program\" field");
        if (mark_terminated()) {
            emit_terminated();
        }
        return "{}";
    }

    auto compiled = compile_source_file(program_path);
    if (!compiled.error.empty()) {
        emit_output("stderr", compiled.error);
        if (mark_terminated()) {
            emit_terminated();
        }
        return "{}";
    }
    program_ = std::move(*compiled.program);

    // Default to the first workflow declaration when the launch config omits
    // "workflow" (the VS Code contribution documents this as "debug the
    // file's entry workflow").
    if (workflow_name.empty()) {
        for (const auto &decl : program_->declarations) {
            if (const auto *wf = std::get_if<ir::WorkflowDecl>(&decl)) {
                workflow_name = wf->name;
                break;
            }
        }
    }
    if (workflow_name.empty()) {
        emit_output("stderr", "no workflow found in the compiled program");
        if (mark_terminated()) {
            emit_terminated();
        }
        return "{}";
    }

    build_breakable_lines(*compiled.source, program_path, workflow_name);

    {
        std::lock_guard lock(mutex_);
        if (workflow_input.has_value()) {
            workflow_input_ = std::move(*workflow_input);
        } else {
            workflow_input_ = runtime::make_none();
        }
        workflow_name_ = workflow_name;
    }
    push_workflow_frame(workflow_name);

    runtime::wasm_runner::WasmWorkflowRuntimeConfig config;
    config.hooks.state_entered_hook = [this](const runtime::AgentId agent,
                                              const std::string_view agent_name,
                                              const std::string_view node_name,
                                              const std::string_view state_name) {
        on_state_entered(agent, agent_name, node_name, state_name);
    };
    // WH-8 (kr68 section 12.9.14): the LIVE debug channel. Fires at P6
    // import boundaries before the capability is dispatched.
    config.hooks.state_entered_live_hook = [this](const runtime::AgentId agent,
                                                   const std::string_view agent_name,
                                                   const std::string_view node_name,
                                                   const std::string_view state_name) {
        on_state_entered_live(agent, agent_name, node_name, state_name);
    };
    config.hooks.capability_invoked_hook = [this](const runtime::AgentId agent,
                                                   const std::string_view capability_name) {
        on_capability_invoked(agent, capability_name);
    };
    config.hooks.node_completed_hook = [this](const runtime::AgentId agent,
                                               const std::string_view node_name,
                                               const runtime::Value &output) {
        const auto agent_id = agent_debug_id(agent);
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        agent_outputs_[agent_id] = runtime::clone_value(output);
        node_results_.emplace(std::string(node_name), runtime::clone_value(output));
    };
    // Capability output / failures -> DAP `output` events (RFC 0015 Slice 7).
    // Fires on the workflow thread right after the invoker returns, for every
    // capability call. A successful call streams its result value (serialized
    // via value_to_json) on the "stdout" category; a failed call reports its
    // error message on "stderr" so the runtime's own failure surfaces in the
    // debug console instead of being swallowed.
    config.hooks.capability_result_observer =
        [this](const runtime::CapabilityInvocationContext &,
               const runtime::CapabilityCallResult &result) {
            {
                std::lock_guard lock(mutex_);
                if (stopping_) {
                    return;
                }
            }
            if (result.status == runtime::CapabilityCallStatus::Success &&
                result.value.has_value()) {
                emit_output("stdout", runtime::value_to_json(*result.value) + "\n");
                return;
            }
            if (result.status != runtime::CapabilityCallStatus::Success) {
                std::string message = "capability call failed";
                if (!result.error_message.empty()) {
                    message += ": " + result.error_message;
                }
                emit_output("stderr", message + "\n");
            }
        };
    // The debug session owns no capability providers, but the runtime only
    // creates its capability dispatch path (and thus only fires
    // capability_invoked_hook) when an invoker is configured. Install a stub
    // that succeeds by echoing the first argument (the same pattern as the
    // wasm runner tests); the hook still pauses before the stub runs.
    config.invoker =
        [](const runtime::CapabilityInvocationContext &,
           const std::string &,
           const std::vector<runtime::Value> &args) -> runtime::CapabilityCallResult {
        runtime::CapabilityCallResult result;
        result.status = runtime::CapabilityCallStatus::Success;
        if (!args.empty()) {
            result.value = runtime::clone_value(args[0]);
        } else {
            result.value = runtime::make_none();
        }
        return result;
    };
    config.cancellation_requested = [this] {
        std::lock_guard lock(mutex_);
        return stopping_;
    };
    runtime_ = std::make_unique<runtime::wasm_runner::WasmWorkflowRuntime>(*program_, std::move(config));

    // WH-8 (kr68 section 12.9.14.4): deferred breakpoint binding. Now that
    // the facade (and its descriptor) exist, classify every registered line
    // breakpoint and send unsolicited "breakpoint" events with the refined
    // verified/message.
    classify_and_report_breakpoints();

    // Emit `initialized` before the worker starts so the client never sees a
    // `stopped` or `terminated` event before it (RFC 0015 event table).
    server_.send_event("initialized", json::JsonValue::make_object());

    worker_ = std::thread([this,
                           workflow_name = std::move(workflow_name),
                           input = runtime::clone_value(workflow_input_)]() mutable {
        execute(std::move(workflow_name), std::move(input));
    });
    return "{}";
#endif // AHFL_ENABLE_BACKEND_WASM
}

void DebugSession::disconnect() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        paused_ = false;
    }
    resume_cv_.notify_all();

    if (worker_.joinable()) {
        worker_.join();
    }

#ifdef AHFL_ENABLE_BACKEND_WASM
    runtime_.reset();
#endif
    program_.reset();

    if (mark_terminated()) {
        emit_terminated();
    }
}

void DebugSession::resume() {
    {
        std::lock_guard lock(mutex_);
        if (!stopping_) {
            paused_ = false;
            // `continue` cancels any step that was armed before it completes.
            stepper_.pending = StepKind::None;
        }
    }
    resume_cv_.notify_all();
}

void DebugSession::step_over() {
    arm_step_and_resume(StepKind::Over);
}

void DebugSession::step_in() {
    // Capability step-into is a future enhancement (RFC 0015 Open Question 2:
    // capabilities are external calls); until then stepIn behaves like next.
    arm_step_and_resume(StepKind::Into);
}

void DebugSession::step_out() {
    arm_step_and_resume(StepKind::Out);
}

void DebugSession::arm_step_and_resume(const StepKind kind) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        stepper_.pending = kind;
        stepper_.step_over_agent = last_agent_id_;
        stepper_.step_over_state = last_state_name_;
        stepper_.step_out_depth = 0; // depth tracking arrives with sub-workflow support
        paused_ = false;
    }
    resume_cv_.notify_all();
}

bool DebugSession::is_running() const noexcept {
    return worker_.joinable();
}

void DebugSession::execute(std::string workflow_name, runtime::Value workflow_input) {
#ifdef AHFL_ENABLE_BACKEND_WASM
    auto result = runtime_->run(workflow_name, std::move(workflow_input));

    {
        std::lock_guard lock(mutex_);
        if (const auto *output = result.output(); output != nullptr) {
            workflow_output_ = runtime::clone_value(*output);
        }
    }

    // Runtime failure -> DAP `output` event on "stderr" (RFC 0015 Slice 7).
    // A workflow that fails (node failure, dependency failure, evaluation
    // error) carries the failure diagnostics; surface them on the debug
    // console rather than dropping them silently. Emitted before `terminated`
    // so the client sees the cause before the session ends.
    {
        const bool stopping = [this] {
            std::lock_guard lock(mutex_);
            return stopping_;
        }();
        if (!stopping && result.has_errors()) {
            std::ostringstream out;
            result.diagnostics.render(out);
            std::string rendered = out.str();
            if (rendered.empty()) {
                rendered = "workflow \"" + workflow_name + "\" failed";
            }
            emit_output("stderr", rendered);
        }
    }

    if (mark_terminated()) {
        emit_terminated();
    }
#else
    (void)workflow_name;
    (void)workflow_input;
#endif
}

void DebugSession::build_breakable_lines(const SourceFile &source,
                                         const std::string &source_path,
                                         const std::string &workflow_name) {
    breakable_lines_.clear();
    state_line_records_.clear();
    node_line_map_.clear();
    workflow_location_ = {};

    const auto line_of = [&source](const SourceRange &range) {
        return static_cast<int>(source.locate(range.begin_offset).line);
    };

    // WH-8 (kr68 section 12.9.14.4): breakable lines are STATE HANDLER
    // lines only. The 7-row liveness table classifies non-state-handler
    // lines (agent declarations, workflow declarations, node declarations)
    // as NOT BREAKABLE. The node_line_map_ and workflow_location_ are
    // still built for frame location, but their lines are not registered
    // as breakable.
    for (const auto &decl : program_->declarations) {
        std::visit(
            Overloaded{
                [&](const ir::FlowDecl &flow) {
                    for (const auto &handler : flow.state_handlers) {
                        if (!handler.source_range.has_value()) {
                            continue;
                        }
                        const int line = line_of(*handler.source_range);
                        breakable_lines_.emplace(source_path, line);
                        // F4.3: thread the enclosing agent's canonical name
                        // so two agents sharing a state name keep distinct
                        // (agent, state) -> (file, line) records.
                        state_line_records_.push_back(
                            {flow.target_ref.canonical_name,
                             handler.state_name, source_path, line});
                    }
                },
                [&](const ir::WorkflowDecl &workflow) {
                    if (workflow.provenance.source_range.has_value()) {
                        const int line = line_of(*workflow.provenance.source_range);
                        // Only the launched workflow anchors the root stack frame.
                        if (workflow_name ==
                            std::string(ir::symbol_canonical_name(workflow.symbol_ref,
                                                                  workflow.name))) {
                            workflow_location_ = std::make_pair(source_path, line);
                        }
                    }
                    for (const auto &node : workflow.nodes) {
                        if (node.source_range.has_value()) {
                            const int line = line_of(*node.source_range);
                            node_line_map_.emplace(node.name, std::make_pair(source_path, line));
                        }
                    }
                },
                [](const auto &) { /* declarations without a debuggable location */ },
            },
            decl);
    }

    breakpoints_.set_breakable_lines(breakable_lines_);
}

std::pair<bool, std::string>
DebugSession::classify_record_liveness_locked(const StateLineRecord &record) const {
    // The descriptor is WASM-only; on the OFF lane `record` is unconsulted
    // and the function falls through to the "not found" verdict below.
    (void)record;
#ifdef AHFL_ENABLE_BACKEND_WASM
    const std::string &state_name = record.state_name;
    const std::string &agent_name = record.agent_name;
    if (!runtime_) {
        return {false, "breakpoint set before launch; deferred binding pending"};
    }
    const auto *descriptor = runtime_->descriptor_for(workflow_name_);
    if (descriptor == nullptr) {
        return {false, "workflow descriptor unavailable"};
    }

    // WireJson lane: all state hooks are post-run.
    if (descriptor->frame_contract !=
        ahfl::backends::CoreWasmFrameContract::P6Frame) {
        return {false,
                "state hooks are post-run on the WireJson lane (no trace "
                "ring); pauses after run completes (post-mortem)"};
    }

    // P6 lane: find the agent whose walk contains this state. F4.3: filter
    // by the record's agent_name so two agents sharing a state name don't
    // cross-match (the old code matched the first agent with the state).
    for (const auto &agent_walk : descriptor->agents) {
        if (agent_walk.agent != agent_name) {
            continue;
        }
        // Check if the state is on this agent's walk.
        std::optional<std::uint32_t> walk_index;
        for (std::uint32_t i = 0; i < agent_walk.walk.size(); ++i) {
            if (agent_walk.walk[i] == state_name) {
                walk_index = i;
                break;
            }
        }
        if (!walk_index.has_value()) {
            // State not on this agent's walk — check all_states for the
            // NEVER classification (untaken branch).
            bool in_all_states = false;
            for (const auto &s : agent_walk.all_states) {
                if (s == state_name) {
                    in_all_states = true;
                    break;
                }
            }
            if (in_all_states) {
                return {false,
                        "state is not on the runner's walk (untaken branch); "
                        "never paused"};
            }
            break; // agent found but state not on walk or all_states
        }

        // State is on this agent's walk. Check if any node with this runner
        // has capability.
        bool agent_has_cap = false;
        for (const auto &node : descriptor->nodes) {
            if (node.runner < descriptor->agents.size() &&
                &descriptor->agents[node.runner] == &agent_walk &&
                node.has_capability) {
                agent_has_cap = true;
                break;
            }
        }

        if (!agent_has_cap) {
            return {false,
                    "state has no capability boundary in its runner; pauses "
                    "after run completes (post-mortem)"};
        }

        if (*walk_index <= agent_walk.last_cap_walk_index) {
            return {true,
                    "state entered before capability boundary (live pause)"};
        }
        return {false,
                "state is entered after the last capability boundary; pauses "
                "after run completes (post-mortem)"};
    }
#endif // AHFL_ENABLE_BACKEND_WASM

    return {false, "state handler not found in workflow descriptor"};
}

std::pair<bool, std::string>
DebugSession::classify_line_liveness_locked(const std::string &source_path,
                                            int line) const {
    // F6.1: gather ALL (agent, state) records on this (file, line). The
    // grammar separates stateHandlers by whitespace only, so two handlers
    // (same or different agents) can legally share ONE physical line; a
    // single DAP breakpoint on that line must report an honest verdict
    // instead of attributing one arbitrary handler's liveness.
    std::vector<const StateLineRecord *> matched_records;
    for (const auto &rec : state_line_records_) {
        if (rec.file == source_path && rec.line == line) {
            matched_records.push_back(&rec);
        }
    }
    if (matched_records.empty()) {
        return {false,
                "line is not a state handler; only state handlers and "
                "capability calls are breakable"};
    }

    // Classify every handler on the line. Unanimous verdicts collapse to
    // the single (verified, message); differing verdicts get the shared-
    // line message so the user splits the handlers onto separate lines to
    // bind a specific one.
    auto verdict = classify_record_liveness_locked(*matched_records.front());
    for (std::size_t i = 1; i < matched_records.size(); ++i) {
        const auto other = classify_record_liveness_locked(*matched_records[i]);
        if (other != verdict) {
            return {false,
                    "line is shared by multiple state handlers with differing "
                    "pause behavior; put each handler on its own line to bind "
                    "a specific one"};
        }
    }
    return verdict;
}

void DebugSession::classify_and_report_breakpoints() {
    // Snapshot the breakpoints to classify (avoid holding the manager's
    // lock while sending events).
    struct PendingBp {
        int id;
        std::string source_file;
        int line;
    };
    std::vector<PendingBp> pending;
    {
        // The BreakpointManager is not internally synchronized; the DAP
        // server is single-threaded for request handling, and the worker
        // thread only reads breakpoints via check_* calls. Snapshot without
        // the session mutex to avoid deadlock with the worker.
        for (const auto &bp : breakpoints_.all_breakpoints()) {
            if (bp.kind == BreakpointKind::Line && bp.enabled) {
                pending.push_back({bp.id, bp.source_file, bp.line});
            }
        }
    }

    for (const auto &pbp : pending) {
        bool verified = false;
        std::string message;
        {
            std::lock_guard lock(mutex_);
            auto result = classify_line_liveness_locked(pbp.source_file, pbp.line);
            verified = result.first;
            message = std::move(result.second);
        }
        // Send an unsolicited "breakpoint" event with the refined
        // verified/message (DAP deferred binding pattern).
        auto body = ahfl::json::JsonValue::make_object();
        body->set("id", ahfl::json::JsonValue::make_int(pbp.id));
        body->set("verified",
                  ahfl::json::JsonValue::make_bool(verified));
        body->set("line", ahfl::json::JsonValue::make_int(pbp.line));
        body->set("message",
                  ahfl::json::JsonValue::make_string(std::move(message)));
        server_.send_event("breakpoint", std::move(body));
    }
}

void DebugSession::report_breakpoint_liveness(
    const std::string &source_file,
    const std::vector<std::pair<int, int>> &id_lines) {
    for (const auto &[id, line] : id_lines) {
        bool verified = false;
        std::string message;
        {
            std::lock_guard lock(mutex_);
            auto result = classify_line_liveness_locked(source_file, line);
            verified = result.first;
            message = std::move(result.second);
        }
        // Send an unsolicited "breakpoint" event with the refined
        // verified/message (DAP deferred binding pattern, same as the
        // launch-time classify_and_report_breakpoints).
        auto body = ahfl::json::JsonValue::make_object();
        body->set("id", ahfl::json::JsonValue::make_int(id));
        body->set("verified",
                  ahfl::json::JsonValue::make_bool(verified));
        body->set("line", ahfl::json::JsonValue::make_int(line));
        body->set("message",
                  ahfl::json::JsonValue::make_string(std::move(message)));
        server_.send_event("breakpoint", std::move(body));
    }
}

void DebugSession::push_workflow_frame(std::string workflow_name) {
    std::lock_guard lock(mutex_);
    DebugFrame frame;
    frame.kind = DebugFrame::Kind::Workflow;
    frame.name = std::move(workflow_name);
    frame.source_file = workflow_location_.first;
    frame.line = workflow_location_.second;
    frames_.push_back(std::move(frame));
}

void DebugSession::on_state_entered(const runtime::AgentId agent,
                                    const std::string_view agent_name,
                                    const std::string_view node_name,
                                    const std::string_view state_name) {
    // WH-8 (kr68 section 12.9.14): dedup against the live channel. The
    // live hook fires at import boundaries; the post-run hook fires the
    // full schedule-ordered sequence. Skip the breakpoint check for
    // occurrences already fired live (post_count <= live_count). The
    // frame stack is still updated so the DAP shows the correct state.
    const std::string dedup_key =
        std::string(node_name) + '\x1f' + std::string(state_name);
    bool already_live = false;
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        auto &post = post_count_[dedup_key];
        ++post;
        if (const auto live_it = live_count_.find(dedup_key);
            live_it != live_count_.end() && post <= live_it->second) {
            already_live = true;
        }
    }

    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        // WH-8: synthesize a Node frame on node change (the wasm lane
        // does not fire agent_input_hook for workflow nodes).
        bool need_node_frame = true;
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->kind == DebugFrame::Kind::Workflow) {
                break;
            }
            if (it->kind == DebugFrame::Kind::Node) {
                need_node_frame = (it->node_name != node_name);
                break;
            }
        }
        if (need_node_frame) {
            while (!frames_.empty() &&
                   frames_.back().kind != DebugFrame::Kind::Workflow) {
                frames_.pop_back();
            }
            DebugFrame node_frame;
            node_frame.kind = DebugFrame::Kind::Node;
            node_frame.name = std::string(node_name);
            node_frame.agent_id = agent_debug_id(agent);
            node_frame.agent_name = std::string(agent_name);
            node_frame.node_name = std::string(node_name);
            if (const auto it = node_line_map_.find(node_frame.node_name);
                it != node_line_map_.end()) {
                node_frame.source_file = it->second.first;
                node_frame.line = it->second.second;
            }
            frames_.push_back(std::move(node_frame));
        }
        // Pop capability frames: the previous capability completed before the
        // state was entered.
        while (!frames_.empty() &&
               frames_.back().kind == DebugFrame::Kind::Capability) {
            frames_.pop_back();
        }
        // A state entry replaces the top state frame. Same agent: a transition.
        // Different agent: the previous agent completed (agents run
        // sequentially, one node at a time).
        if (!frames_.empty() && frames_.back().kind == DebugFrame::Kind::State) {
            frames_.pop_back();
        }
        DebugFrame frame;
        frame.kind = DebugFrame::Kind::State;
        frame.name = std::string(agent_name) + "::" + std::string(state_name);
        frame.agent_id = agent_debug_id(agent);
        frame.agent_name = std::string(agent_name);
        frame.node_name = std::string(node_name);
        frame.state_name = std::string(state_name);
        // F4.3: resolve the state line with BOTH agent_name and state_name
        // (two agents can share state names; the agent disambiguates).
        if (const auto rec_it = std::find_if(
                state_line_records_.begin(), state_line_records_.end(),
                [&](const StateLineRecord &rec) {
                    return rec.agent_name == agent_name &&
                           rec.state_name == state_name;
                });
            rec_it != state_line_records_.end()) {
            frame.source_file = rec_it->file;
            frame.line = rec_it->line;
        }
        frames_.push_back(std::move(frame));
        // Record the current position before any pause so step requests armed
        // while paused at a breakpoint use it as their origin.
        last_agent_id_ = frames_.back().agent_id;
        last_state_name_ = frames_.back().state_name;
        // WH-8: track the current node per agent so the Input/Output scope
        // and evaluate can resolve the agent's input value.
        agent_node_[frames_.back().agent_id] = std::string(node_name);
    }

    // Already fired live: the live channel already paused (or will pause)
    // for this occurrence. Skip the breakpoint check.
    if (already_live) {
        return;
    }

    const auto agent_id = agent_debug_id(agent);

    const auto state_hits =
        breakpoints_.check_state_breakpoints(agent_id, std::string(state_name));
    if (!state_hits.empty()) {
        std::string description = "state breakpoint: " + std::string(state_name);
        if (!state_hits.front().description.empty()) {
            description = state_hits.front().description;
        }
        // WH-8 (kr68 section 12.9.14.4): the post-run hook fires after the
        // run completes; pauses here are POST-MORTEM (reason "pause").
        pause("pause", std::move(description));
        return;
    }

    // Line breakpoints: map the entered state back to its handler's source
    // location and pause when a line breakpoint is set there (RFC 0015 Slice 3).
    // F4.3: resolve with BOTH agent_name and state_name.
    if (const auto rec_it = std::find_if(
            state_line_records_.begin(), state_line_records_.end(),
            [&](const StateLineRecord &rec) {
                return rec.agent_name == agent_name &&
                       rec.state_name == state_name;
            });
        rec_it != state_line_records_.end()) {
        const auto &file = rec_it->file;
        const auto line = rec_it->line;
        const auto line_hits = breakpoints_.check_line_breakpoints(file, line);
        if (!line_hits.empty()) {
            std::string description = "line breakpoint: " + file + ":" + std::to_string(line);
            if (!line_hits.front().description.empty()) {
                description = line_hits.front().description;
            }
            pause("pause", std::move(description));
            return;
        }
    }

    // Stepping (RFC 0015 Slice 4): a pending step completes at the first
    // state transition satisfying its kind. Checked after breakpoints so a
    // breakpoint at the same position wins and reports its own reason.
    bool step_pause = false;
    std::string step_description;
    {
        std::lock_guard lock(mutex_);
        if (stepper_.pending == StepKind::Over || stepper_.pending == StepKind::Into) {
            // Step over / step in (state-transition level; capability
            // step-into is a future enhancement): complete at the first state
            // transition away from the step origin.
            if (agent_id != stepper_.step_over_agent ||
                state_name != stepper_.step_over_state) {
                step_pause = true;
            }
        } else if (stepper_.pending == StepKind::Out) {
            // Workflow nesting depth tracking arrives with sub-workflow
            // support; for now step out completes at the next state
            // transition in a different agent.
            if (agent_id != stepper_.step_over_agent) {
                step_pause = true;
            }
        }
        if (step_pause) {
            stepper_.pending = StepKind::None;
            step_description = "step: " + std::string(state_name);
        }
    }
    if (step_pause) {
        pause("step", std::move(step_description));
    }
}

void DebugSession::on_state_entered_live(const runtime::AgentId agent,
                                          const std::string_view agent_name,
                                          const std::string_view node_name,
                                          const std::string_view state_name) {
    // WH-8 (kr68 section 12.9.14): the LIVE debug channel. Fires at P6
    // import boundaries before the capability is dispatched. A blocking
    // pause here pauses the run genuinely inside the ImportCallback.
    const std::string dedup_key =
        std::string(node_name) + '\x1f' + std::string(state_name);
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        ++live_count_[dedup_key];
    }

    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        // Synthesize a Node frame on node change (the wasm lane does not
        // fire agent_input_hook for workflow nodes).
        bool need_node_frame = true;
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->kind == DebugFrame::Kind::Workflow) {
                break;
            }
            if (it->kind == DebugFrame::Kind::Node) {
                need_node_frame = (it->node_name != node_name);
                break;
            }
        }
        if (need_node_frame) {
            while (!frames_.empty() &&
                   frames_.back().kind != DebugFrame::Kind::Workflow) {
                frames_.pop_back();
            }
            DebugFrame node_frame;
            node_frame.kind = DebugFrame::Kind::Node;
            node_frame.name = std::string(node_name);
            node_frame.agent_id = agent_debug_id(agent);
            node_frame.agent_name = std::string(agent_name);
            node_frame.node_name = std::string(node_name);
            if (const auto it = node_line_map_.find(node_frame.node_name);
                it != node_line_map_.end()) {
                node_frame.source_file = it->second.first;
                node_frame.line = it->second.second;
            }
            frames_.push_back(std::move(node_frame));
        }
        while (!frames_.empty() &&
               frames_.back().kind == DebugFrame::Kind::Capability) {
            frames_.pop_back();
        }
        if (!frames_.empty() && frames_.back().kind == DebugFrame::Kind::State) {
            frames_.pop_back();
        }
        DebugFrame frame;
        frame.kind = DebugFrame::Kind::State;
        frame.name = std::string(agent_name) + "::" + std::string(state_name);
        frame.agent_id = agent_debug_id(agent);
        frame.agent_name = std::string(agent_name);
        frame.node_name = std::string(node_name);
        frame.state_name = std::string(state_name);
        // F4.3: resolve the state line with BOTH agent_name and state_name.
        if (const auto rec_it = std::find_if(
                state_line_records_.begin(), state_line_records_.end(),
                [&](const StateLineRecord &rec) {
                    return rec.agent_name == agent_name &&
                           rec.state_name == state_name;
                });
            rec_it != state_line_records_.end()) {
            frame.source_file = rec_it->file;
            frame.line = rec_it->line;
        }
        frames_.push_back(std::move(frame));
        last_agent_id_ = frames_.back().agent_id;
        last_state_name_ = frames_.back().state_name;
        agent_node_[frames_.back().agent_id] = std::string(node_name);
    }

    const auto agent_id = agent_debug_id(agent);

    const auto state_hits =
        breakpoints_.check_state_breakpoints(agent_id, std::string(state_name));
    if (!state_hits.empty()) {
        std::string description = "state breakpoint: " + std::string(state_name);
        if (!state_hits.front().description.empty()) {
            description = state_hits.front().description;
        }
        pause("breakpoint", std::move(description));
        return;
    }

    // F4.3: resolve the state line with BOTH agent_name and state_name.
    if (const auto rec_it = std::find_if(
            state_line_records_.begin(), state_line_records_.end(),
            [&](const StateLineRecord &rec) {
                return rec.agent_name == agent_name &&
                       rec.state_name == state_name;
            });
        rec_it != state_line_records_.end()) {
        const auto &file = rec_it->file;
        const auto line = rec_it->line;
        const auto line_hits = breakpoints_.check_line_breakpoints(file, line);
        if (!line_hits.empty()) {
            std::string description = "line breakpoint: " + file + ":" + std::to_string(line);
            if (!line_hits.front().description.empty()) {
                description = line_hits.front().description;
            }
            pause("breakpoint", std::move(description));
            return;
        }
    }

    // Stepping: same logic as on_state_entered.
    bool step_pause = false;
    std::string step_description;
    {
        std::lock_guard lock(mutex_);
        if (stepper_.pending == StepKind::Over || stepper_.pending == StepKind::Into) {
            if (agent_id != stepper_.step_over_agent ||
                state_name != stepper_.step_over_state) {
                step_pause = true;
            }
        } else if (stepper_.pending == StepKind::Out) {
            if (agent_id != stepper_.step_over_agent) {
                step_pause = true;
            }
        }
        if (step_pause) {
            stepper_.pending = StepKind::None;
            step_description = "step: " + std::string(state_name);
        }
    }
    if (step_pause) {
        pause("step", std::move(step_description));
    }
}

void DebugSession::on_capability_invoked(const runtime::AgentId agent,
                                         const std::string_view capability_name) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        // Replace the top capability frame (the previous capability completed
        // before this one was invoked). Capability call sites are not exposed
        // by the runtime hook, so the frame carries no source location; the
        // state frame below it locates the call.
        while (!frames_.empty() && frames_.back().kind == DebugFrame::Kind::Capability) {
            frames_.pop_back();
        }
        DebugFrame frame;
        frame.kind = DebugFrame::Kind::Capability;
        frame.name = std::string(capability_name);
        frame.agent_id = agent_debug_id(agent);
        frames_.push_back(std::move(frame));
    }

    const auto agent_id = agent_debug_id(agent);
    const auto hits =
        breakpoints_.check_capability_breakpoints(agent_id, std::string(capability_name));
    if (!hits.empty()) {
        std::string description = "capability breakpoint: " + std::string(capability_name);
        if (!hits.front().description.empty()) {
            description = hits.front().description;
        }
        pause("breakpoint", std::move(description));
        return;
    }

    // Stepping (RFC 0015 Slice 4): a pending step completes at a capability
    // call from a different agent than the step origin. Capability calls
    // carry no state identity, and step-into capability internals is a
    // future enhancement (RFC 0015 Open Question 2).
    bool step_pause = false;
    std::string step_description;
    {
        std::lock_guard lock(mutex_);
        if (stepper_.pending != StepKind::None && agent_id != stepper_.step_over_agent) {
            step_pause = true;
            stepper_.pending = StepKind::None;
            step_description = "step: " + std::string(capability_name);
        }
    }
    if (step_pause) {
        pause("step", std::move(step_description));
    }
}

void DebugSession::pause(std::string reason, std::string description) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
        paused_ = true;
    }

    auto body = json::JsonValue::make_object();
    body->set("reason", json::JsonValue::make_string(std::move(reason)));
    body->set("threadId", json::JsonValue::make_int(1));
    body->set("allThreadsStopped", json::JsonValue::make_bool(true));
    if (!description.empty()) {
        body->set("description", json::JsonValue::make_string(std::move(description)));
    }
    server_.send_event("stopped", std::move(body));

    std::unique_lock lock(mutex_);
    resume_cv_.wait(lock, [this] { return !paused_ || stopping_; });
}

bool DebugSession::mark_terminated() {
    std::lock_guard lock(mutex_);
    if (terminated_emitted_) {
        return false;
    }
    terminated_emitted_ = true;
    return true;
}

void DebugSession::emit_terminated() {
    server_.send_event("terminated", json::JsonValue::make_object());
}

void DebugSession::emit_output(const std::string_view category, const std::string_view text) {
    auto body = json::JsonValue::make_object();
    body->set("category", json::JsonValue::make_string(std::string(category)));
    body->set("output", json::JsonValue::make_string(std::string(text)));
    server_.send_event("output", std::move(body));
}

// --- DAP request handlers (RFC 0015 Slice 5) ---

std::string DebugSession::stack_trace_json() const {
    std::lock_guard lock(mutex_);
    std::ostringstream oss;
    oss << R"({"stackFrames":[)";
    // DAP lists the top (most recent) frame first. frames_ is stored bottom to
    // top, so iterate in reverse and assign stable 1-based ids.
    int id = 1;
    bool first = true;
    for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
        const auto &frame = *it;
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << R"({"id":)" << id++ << R"(,"name":)" << json_escape(frame.name);
        if (!frame.source_file.empty()) {
            oss << R"(,"source":{"path":)" << json_escape(frame.source_file) << "}";
        }
        oss << R"(,"line":)" << frame.line << R"(,"column":1})";
    }
    oss << R"(],"totalFrames":)" << frames_.size() << "}";
    return oss.str();
}

std::string DebugSession::scopes_json(const int frame_id) const {
    std::lock_guard lock(mutex_);
    // Resolve the frame's agent. frames_ is bottom to top; frame_id is 1-based
    // from the top (matching stack_trace_json).
    std::string agent_id;
    if (frame_id >= 1 && static_cast<std::size_t>(frame_id) <= frames_.size()) {
        agent_id = frames_[frames_.size() - static_cast<std::size_t>(frame_id)].agent_id;
    }
    // Fall back to the top state frame's agent when the client sent an unknown
    // or stale frame id.
    if (agent_id.empty()) {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->kind == DebugFrame::Kind::State) {
                agent_id = it->agent_id;
                break;
            }
        }
    }

    std::ostringstream oss;
    oss << R"({"scopes":[)";
    bool first = true;
    auto append_scope = [&](const std::string &name, const int ref) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << R"({"name":)" << json_escape(name) << R"(,"variablesReference":)" << ref
            << R"(,"expensive":false})";
    };

    if (agent_id.empty()) {
        // Workflow frame (or no agent yet): only the Workflow scope applies.
        append_scope("Workflow", 400);
    } else {
        const int agent_index = std::stoi(agent_id);
        append_scope("Agent State", 100 + agent_index);
        append_scope("Context", 200 + agent_index);
        append_scope("Input/Output", 300 + agent_index);
        append_scope("Workflow", 400);
    }
    oss << "]}";
    return oss.str();
}

std::string DebugSession::variables_json(const int variables_reference) {
    std::lock_guard lock(mutex_);

    // Chained expansion of a registered structured value.
    if (variables_reference >= 1000) {
        const auto it = variable_store_.find(variables_reference);
        if (it != variable_store_.end()) {
            return expand_value_locked(it->second);
        }
        return R"({"variables":[]})";
    }

    std::ostringstream oss;
    oss << R"({"variables":[)";
    bool first = true;
    auto append_var = [&](const std::string &name, const runtime::Value *value) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << R"({"name":)" << json_escape(name) << R"(,"value":)"
            << (value != nullptr ? json_escape(runtime::value_to_json(*value))
                                 : json_escape("null"))
            << R"(,"type":)"
            << json_escape(value != nullptr ? value_type_name(*value) : std::string("None"))
            << R"(,"variablesReference":)"
            << (value != nullptr ? register_variable_locked(*value) : 0) << "}";
    };

    if (variables_reference >= 100 && variables_reference < 200) {
        // Agent State scope: the current state name for the agent. State
        // variables (handler locals) are mutated inside the evaluator and not
        // exposed by the runtime; only the state name is observable.
        const std::string agent_id = std::to_string(variables_reference - 100);
        for (const auto &frame : frames_) {
            if (frame.kind == DebugFrame::Kind::State && frame.agent_id == agent_id) {
                if (!first) {
                    oss << ",";
                }
                first = false;
                oss << R"({"name":)" << json_escape("state") << R"(,"value":)"
                    << json_escape(frame.state_name) << R"(,"type":)" << json_escape("String")
                    << R"(,"variablesReference":0})";
                break;
            }
        }
    } else if (variables_reference >= 200 && variables_reference < 300) {
        // Context scope: agent context fields are assigned inside state
        // handlers (evaluator ctx scope) and not exposed by any runtime hook,
        // so this scope is honestly empty.
    } else if (variables_reference >= 300 && variables_reference < 400) {
        // Input/Output scope: the agent's live input (resolved from the
        // workflow's node argument expression) and output (observed via
        // node_completed_hook). WH-8: the wasm lane has no agent_input_hook;
        // the DAP infers the input from the IR node argument.
        const std::string agent_id = std::to_string(variables_reference - 300);
        if (const auto node_it = agent_node_.find(agent_id);
            node_it != agent_node_.end()) {
            if (const auto *input = resolve_node_input_locked(node_it->second)) {
                append_var("input", input);
            }
        }
        if (const auto it = agent_outputs_.find(agent_id); it != agent_outputs_.end()) {
            append_var("output", &it->second);
        }
    } else if (variables_reference >= 400 && variables_reference < 500) {
        // Workflow scope: workflow input, node results, workflow output.
        append_var("input", &workflow_input_);
        std::vector<std::string> node_names;
        node_names.reserve(node_results_.size());
        for (const auto &[name, _] : node_results_) {
            node_names.push_back(name);
        }
        std::sort(node_names.begin(), node_names.end());
        for (const auto &name : node_names) {
            append_var(name, &node_results_.at(name));
        }
        if (workflow_output_.has_value()) {
            append_var("output", &*workflow_output_);
        }
    }

    oss << "]}";
    return oss.str();
}

namespace {

// Split an evaluate expression into a root identifier and a chain of `.field`
// segments. Returns nullopt when the expression is not a plain identifier
// path (empty, contains call syntax, operators, indexing, whitespace inside a
// segment, etc.) — the debugger only resolves live-value paths, not arbitrary
// expressions (RFC 0015 Slice 6).
[[nodiscard]] std::optional<std::vector<std::string>>
parse_value_path(const std::string &expression) {
    auto is_ident_start = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    auto is_ident_char = [&](char c) {
        return is_ident_start(c) || (c >= '0' && c <= '9');
    };

    // Trim surrounding whitespace; interior whitespace is rejected below.
    std::size_t begin = 0;
    std::size_t end = expression.size();
    while (begin < end && (expression[begin] == ' ' || expression[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (expression[end - 1] == ' ' || expression[end - 1] == '\t')) {
        --end;
    }
    if (begin == end) {
        return std::nullopt;
    }

    std::vector<std::string> segments;
    std::string current;
    bool expect_start = true;
    for (std::size_t i = begin; i < end; ++i) {
        const char c = expression[i];
        if (c == '.') {
            if (current.empty()) {
                return std::nullopt; // leading dot or empty segment
            }
            segments.push_back(std::move(current));
            current.clear();
            expect_start = true;
            continue;
        }
        if (expect_start) {
            if (!is_ident_start(c)) {
                return std::nullopt;
            }
            expect_start = false;
        } else if (!is_ident_char(c)) {
            return std::nullopt;
        }
        current.push_back(c);
    }
    if (current.empty()) {
        return std::nullopt; // trailing dot
    }
    segments.push_back(std::move(current));
    return segments;
}

// Resolve a single `.field` segment against a value: struct fields and named
// enum payloads are addressable by name. Returns nullptr when the value is not
// a keyed aggregate or has no such member.
[[nodiscard]] const runtime::Value *resolve_member(const runtime::Value &value,
                                                     const std::string &field) {
    if (const auto *sv = std::get_if<runtime::StructValue>(&value.node)) {
        if (const auto it = sv->fields.find(field); it != sv->fields.end()) {
            return it->value.get();
        }
        return nullptr;
    }
    if (const auto *ev = std::get_if<runtime::EnumValue>(&value.node)) {
        if (const auto it = ev->named_payload.find(field); it != ev->named_payload.end()) {
            return it->value.get();
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

std::string DebugSession::evaluate_json(const std::string &expression, const int frame_id) {
    const auto path = parse_value_path(expression);
    if (!path.has_value()) {
        return R"({"error":)" +
               json_escape("cannot evaluate \"" + expression +
                           "\": only identifier and field-access paths are supported") +
               "}";
    }

    std::lock_guard lock(mutex_);

    // Resolve the frame's agent (frame_id is 1-based from the top, matching
    // stack_trace_json / scopes_json).
    std::string agent_id;
    if (frame_id >= 1 && static_cast<std::size_t>(frame_id) <= frames_.size()) {
        agent_id = frames_[frames_.size() - static_cast<std::size_t>(frame_id)].agent_id;
    }
    if (agent_id.empty()) {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            if (it->kind == DebugFrame::Kind::State || it->kind == DebugFrame::Kind::Node) {
                agent_id = it->agent_id;
                break;
            }
        }
    }

    const std::string &root = path->front();

    // Resolve the root binding using the runtime's own scope semantics.
    // WH-8: the wasm lane has no agent_input_hook; the DAP infers the agent's
    // input from the IR node argument expression. "input" names the frame
    // agent's input; an unqualified identifier names a workflow node result
    // or (fallback) an agent-input field flattened into the input scope.
    const runtime::Value *current = nullptr;
    if (root == "input") {
        if (const auto node_it = agent_node_.find(agent_id);
            node_it != agent_node_.end()) {
            current = resolve_node_input_locked(node_it->second);
        }
    } else if (root == "output") {
        if (const auto it = agent_outputs_.find(agent_id); it != agent_outputs_.end()) {
            current = &it->second;
        }
    } else if (const auto node_it = node_results_.find(root); node_it != node_results_.end()) {
        // Workflow node result (mirrors EvalContext node-output scope).
        current = &node_it->second;
    } else {
        // Unqualified identifier: try resolving as a field of the agent's
        // input (the evaluator flattens input struct fields into the input
        // scope).
        const runtime::Value *agent_input = nullptr;
        if (const auto node_it = agent_node_.find(agent_id);
            node_it != agent_node_.end()) {
            agent_input = resolve_node_input_locked(node_it->second);
        }
        if (agent_input != nullptr) {
            current = resolve_member(*agent_input, root);
        }
    }

    if (current == nullptr) {
        return R"({"error":)" +
               json_escape("unknown identifier \"" + root + "\" in the paused context") + "}";
    }

    // Walk the remaining `.field` segments.
    for (std::size_t i = 1; i < path->size(); ++i) {
        const runtime::Value *next = resolve_member(*current, (*path)[i]);
        if (next == nullptr) {
            return R"({"error":)" +
                   json_escape("no field \"" + (*path)[i] + "\" on \"" +
                               value_type_name(*current) + "\"") +
                   "}";
        }
        current = next;
    }

    std::ostringstream oss;
    oss << R"({"result":)" << json_escape(runtime::value_to_json(*current)) << R"(,"type":)"
        << json_escape(value_type_name(*current)) << R"(,"variablesReference":)"
        << register_variable_locked(*current) << "}";
    return oss.str();
}

int DebugSession::register_variable_locked(const runtime::Value &value) {
    if (!is_expandable(value)) {
        return 0;
    }
    const int ref = next_var_ref_++;
    variable_store_.emplace(ref, runtime::clone_value(value));
    return ref;
}

std::string DebugSession::expand_value_locked(const runtime::Value &value) {
    std::ostringstream oss;
    oss << R"({"variables":[)";
    bool first = true;
    auto append_child = [&](const std::string &name, const runtime::Value *child) {
        if (!first) {
            oss << ",";
        }
        first = false;
        oss << R"({"name":)" << json_escape(name) << R"(,"value":)"
            << (child != nullptr ? json_escape(runtime::value_to_json(*child))
                                 : json_escape("null"))
            << R"(,"type":)"
            << json_escape(child != nullptr ? value_type_name(*child) : std::string("None"))
            << R"(,"variablesReference":)"
            << (child != nullptr ? register_variable_locked(*child) : 0) << "}";
    };

    std::visit(
        Overloaded{
            [&](const runtime::StructValue &sv) {
                // FieldMap iterates in name-sorted order already.
                for (const auto &[name, value] : sv.fields) {
                    append_child(name, value.get());
                }
            },
            [&](const runtime::EnumValue &ev) {
                // Variant marker (scalar, no children).
                if (!first) {
                    oss << ",";
                }
                first = false;
                oss << R"({"name":)" << json_escape("_variant") << R"(,"value":)"
                    << json_escape(ev.variant) << R"(,"type":)" << json_escape(ev.enum_name)
                    << R"(,"variablesReference":0})";
                for (std::size_t i = 0; i < ev.payload.size(); ++i) {
                    append_child("_" + std::to_string(i), ev.payload[i].get());
                }
                // FieldMap iterates in name-sorted order already.
                for (const auto &[name, value] : ev.named_payload) {
                    append_child(name, value.get());
                }
            },
            [&](const runtime::ListValue &lv) {
                for (std::size_t i = 0; i < lv.items.size(); ++i) {
                    append_child(std::to_string(i), lv.items[i].get());
                }
            },
            [&](const runtime::SetValue &sv) {
                for (std::size_t i = 0; i < sv.items.size(); ++i) {
                    append_child(std::to_string(i), sv.items[i].get());
                }
            },
            [&](const runtime::MapValue &mv) {
                for (std::size_t i = 0; i < mv.entries.size(); ++i) {
                    const auto &[key, val] = mv.entries[i];
                    const std::string key_name =
                        key != nullptr ? runtime::value_to_json(*key) : "null";
                    append_child(key_name, val.get());
                }
            },
            [](const auto &) { /* scalar: no children */ },
        },
        value.node);

    oss << "]}";
    return oss.str();
}

const runtime::Value *
DebugSession::resolve_node_input_locked(std::string_view node_name) const {
    if (!program_.has_value()) {
        return nullptr;
    }
    for (const auto &decl : program_->declarations) {
        const auto *wf = std::get_if<ir::WorkflowDecl>(&decl);
        if (wf == nullptr || wf->name != workflow_name_) {
            continue;
        }
        for (const auto &node : wf->nodes) {
            if (node.name != node_name || !node.input.has_value()) {
                continue;
            }
            const auto *path_expr =
                std::get_if<ir::PathExpr>(&node.input->node);
            if (path_expr == nullptr) {
                return nullptr;
            }
            const auto &root = path_expr->path.root_name;
            const runtime::Value *current = nullptr;
            if (root == "input") {
                current = &workflow_input_;
            } else if (const auto it = node_results_.find(root);
                       it != node_results_.end()) {
                current = &it->second;
            }
            if (current == nullptr) {
                return nullptr;
            }
            // F4.4: follow the .member chain via resolve_member (struct
            // fields and named enum payloads). Any miss returns nullptr.
            for (const auto &member : path_expr->path.members) {
                current = resolve_member(*current, member);
                if (current == nullptr) {
                    return nullptr;
                }
            }
            return current;
        }
    }
    return nullptr;
}

} // namespace ahfl::dap
