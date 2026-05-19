#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ahfl/dap/breakpoints.hpp"
#include "ahfl/dap/state_inspector.hpp"

namespace ahfl::dap {

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

    [[nodiscard]] DapMessage handle_request(const DapMessage &request);
    [[nodiscard]] DapCapabilities capabilities() const;

    void set_initialize_handler(std::function<std::string()> handler);
    void set_launch_handler(std::function<std::string(const std::string &)> handler);
    void set_disconnect_handler(std::function<void()> handler);
    void set_continue_handler(std::function<void()> handler);
    void set_next_handler(std::function<void()> handler);
    void set_evaluate_handler(std::function<std::string(const std::string &)> handler);

    [[nodiscard]] std::string encode_message(const DapMessage &msg) const;
    [[nodiscard]] DapMessage decode_message(const std::string &raw) const;

    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int next_seq();

    BreakpointManager &breakpoint_manager();
    StateInspector &state_inspector();

  private:
    bool initialized_ = false;
    int seq_counter_ = 1;
    DapCapabilities capabilities_;

    BreakpointManager breakpoint_manager_;
    StateInspector state_inspector_;

    std::function<std::string()> initialize_handler_;
    std::function<std::string(const std::string &)> launch_handler_;
    std::function<void()> disconnect_handler_;
    std::function<void()> continue_handler_;
    std::function<void()> next_handler_;
    std::function<std::string(const std::string &)> evaluate_handler_;

    // Internal command handlers
    [[nodiscard]] std::string handle_set_breakpoints(const std::string &body);
    [[nodiscard]] std::string handle_threads();
    [[nodiscard]] std::string handle_stack_trace();
    [[nodiscard]] std::string handle_scopes(const std::string &body);
    [[nodiscard]] std::string handle_variables(const std::string &body);
    [[nodiscard]] std::string handle_continue();
    [[nodiscard]] std::string handle_next();
    [[nodiscard]] std::string handle_evaluate(const std::string &body);
};

} // namespace ahfl::dap
