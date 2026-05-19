#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

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
    std::string body;  // JSON string
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
    
    [[nodiscard]] DapMessage handle_request(const DapMessage& request);
    [[nodiscard]] DapCapabilities capabilities() const;
    
    void set_initialize_handler(std::function<std::string()> handler);
    void set_launch_handler(std::function<std::string(const std::string&)> handler);
    void set_disconnect_handler(std::function<void()> handler);
    
    [[nodiscard]] std::string encode_message(const DapMessage& msg) const;
    [[nodiscard]] DapMessage decode_message(const std::string& raw) const;
    
    [[nodiscard]] bool is_initialized() const;
    [[nodiscard]] int next_seq();
    
private:
    bool initialized_ = false;
    int seq_counter_ = 1;
    DapCapabilities capabilities_;
    std::function<std::string()> initialize_handler_;
    std::function<std::string(const std::string&)> launch_handler_;
    std::function<void()> disconnect_handler_;
};

} // namespace ahfl::dap
