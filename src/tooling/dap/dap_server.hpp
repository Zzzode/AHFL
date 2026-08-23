#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/state_inspector.hpp"

namespace ahfl::json {
struct JsonValue;
}

namespace ahfl::dap {

class DebugSession;

enum class DapMessageType {
    Request,
    Response,
    Event
};

struct DapMessage {
    DapMessageType type = DapMessageType::Request;
    int seq = 0;
    std::string command;
    std::string body; // JSON string
};

struct DapCapabilities {
    bool supports_configuration_done = true;
    bool supports_set_variable = false;
    bool supports_step_in_targets = false;
    bool supports_completions = false;
};

class DapServer {
  public:
    DapServer();
    ~DapServer();

    DapServer(const DapServer &) = delete;
    DapServer &operator=(const DapServer &) = delete;

    [[nodiscard]] DapMessage handle_request(const DapMessage &request);
    [[nodiscard]] DapCapabilities capabilities() const;

    /// Emit a DAP event: a Content-Length framed JSON message with
    /// "type":"event" and the given event type, delivered to the installed
    /// event output. Safe to call from any thread; the sink must be
    /// thread-safe.
    void send_event(std::string_view event_type, std::unique_ptr<json::JsonValue> body);

    /// Install the sink for framed event messages (e.g. write to stdout).
    void set_event_output(std::function<void(std::string_view framed)> sink);

    [[nodiscard]] std::string encode_message(const DapMessage &msg) const;
    [[nodiscard]] DapMessage decode_message(const std::string &raw) const;

    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int next_seq();

    BreakpointManager &breakpoint_manager();
    StateInspector &state_inspector();

  private:
    bool initialized_ = false;
    std::atomic<int> seq_counter_{1};
    DapCapabilities capabilities_;

    BreakpointManager breakpoint_manager_;
    StateInspector state_inspector_;

    std::function<void(std::string_view)> event_output_;
    std::unique_ptr<DebugSession> session_;

    // Internal command handlers
    [[nodiscard]] std::string handle_set_breakpoints(const std::string &body);
    [[nodiscard]] std::string handle_threads();
    [[nodiscard]] std::string handle_stack_trace();
    [[nodiscard]] std::string handle_scopes(const std::string &body);
    [[nodiscard]] std::string handle_variables(const std::string &body);
    [[nodiscard]] std::string handle_continue();
    [[nodiscard]] std::string handle_next();
    [[nodiscard]] std::string handle_step_in();
    [[nodiscard]] std::string handle_step_out();
    [[nodiscard]] std::string handle_evaluate(const std::string &body);
};

} // namespace ahfl::dap
