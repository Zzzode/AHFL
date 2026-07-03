#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <variant>

#include "base/json/json_value.hpp"

namespace ahfl::lsp {

// ---------- Message types ----------

struct JsonRpcRequest {
    std::string id; // string or integer serialized as string
    std::string method;
    std::unique_ptr<json::JsonValue> params; // may be null
};

struct JsonRpcNotification {
    std::string method;
    std::unique_ptr<json::JsonValue> params;
};

struct JsonRpcError {
    int code{0};
    std::string message;
};

struct JsonRpcResponse {
    std::string id;
    std::unique_ptr<json::JsonValue> result; // null if error
    std::optional<JsonRpcError> error;
};

using IncomingMessage = std::variant<JsonRpcRequest, JsonRpcNotification, JsonRpcResponse>;

// ---------- Transport ----------

/// JSON-RPC transport using Content-Length framing over stdin/stdout.
class JsonRpcTransport {
  public:
    explicit JsonRpcTransport(std::istream &in, std::ostream &out);

    /// Read one complete JSON-RPC message. Returns nullopt on EOF.
    [[nodiscard]] std::optional<IncomingMessage> read_message();

    /// Send a response (to a request).
    void send_response(const JsonRpcResponse &resp);

    /// Send a request (server → client).
    void send_request(const std::string &id,
                      const std::string &method,
                      std::unique_ptr<json::JsonValue> params);

    /// Send a notification (server → client).
    void send_notification(const std::string &method, std::unique_ptr<json::JsonValue> params);

  private:
    std::istream &in_;
    std::ostream &out_;

    void write_message(const std::string &json_body);
};

// ---------- Standard error codes ----------

inline constexpr int kParseError = -32700;
inline constexpr int kInvalidRequest = -32600;
inline constexpr int kMethodNotFound = -32601;
inline constexpr int kInvalidParams = -32602;
inline constexpr int kInternalError = -32603;

} // namespace ahfl::lsp
