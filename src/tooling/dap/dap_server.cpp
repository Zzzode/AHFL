#include "tooling/dap/dap_server.hpp"

#include "base/json/json_value.hpp"
#include "tooling/dap/debug_session.hpp"

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>

namespace ahfl::dap {

namespace {

std::string json_escape(std::string_view value) {
    return ahfl::json::serialize_json(*ahfl::json::JsonValue::make_string(std::string(value)));
}

std::string get_json_string(const ahfl::json::JsonValue &object, std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr)
        return "";
    auto value = field->as_string();
    if (!value.has_value())
        return "";
    return std::string(*value);
}

int get_json_int(const ahfl::json::JsonValue &object, std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr)
        return -1;
    auto value = field->as_int();
    if (!value.has_value())
        return -1;
    if (*value < 0 || *value > static_cast<int64_t>(std::numeric_limits<int>::max()))
        return -1;
    return static_cast<int>(*value);
}

std::vector<int> get_json_int_array(const ahfl::json::JsonValue &object, std::string_view key) {
    std::vector<int> result;
    const auto *field = object.get(key);
    if (field == nullptr || !field->is_array())
        return result;
    for (const auto &item : field->array_items) {
        auto value = item->as_int();
        if (!value.has_value() || *value < 0 ||
            *value > static_cast<int64_t>(std::numeric_limits<int>::max())) {
            return {};
        }
        result.push_back(static_cast<int>(*value));
    }
    return result;
}

} // namespace

DapServer::DapServer() = default;

DapServer::~DapServer() = default;

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
    } else if (request.command == "launch") {
        if (session_) {
            session_->disconnect();
            session_.reset();
        }
        session_ = std::make_unique<DebugSession>(*this, breakpoint_manager_, state_inspector_);
        response.body = session_->launch(request.body);
    } else if (request.command == "disconnect") {
        if (session_) {
            session_->disconnect();
            session_.reset();
        }
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
        response.body = R"({"error":)" + json_escape("unknown command: " + request.command) + "}";
    }

    return response;
}

DapCapabilities DapServer::capabilities() const {
    return capabilities_;
}

void DapServer::send_event(std::string_view event_type,
                           std::unique_ptr<ahfl::json::JsonValue> body) {
    DapMessage event;
    event.type = DapMessageType::Event;
    event.seq = next_seq();
    event.command = std::string(event_type);
    event.body = body ? ahfl::json::serialize_json(*body) : std::string{"{}"};

    if (event_output_) {
        event_output_(encode_message(event));
    }
}

void DapServer::set_event_output(std::function<void(std::string_view)> sink) {
    event_output_ = std::move(sink);
}

std::string DapServer::encode_message(const DapMessage &msg) const {
    std::string type_str;
    const char *field_name;
    switch (msg.type) {
    case DapMessageType::Request:
        type_str = "request";
        field_name = "command";
        break;
    case DapMessageType::Response:
        type_str = "response";
        field_name = "command";
        break;
    case DapMessageType::Event:
        type_str = "event";
        field_name = "event";
        break;
    }

    std::string json = R"({"seq":)" + std::to_string(msg.seq) + R"(,"type":)" +
                       json_escape(type_str) + R"(,")" + field_name + R"(":)" +
                       json_escape(msg.command);
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

    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return msg;
    }

    auto type_str = get_json_string(**parsed, "type");
    if (type_str == "request")
        msg.type = DapMessageType::Request;
    else if (type_str == "response")
        msg.type = DapMessageType::Response;
    else if (type_str == "event")
        msg.type = DapMessageType::Event;

    msg.seq = get_json_int(**parsed, "seq");
    msg.command = get_json_string(**parsed, "command");

    if (const auto *body = (*parsed)->get("body"); body != nullptr) {
        msg.body = ahfl::json::serialize_json(*body);
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
    auto parsed = ahfl::json::parse_json(body);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return R"({"breakpoints":[]})";
    }
    auto source_path = get_json_string(**parsed, "path");
    auto lines = get_json_int_array(**parsed, "lines");

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
        oss << R"({"id":)" << frame.id << R"(,"name":)" << json_escape(frame.name)
            << R"(,"source":{"path":)" << json_escape(frame.source_file) << R"(})"
            << R"(,"line":)" << frame.line << R"(,"column":1})";
        first = false;
    }
    oss << R"(],"totalFrames":)" << frames.size() << "}";
    return oss.str();
}

std::string DapServer::handle_scopes(const std::string &body) {
    // Extract frameId to determine which agent
    auto parsed = ahfl::json::parse_json(body);
    auto frame_id = parsed.has_value() && *parsed && (*parsed)->is_object()
                        ? get_json_int(**parsed, "frameId")
                        : -1;
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
        oss << R"({"name":)" << json_escape(scope.name) << R"(,"variablesReference":)" << ref_base
            << R"(,"expensive":false})";
        first = false;
        ++ref_base;
    }
    oss << "]}";
    return oss.str();
}

std::string DapServer::handle_variables(const std::string &body) {
    auto parsed = ahfl::json::parse_json(body);
    auto variables_ref = parsed.has_value() && *parsed && (*parsed)->is_object()
                             ? get_json_int(**parsed, "variablesReference")
                             : -1;

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
        oss << R"({"name":)" << json_escape(var.name) << R"(,"value":)" << json_escape(var.value)
            << R"(,"type":)" << json_escape(var.type) << R"(,"variablesReference":)"
            << var.variables_reference << "}";
        first = false;
    }
    oss << "]}";
    return oss.str();
}

std::string DapServer::handle_continue() {
    if (session_) {
        session_->resume();
    }
    return R"({"allThreadsContinued":true})";
}

std::string DapServer::handle_next() {
    // Stepping arrives with the DebugStepper (RFC 0015, later slice).
    return "{}";
}

std::string DapServer::handle_evaluate(const std::string &body) {
    auto parsed = ahfl::json::parse_json(body);
    auto expression = parsed.has_value() && *parsed && (*parsed)->is_object()
                          ? get_json_string(**parsed, "expression")
                          : "";
    (void)expression;
    // Evaluate in the paused context arrives with the DebugSession evaluator
    // (RFC 0015, later slice).
    std::ostringstream oss;
    oss << R"({"result":)" << json_escape("(evaluate not yet supported)")
        << R"(,"variablesReference":0})";
    return oss.str();
}

} // namespace ahfl::dap
