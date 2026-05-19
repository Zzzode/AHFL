#include "ahfl/dap/dap_server.hpp"
#include <sstream>

namespace ahfl::dap {

DapServer::DapServer() = default;

DapMessage DapServer::handle_request(const DapMessage& request) {
    DapMessage response;
    response.type = DapMessageType::Response;
    response.seq = next_seq();
    response.command = request.command;
    
    if (request.command == "initialize") {
        initialized_ = true;
        if (initialize_handler_) {
            response.body = initialize_handler_();
        } else {
            response.body = R"({"supportsConfigurationDoneRequest":true})";
        }
    } else if (request.command == "launch") {
        if (launch_handler_) {
            response.body = launch_handler_(request.body);
        } else {
            response.body = "{}";
        }
    } else if (request.command == "disconnect") {
        if (disconnect_handler_) {
            disconnect_handler_();
        }
        initialized_ = false;
        response.body = "{}";
    } else if (request.command == "configurationDone") {
        response.body = "{}";
    } else {
        response.body = R"({"error":"unknown command"})";
    }
    
    return response;
}

DapCapabilities DapServer::capabilities() const {
    return capabilities_;
}

void DapServer::set_initialize_handler(std::function<std::string()> handler) {
    initialize_handler_ = std::move(handler);
}

void DapServer::set_launch_handler(std::function<std::string(const std::string&)> handler) {
    launch_handler_ = std::move(handler);
}

void DapServer::set_disconnect_handler(std::function<void()> handler) {
    disconnect_handler_ = std::move(handler);
}

std::string DapServer::encode_message(const DapMessage& msg) const {
    std::ostringstream oss;
    oss << "Content-Length: ";
    std::string body = R"({"seq":)" + std::to_string(msg.seq)
        + R"(,"type":")" + (msg.type == DapMessageType::Response ? "response" : "request")
        + R"(","command":")" + msg.command
        + R"(","body":)" + (msg.body.empty() ? "{}" : msg.body)
        + "}";
    oss << body.size() << "\r\n\r\n" << body;
    return oss.str();
}

DapMessage DapServer::decode_message(const std::string& raw) const {
    DapMessage msg;
    // Simple parsing: find command in JSON-like format
    auto cmd_pos = raw.find("\"command\":\"");
    if (cmd_pos != std::string::npos) {
        auto start = cmd_pos + 11;
        auto end = raw.find('"', start);
        if (end != std::string::npos) {
            msg.command = raw.substr(start, end - start);
        }
    }
    
    auto body_pos = raw.find("\"body\":");
    if (body_pos != std::string::npos) {
        msg.body = raw.substr(body_pos + 7);
        // Trim trailing }
        if (!msg.body.empty() && msg.body.back() == '}') {
            msg.body.pop_back();
        }
    }
    
    msg.type = DapMessageType::Request;
    return msg;
}

bool DapServer::is_initialized() const {
    return initialized_;
}

int DapServer::next_seq() {
    return seq_counter_++;
}

} // namespace ahfl::dap
