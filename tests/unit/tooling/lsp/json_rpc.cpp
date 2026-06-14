#include "tooling/lsp/json_rpc.hpp"

#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

namespace {

using namespace ahfl::lsp;

/// Helper: construct a Content-Length framed message string.
std::string make_frame(const std::string &json_body) {
    std::string frame;
    frame += "Content-Length: " + std::to_string(json_body.size()) + "\r\n";
    frame += "\r\n";
    frame += json_body;
    return frame;
}

bool test_read_request() {
    std::string body = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    std::string frame = make_frame(body);

    std::istringstream in(frame);
    std::ostringstream out;
    JsonRpcTransport transport(in, out);

    auto msg = transport.read_message();
    if (!msg.has_value())
        return false;

    auto *req = std::get_if<JsonRpcRequest>(&*msg);
    if (!req)
        return false;
    if (req->method != "initialize")
        return false;
    if (req->id.empty())
        return false;

    return true;
}

bool test_read_notification() {
    std::string body = R"({"jsonrpc":"2.0","method":"initialized","params":{}})";
    std::string frame = make_frame(body);

    std::istringstream in(frame);
    std::ostringstream out;
    JsonRpcTransport transport(in, out);

    auto msg = transport.read_message();
    if (!msg.has_value())
        return false;

    auto *notif = std::get_if<JsonRpcNotification>(&*msg);
    if (!notif)
        return false;
    if (notif->method != "initialized")
        return false;

    return true;
}

bool test_send_response_content_length() {
    std::istringstream in;
    std::ostringstream out;
    JsonRpcTransport transport(in, out);

    JsonRpcResponse resp;
    resp.id = "42";
    resp.result = ahfl::json::JsonValue::make_string("ok");
    resp.error = std::nullopt;

    transport.send_response(resp);

    std::string output = out.str();
    // Output should start with Content-Length header
    if (output.find("Content-Length: ") != 0)
        return false;
    // Should contain \r\n\r\n separator
    if (output.find("\r\n\r\n") == std::string::npos)
        return false;
    // The body part should contain the id
    auto body_start = output.find("\r\n\r\n") + 4;
    std::string body = output.substr(body_start);
    if (body.find("42") == std::string::npos)
        return false;

    return true;
}

bool test_empty_stream_returns_nullopt() {
    std::istringstream in("");
    std::ostringstream out;
    JsonRpcTransport transport(in, out);

    auto msg = transport.read_message();
    if (msg.has_value())
        return false;

    return true;
}

bool test_malformed_json_returns_nullopt_without_throwing() {
    std::string body = R"({"jsonrpc":"2.0","id":1,"method":)";
    std::string frame = make_frame(body);

    std::istringstream in(frame);
    std::ostringstream out;
    JsonRpcTransport transport(in, out);

    auto msg = transport.read_message();
    return !msg.has_value();
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](bool (*fn)(), const char *name) {
        if (!fn()) {
            std::cerr << "FAIL: " << name << "\n";
            ++failures;
        }
    };

    run(test_read_request, "test_read_request");
    run(test_read_notification, "test_read_notification");
    run(test_send_response_content_length, "test_send_response_content_length");
    run(test_empty_stream_returns_nullopt, "test_empty_stream_returns_nullopt");
    run(test_malformed_json_returns_nullopt_without_throwing,
        "test_malformed_json_returns_nullopt_without_throwing");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
