#include "tooling/dap/debug_session.hpp"

#include <sstream>
#include <string_view>
#include <utility>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/validate.hpp"
#include "base/json/json_value.hpp"
#include "runtime/evaluator/value.hpp"
#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/dap_server.hpp"
#include "tooling/dap/state_inspector.hpp"

namespace ahfl::dap {

namespace {

struct CompileResult {
    std::optional<ir::Program> program;
    std::string error;
};

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

[[nodiscard]] std::string agent_debug_id(const runtime::AgentId agent) {
    return std::to_string(agent.index() + 1);
}

} // namespace

DebugSession::DebugSession(DapServer &server,
                           BreakpointManager &breakpoints,
                           StateInspector &inspector)
    : server_(server), breakpoints_(breakpoints), inspector_(inspector) {}

DebugSession::~DebugSession() {
    disconnect();
}

std::string DebugSession::launch(const std::string &config_json) {
    std::string program_path;
    std::string workflow_name;
    if (const auto parsed = json::parse_json(config_json);
        parsed.has_value() && *parsed && (*parsed)->is_object()) {
        program_path = json_string_field(**parsed, "program");
        workflow_name = json_string_field(**parsed, "workflow");
    }

    if (program_path.empty() || workflow_name.empty()) {
        emit_output("stderr", "launch config requires \"program\" and \"workflow\" fields");
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

    runtime::WorkflowRuntimeConfig config;
    config.state_entered_hook = [this](const runtime::AgentId agent,
                                       const std::string_view state_name) {
        on_state_entered(agent, state_name);
    };
    config.capability_invoked_hook = [this](const runtime::AgentId agent,
                                            const std::string_view capability_name) {
        on_capability_invoked(agent, capability_name);
    };
    // The debug session owns no capability providers, but the runtime only
    // creates its capability dispatch path (and thus only fires
    // capability_invoked_hook) when an invoker is configured. Install a stub
    // that succeeds with a None value so workflows with capability calls can
    // be debugged end-to-end; the hook still pauses before the stub runs.
    config.contextual_capability_invoker =
        [](const runtime::CapabilityInvocationContext &,
           const std::string &,
           const std::vector<runtime::Value> &) -> runtime::CapabilityCallResult {
        runtime::CapabilityCallResult result;
        result.status = runtime::CapabilityCallStatus::Success;
        result.value = evaluator::make_none();
        return result;
    };
    config.cancellation_requested = [this] {
        std::lock_guard lock(mutex_);
        return stopping_;
    };
    runtime_ = std::make_unique<runtime::WorkflowRuntime>(*program_, std::move(config));

    // Emit `initialized` before the worker starts so the client never sees a
    // `stopped` or `terminated` event before it (RFC 0015 event table).
    server_.send_event("initialized", json::JsonValue::make_object());

    worker_ = std::thread([this, workflow_name = std::move(workflow_name)]() mutable {
        execute(std::move(workflow_name));
    });
    return "{}";
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

    runtime_.reset();
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
        }
    }
    resume_cv_.notify_all();
}

bool DebugSession::is_running() const noexcept {
    return worker_.joinable();
}

void DebugSession::execute(std::string workflow_name) {
    (void)runtime_->run(workflow_name, evaluator::make_none());

    if (mark_terminated()) {
        emit_terminated();
    }
}

void DebugSession::on_state_entered(const runtime::AgentId agent,
                                    const std::string_view state_name) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
    }

    const auto agent_id = agent_debug_id(agent);
    inspector_.set_agent_state(agent_id, std::string(state_name), {});

    const auto hits = breakpoints_.check_state_breakpoints(agent_id, std::string(state_name));
    if (hits.empty()) {
        return;
    }

    std::string description = "state breakpoint: " + std::string(state_name);
    if (!hits.front().description.empty()) {
        description = hits.front().description;
    }
    pause("breakpoint", std::move(description));
}

void DebugSession::on_capability_invoked(const runtime::AgentId agent,
                                         const std::string_view capability_name) {
    {
        std::lock_guard lock(mutex_);
        if (stopping_) {
            return;
        }
    }

    const auto agent_id = agent_debug_id(agent);
    const auto hits =
        breakpoints_.check_capability_breakpoints(agent_id, std::string(capability_name));
    if (hits.empty()) {
        return;
    }

    std::string description = "capability breakpoint: " + std::string(capability_name);
    if (!hits.front().description.empty()) {
        description = hits.front().description;
    }
    pause("breakpoint", std::move(description));
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

} // namespace ahfl::dap
