#include "ahfl/dap/dap_server.hpp"

#include <sstream>
#include <string>

namespace ahfl::dap {

namespace {

// Simple JSON field extraction helpers
std::string extract_json_string(const std::string &json, const std::string &key) {
    auto search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return "";
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos)
        return "";
    return json.substr(pos, end - pos);
}

int extract_json_int(const std::string &json, const std::string &key) {
    auto search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return -1;
    pos += search.size();
    // Skip whitespace
    while (pos < json.size() && json[pos] == ' ')
        ++pos;
    std::string num;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        num += json[pos++];
    }
    if (num.empty())
        return -1;
    try {
        return std::stoi(num);
    } catch (...) {
        return -1;
    }
}

// Extract an array of integers: "lines":[1,5,10]
std::vector<int> extract_json_int_array(const std::string &json, const std::string &key) {
    std::vector<int> result;
    auto search = "\"" + key + "\":[";
    auto pos = json.find(search);
    if (pos == std::string::npos)
        return result;
    pos += search.size();
    auto end = json.find(']', pos);
    if (end == std::string::npos)
        return result;
    auto arr = json.substr(pos, end - pos);
    std::istringstream iss(arr);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Trim whitespace
        while (!token.empty() && token.front() == ' ')
            token.erase(token.begin());
        if (!token.empty()) {
            try {
                result.push_back(std::stoi(token));
            } catch (...) {
            }
        }
    }
    return result;
}

} // namespace

DapServer::DapServer() = default;

DapMessage DapServer::handle_request(const DapMessage &request) {
    DapMessage response;
    response.type = DapMessageType::Response;
    response.seq = next_seq();
    response.command = request.command;

    if (request.command == "initialize") {
        initialized_ = true;
        response.body = R"({"supportsConfigurationDoneRequest":true)"
                        R"(,"supportsFunctionBreakpoints":false)"
                        R"(,"supportsConditionalBreakpoints":false)"
                        R"(,"supportsEvaluateForHovers":true})";
        if (initialize_handler_) {
            auto extra = initialize_handler_();
            if (!extra.empty())
                response.body = extra;
        }
    } else if (request.command == "launch") {
        if (launch_handler_) {
            response.body = launch_handler_(request.body);
        } else {
            response.body = "{}";
        }
    } else if (request.command == "disconnect") {
        if (disconnect_handler_)
            disconnect_handler_();
        initialized_ = false;
        response.body = "{}";
    } else if (request.command == "configurationDone") {
        response.body = "{}";
    } else if (request.command == "setBreakpoints") {
        response.body = handle_set_breakpoints(request.body);
    } else if (request.command == "threads") {
        response.body = handle_threads();
    } else if (request.command == "stackTrace") {
        response.body = handle_stack_trace();
    } else if (request.command == "scopes") {
        response.body = handle_scopes(request.body);
    } else if (request.command == "variables") {
        response.body = handle_variables(request.body);
    } else if (request.command == "continue") {
        response.body = handle_continue();
    } else if (request.command == "next") {
        response.body = handle_next();
    } else if (request.command == "evaluate") {
        response.body = handle_evaluate(request.body);
    } else {
        response.body = R"({"error":"unknown command: )" + request.command + R"("})";
    }

    return response;
}

DapCapabilities DapServer::capabilities() const {
    return capabilities_;
}

void DapServer::set_initialize_handler(std::function<std::string()> handler) {
    initialize_handler_ = std::move(handler);
}

void DapServer::set_launch_handler(std::function<std::string(const std::string &)> handler) {
    launch_handler_ = std::move(handler);
}

void DapServer::set_disconnect_handler(std::function<void()> handler) {
    disconnect_handler_ = std::move(handler);
}

void DapServer::set_continue_handler(std::function<void()> handler) {
    continue_handler_ = std::move(handler);
}

void DapServer::set_next_handler(std::function<void()> handler) {
    next_handler_ = std::move(handler);
}

void DapServer::set_evaluate_handler(std::function<std::string(const std::string &)> handler) {
    evaluate_handler_ = std::move(handler);
}

std::string DapServer::encode_message(const DapMessage &msg) const {
    std::string type_str;
    switch (msg.type) {
    case DapMessageType::Request:
        type_str = "request";
        break;
    case DapMessageType::Response:
        type_str = "response";
        break;
    case DapMessageType::Event:
        type_str = "event";
        break;
    }

    std::string json = R"({"seq":)" + std::to_string(msg.seq) + R"(,"type":")" + type_str + R"(")" +
                       R"(,"command":")" + msg.command + R"(")";
    if (!msg.body.empty()) {
        json += R"(,"body":)" + msg.body;
    }
    json += "}";

    return "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n" + json;
}

DapMessage DapServer::decode_message(const std::string &raw) const {
    DapMessage msg;

    // Find content start after headers
    auto content_start = raw.find("\r\n\r\n");
    std::string json;
    if (content_start != std::string::npos) {
        json = raw.substr(content_start + 4);
    } else {
        json = raw;
    }

    // Extract fields
    auto type_str = extract_json_string(json, "type");
    if (type_str == "request")
        msg.type = DapMessageType::Request;
    else if (type_str == "response")
        msg.type = DapMessageType::Response;
    else if (type_str == "event")
        msg.type = DapMessageType::Event;

    msg.seq = extract_json_int(json, "seq");
    msg.command = extract_json_string(json, "command");

    // Extract body (everything between "body": and last })
    auto body_pos = json.find("\"body\":");
    if (body_pos != std::string::npos) {
        body_pos += 7; // skip "body":
        // Simple: take rest minus final }
        auto rest = json.substr(body_pos);
        if (!rest.empty() && rest.back() == '}') {
            rest.pop_back();
        }
        msg.body = rest;
    }

    return msg;
}

