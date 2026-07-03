#include "tooling/lsp/json_rpc.hpp"

#include <charconv>
#include <cstddef>
#include <string>

namespace ahfl::lsp {
namespace {

[[nodiscard]] std::string id_value_to_string(const json::JsonValue &id_val) {
    if (auto s = id_val.as_string(); s.has_value()) {
        return std::string(*s);
    }
    if (auto i = id_val.as_int(); i.has_value()) {
        return std::to_string(*i);
    }
    return "0";
}

void set_id_field(json::JsonValue &object, const std::string &id) {
    int64_t id_int = 0;
    auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), id_int);
    if (ec == std::errc{} && ptr == id.data() + id.size()) {
        object.set("id", json::JsonValue::make_int(id_int));
        return;
    }
    object.set("id", json::JsonValue::make_string(id));
}

void set_cloned_field(json::JsonValue &object,
                      std::string key,
                      const std::unique_ptr<json::JsonValue> &value) {
    if (!value) {
        return;
    }
    auto json_str = json::serialize_json(*value);
    auto reparsed = json::parse_json(json_str);
    if (reparsed.has_value()) {
        object.set(std::move(key), std::move(*reparsed));
    }
}

} // namespace

JsonRpcTransport::JsonRpcTransport(std::istream &in, std::ostream &out) : in_(in), out_(out) {}

std::optional<IncomingMessage> JsonRpcTransport::read_message() {
    // Read headers until empty line
    std::size_t content_length = 0;
    std::string line;

    while (std::getline(in_, line)) {
        // Remove trailing \r if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            break; // End of headers
        }

        // Parse Content-Length header
        constexpr std::string_view prefix = "Content-Length: ";
        if (line.starts_with(prefix)) {
            auto num_str = std::string_view(line).substr(prefix.size());
            std::from_chars(num_str.data(), num_str.data() + num_str.size(), content_length);
        }
        // Ignore other headers (Content-Type etc.)
    }

    if (in_.eof() || content_length == 0) {
        return std::nullopt;
    }

    // Read exactly content_length bytes
    std::string body(content_length, '\0');
    in_.read(body.data(), static_cast<std::streamsize>(content_length));

    if (static_cast<std::size_t>(in_.gcount()) != content_length) {
        return std::nullopt;
    }

    // Parse JSON
    auto json_opt = json::parse_json(body);
    if (!json_opt.has_value()) {
        return std::nullopt;
    }

    auto &root = *json_opt.value();

    // Check if it is a response to a server-initiated request.
    const auto *id_val = root.get("id");
    const auto *method_val = root.get("method");
    if (method_val == nullptr && id_val != nullptr) {
        JsonRpcResponse resp;
        resp.id = id_value_to_string(*id_val);
        if (auto *result_raw = root.get_mut("result"); result_raw != nullptr) {
            resp.result = std::make_unique<json::JsonValue>();
            std::swap(*resp.result, *result_raw);
        }
        if (const auto *error_raw = root.get("error");
            error_raw != nullptr && error_raw->is_object()) {
            JsonRpcError error;
            if (const auto *code = error_raw->get("code"); code != nullptr) {
                if (auto value = code->as_int(); value.has_value()) {
                    error.code = static_cast<int>(*value);
                }
            }
            if (const auto *message = error_raw->get("message"); message != nullptr) {
                if (auto value = message->as_string(); value.has_value()) {
                    error.message = std::string(*value);
                }
            }
            resp.error = std::move(error);
        }
        return IncomingMessage{std::move(resp)};
    }

    // Extract method
    if (method_val == nullptr) {
        return std::nullopt;
    }
    auto method_str = method_val->as_string();
    if (!method_str.has_value()) {
        return std::nullopt;
    }

    // Extract params (may be absent)
    std::unique_ptr<json::JsonValue> params;
    auto *params_raw = root.get_mut("params");
    if (params_raw != nullptr) {
        // Move the params subtree out — create a copy via serialize/reparse for simplicity
        // Actually, let's just create a new object and swap fields
        params = std::make_unique<json::JsonValue>();
        std::swap(*params, *params_raw);
    }

    if (id_val != nullptr) {
        // It's a request
        JsonRpcRequest req;
        req.method = std::string(*method_str);
        req.params = std::move(params);

        req.id = id_value_to_string(*id_val);

        return IncomingMessage{std::move(req)};
    }

    // It's a notification
    JsonRpcNotification notif;
    notif.method = std::string(*method_str);
    notif.params = std::move(params);
    return IncomingMessage{std::move(notif)};
}

void JsonRpcTransport::send_response(const JsonRpcResponse &resp) {
    auto obj = json::JsonValue::make_object();
    obj->set("jsonrpc", json::JsonValue::make_string("2.0"));
    set_id_field(*obj, resp.id);

    if (resp.error.has_value()) {
        auto err_obj = json::JsonValue::make_object();
        err_obj->set("code", json::JsonValue::make_int(resp.error->code));
        err_obj->set("message", json::JsonValue::make_string(resp.error->message));
        obj->set("error", std::move(err_obj));
    } else if (resp.result) {
        set_cloned_field(*obj, "result", resp.result);
    } else {
        obj->set("result", json::JsonValue::make_null());
    }

    write_message(json::serialize_json(*obj));
}

void JsonRpcTransport::send_request(const std::string &id,
                                    const std::string &method,
                                    std::unique_ptr<json::JsonValue> params) {
    auto obj = json::JsonValue::make_object();
    obj->set("jsonrpc", json::JsonValue::make_string("2.0"));
    set_id_field(*obj, id);
    obj->set("method", json::JsonValue::make_string(method));
    set_cloned_field(*obj, "params", params);
    write_message(json::serialize_json(*obj));
}

void JsonRpcTransport::send_notification(const std::string &method,
                                         std::unique_ptr<json::JsonValue> params) {
    auto obj = json::JsonValue::make_object();
    obj->set("jsonrpc", json::JsonValue::make_string("2.0"));
    obj->set("method", json::JsonValue::make_string(method));

    set_cloned_field(*obj, "params", params);

    write_message(json::serialize_json(*obj));
}

void JsonRpcTransport::write_message(const std::string &json_body) {
    out_ << "Content-Length: " << json_body.size() << "\r\n\r\n" << json_body;
    out_.flush();
}

} // namespace ahfl::lsp
