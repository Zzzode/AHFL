#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "ahfl/compiler/ir/ir.hpp"
#include "runtime/engine/workflow_runtime.hpp"

namespace ahfl::dap {

class DapServer;
class BreakpointManager;
class StateInspector;

/// DebugSession owns the debug lifecycle for one launched workflow: source
/// compilation, the execution thread, pause/resume coordination and DAP event
/// emission (RFC 0015 Slice 1).
///
/// The workflow runs on a dedicated worker thread. State-entry hooks installed
/// on the WorkflowRuntime update the StateInspector, consult the
/// BreakpointManager and, on a hit, block the worker inside the hook until
/// resume() or disconnect() is called.
class DebugSession {
  public:
    DebugSession(DapServer &server, BreakpointManager &breakpoints, StateInspector &inspector);
    ~DebugSession();

    DebugSession(const DebugSession &) = delete;
    DebugSession &operator=(const DebugSession &) = delete;
    DebugSession(DebugSession &&) = delete;
    DebugSession &operator=(DebugSession &&) = delete;

    /// Compile and launch the workflow described by a DAP `launch` request
    /// body: a JSON object with "program" (path to a .ahfl source file) and
    /// "workflow" (workflow name) fields. Returns the response body for the
    /// launch response; failures are reported as `output` + `terminated`
    /// events.
    [[nodiscard]] std::string launch(const std::string &config_json);

    /// Request the workflow to stop and join the execution thread. Emits a
    /// `terminated` event when the stop was caused by the disconnect itself.
    void disconnect();

    /// Resume execution after a stop (DAP `continue`).
    void resume();

    [[nodiscard]] bool is_running() const noexcept;

  private:
    void execute(std::string workflow_name);
    void on_state_entered(ahfl::runtime::AgentId agent, std::string_view state_name);
    void pause(std::string reason, std::string description);
    [[nodiscard]] bool mark_terminated();
    void emit_terminated();
    void emit_output(std::string_view category, std::string_view text);

    DapServer &server_;
    BreakpointManager &breakpoints_;
    StateInspector &inspector_;

    std::optional<ahfl::ir::Program> program_;
    std::unique_ptr<ahfl::runtime::WorkflowRuntime> runtime_;
    std::thread worker_;

    mutable std::mutex mutex_;
    std::condition_variable resume_cv_;
    bool paused_ = false;
    bool stopping_ = false;
    bool terminated_emitted_ = false;
};

} // namespace ahfl::dap