bool DapServer::is_initialized() const {
    return initialized_;
}

int DapServer::next_seq() {
    return seq_counter_++;
}

BreakpointManager &DapServer::breakpoint_manager() {
    return breakpoint_manager_;
}

StateInspector &DapServer::state_inspector() {
    return state_inspector_;
}

// --- Internal command handlers ---

std::string DapServer::handle_set_breakpoints(const std::string &body) {
    auto source_path = extract_json_string(body, "path");
    auto lines = extract_json_int_array(body, "lines");

    // Clear existing breakpoints for this source
    auto existing = breakpoint_manager_.all_breakpoints();
    for (const auto &bp : existing) {
        if (bp.source_file == source_path) {
            breakpoint_manager_.remove_breakpoint(bp.id);
        }
    }

    // Add new breakpoints
    std::ostringstream oss;
    oss << R"({"breakpoints":[)";
    bool first = true;
    for (int line : lines) {
        Breakpoint bp;
        bp.kind = BreakpointKind::Line;
        bp.source_file = source_path;
        bp.line = line;
        bp.enabled = true;
        bp.verified = true;
        int id = breakpoint_manager_.add_breakpoint(bp);

        if (!first)
            oss << ",";
        oss << R"({"id":)" << id << R"(,"verified":true,"line":)" << line << "}";
        first = false;
    }
    oss << "]}";
    return oss.str();
}

std::string DapServer::handle_threads() {
    return R"({"threads":[{"id":1,"name":"ahfl-main"}]})";
}

std::string DapServer::handle_stack_trace() {
    auto frames = state_inspector_.get_stack_frames();
    std::ostringstream oss;
    oss << R"({"stackFrames":[)";
    bool first = true;
    for (const auto &frame : frames) {
        if (!first)
            oss << ",";
        oss << R"({"id":)" << frame.id << R"(,"name":")" << frame.name << R"(")"
            << R"(,"source":{"path":")" << frame.source_file << R"("})"
            << R"(,"line":)" << frame.line << R"(,"column":1})";
        first = false;
    }
    oss << R"(],"totalFrames":)" << frames.size() << "}";
    return oss.str();
}

std::string DapServer::handle_scopes(const std::string &body) {
    // Extract frameId to determine which agent
    auto frame_id = extract_json_int(body, "frameId");
    (void)frame_id;

    // Get scopes for first available agent
    auto frames = state_inspector_.get_stack_frames();
    std::string agent_id;
    for (const auto &f : frames) {
        if (f.id == frame_id) {
            agent_id = f.agent_id;
            break;
        }
    }

    if (agent_id.empty() && !frames.empty()) {
        agent_id = frames[0].agent_id;
    }

    auto scopes = state_inspector_.get_scopes(agent_id);
    std::ostringstream oss;
    oss << R"({"scopes":[)";
    bool first = true;
    int ref_base = 100; // variablesReference base
    for (const auto &scope : scopes) {
        if (!first)
            oss << ",";
        oss << R"({"name":")" << scope.name << R"(")"
            << R"(,"variablesReference":)" << ref_base << R"(,"expensive":false})";
        first = false;
        ++ref_base;
    }
    oss << "]}";
    return oss.str();
}

std::string DapServer::handle_variables(const std::string &body) {
    auto variables_ref = extract_json_int(body, "variablesReference");

    // Map variablesReference back to scope
    // 100 = first scope (Input), 101 = Context, 102 = Output
    auto frames = state_inspector_.get_stack_frames();
    std::string agent_id;
    if (!frames.empty()) {
        agent_id = frames[0].agent_id;
    }

    std::string scope_name;
    switch (variables_ref) {
    case 100:
        scope_name = "Input";
        break;
    case 101:
        scope_name = "Context";
        break;
    case 102:
        scope_name = "Output";
        break;
    default:
        scope_name = "Context";
        break;
    }

    auto vars = state_inspector_.get_variables(agent_id, scope_name);
    std::ostringstream oss;
    oss << R"({"variables":[)";
    bool first = true;
    for (const auto &var : vars) {
        if (!first)
            oss << ",";
        oss << R"({"name":")" << var.name << R"(")"
            << R"(,"value":")" << var.value << R"(")"
            << R"(,"type":")" << var.type << R"(")"
            << R"(,"variablesReference":)" << var.variables_reference << "}";
        first = false;
    }
    oss << "]}";
    return oss.str();
}

std::string DapServer::handle_continue() {
    if (continue_handler_)
        continue_handler_();
    return R"({"allThreadsContinued":true})";
}

std::string DapServer::handle_next() {
    if (next_handler_)
        next_handler_();
    return "{}";
}

std::string DapServer::handle_evaluate(const std::string &body) {
    auto expression = extract_json_string(body, "expression");
    std::string result_value = "(no result)";

    if (evaluate_handler_ && !expression.empty()) {
        result_value = evaluate_handler_(expression);
    }

    std::ostringstream oss;
    oss << R"({"result":")" << result_value << R"(","variablesReference":0})";
    return oss.str();
}

} // namespace ahfl::dap
