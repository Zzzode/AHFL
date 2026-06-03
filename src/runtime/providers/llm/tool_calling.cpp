#include "runtime/providers/llm/tool_calling.hpp"

#include "base/json/json_value.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::llm_provider {

namespace {

using ahfl::json::JsonValue;

[[nodiscard]] std::unique_ptr<JsonValue> make_message(std::string role, std::string content) {
    auto message = JsonValue::make_object();
    message->set("role", JsonValue::make_string(std::move(role)));
    message->set("content", JsonValue::make_string(std::move(content)));
    return message;
}

[[nodiscard]] std::unique_ptr<JsonValue> parse_parameters_schema(std::string_view schema_json) {
    auto parsed = ahfl::json::parse_json(schema_json);
    if (parsed.has_value() && *parsed) {
        return std::move(*parsed);
    }
    return JsonValue::make_string(std::string(schema_json));
}

[[nodiscard]] const JsonValue *find_tool_calls_array(const JsonValue &value) {
    if (value.is_object()) {
        if (const auto *tool_calls = value.get("tool_calls");
            tool_calls != nullptr && tool_calls->is_array()) {
            return tool_calls;
        }
        for (const auto &[_, child] : value.object_fields) {
            if (child != nullptr) {
                if (const auto *found = find_tool_calls_array(*child); found != nullptr) {
                    return found;
                }
            }
        }
    }
    if (value.is_array()) {
        for (const auto &child : value.array_items) {
            if (child != nullptr) {
                if (const auto *found = find_tool_calls_array(*child); found != nullptr) {
                    return found;
                }
            }
        }
    }
    return nullptr;
}

[[nodiscard]] std::string json_string_or_serialized(const JsonValue *value) {
    if (value == nullptr) {
        return {};
    }
    if (auto string_value = value->as_string(); string_value.has_value()) {
        return std::string(*string_value);
    }
    if (!value->is_null()) {
        return ahfl::json::serialize_json(*value);
    }
    return {};
}

} // namespace

std::string build_tool_request_json(std::string_view system_prompt,
                                    std::string_view user_prompt,
                                    std::string_view model,
                                    const std::vector<ToolDefinition> &tools) {
    auto root = JsonValue::make_object();
    root->set("model", JsonValue::make_string(std::string(model)));

    auto messages = JsonValue::make_array();
    messages->push(make_message("system", std::string(system_prompt)));
    messages->push(make_message("user", std::string(user_prompt)));
    root->set("messages", std::move(messages));

    auto tool_array = JsonValue::make_array();
    for (const auto &tool : tools) {
        auto function = JsonValue::make_object();
        function->set("name", JsonValue::make_string(tool.name));
        function->set("description", JsonValue::make_string(tool.description));
        function->set("parameters", parse_parameters_schema(tool.params_schema_json));

        auto tool_object = JsonValue::make_object();
        tool_object->set("type", JsonValue::make_string("function"));
        tool_object->set("function", std::move(function));
        tool_array->push(std::move(tool_object));
    }
    root->set("tools", std::move(tool_array));

    return ahfl::json::serialize_json(*root);
}

std::vector<ToolCall> parse_tool_calls(std::string_view response_body) {
    std::vector<ToolCall> calls;
    auto parsed = ahfl::json::parse_json(response_body);
    if (!parsed.has_value() || !*parsed) {
        return calls;
    }

    const auto *tool_calls = find_tool_calls_array(**parsed);
    if (tool_calls == nullptr) {
        return calls;
    }
    for (const auto &item : tool_calls->array_items) {
        if (item == nullptr || !item->is_object()) {
            continue;
        }
        ToolCall call;
        call.id = json_string_or_serialized(item->get("id"));

        const auto *function = item->get("function");
        if (function != nullptr && function->is_object()) {
            call.name = json_string_or_serialized(function->get("name"));
            call.arguments_json = json_string_or_serialized(function->get("arguments"));
        } else {
            call.name = json_string_or_serialized(item->get("name"));
            call.arguments_json = json_string_or_serialized(item->get("arguments"));
        }

        if (!call.name.empty()) {
            calls.push_back(std::move(call));
        }
    }

    return calls;
}

} // namespace ahfl::llm_provider
