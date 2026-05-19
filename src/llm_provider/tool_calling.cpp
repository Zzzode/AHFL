#include "ahfl/llm_provider/tool_calling.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ahfl::llm_provider {

std::string build_tool_request_json(std::string_view system_prompt, std::string_view user_prompt,
                                    std::string_view model,
                                    const std::vector<ToolDefinition> &tools) {
    std::string json = R"({"model":")";
    json += model;
    json += R"(","messages":[{"role":"system","content":")";
    // Escape system prompt
    for (char c : system_prompt) {
        if (c == '"') {
            json += "\\\"";
        } else if (c == '\\') {
            json += "\\\\";
        } else if (c == '\n') {
            json += "\\n";
        } else {
            json += c;
        }
    }
    json += R"("},{"role":"user","content":")";
    for (char c : user_prompt) {
        if (c == '"') {
            json += "\\\"";
        } else if (c == '\\') {
            json += "\\\\";
        } else if (c == '\n') {
            json += "\\n";
        } else {
            json += c;
        }
    }
    json += R"("}],"tools":[)";

    for (std::size_t i = 0; i < tools.size(); ++i) {
        if (i > 0) {
            json += ',';
        }
        json += R"({"type":"function","function":{"name":")";
        json += tools[i].name;
        json += R"(","description":")";
        for (char c : tools[i].description) {
            if (c == '"') {
                json += "\\\"";
            } else if (c == '\n') {
                json += "\\n";
            } else {
                json += c;
            }
        }
        json += R"(","parameters":)";
        json += tools[i].params_schema_json;
        json += "}}";
    }

    json += "]}";
    return json;
}

std::vector<ToolCall> parse_tool_calls(std::string_view response_body) {
    std::vector<ToolCall> calls;

    // Parse tool_calls array from response.
    // Look for "tool_calls":[{...},...] pattern
    constexpr std::string_view marker = "\"tool_calls\":[";
    auto pos = response_body.find(marker);
    if (pos == std::string_view::npos) {
        return calls;
    }
    pos += marker.size();

    // Parse each tool call object
    while (pos < response_body.size() && response_body[pos] != ']') {
        if (response_body[pos] == ',') {
            ++pos;
        }
        if (response_body[pos] != '{') {
            break;
        }

        ToolCall call;

        // Find "id":"..."
        auto id_pos = response_body.find("\"id\":\"", pos);
        if (id_pos != std::string_view::npos && id_pos < response_body.find('}', pos)) {
            id_pos += 6;
            auto end = response_body.find('"', id_pos);
            if (end != std::string_view::npos) {
                call.id = std::string(response_body.substr(id_pos, end - id_pos));
            }
        }

        // Find "name":"..."
        auto name_pos = response_body.find("\"name\":\"", pos);
        if (name_pos != std::string_view::npos) {
            name_pos += 8;
            auto end = response_body.find('"', name_pos);
            if (end != std::string_view::npos) {
                call.name = std::string(response_body.substr(name_pos, end - name_pos));
            }
        }

        // Find "arguments":"..." (may contain escaped quotes)
        auto args_pos = response_body.find("\"arguments\":\"", pos);
        if (args_pos != std::string_view::npos) {
            args_pos += 13;
            std::string args;
            while (args_pos < response_body.size() && response_body[args_pos] != '"') {
                if (response_body[args_pos] == '\\' && args_pos + 1 < response_body.size()) {
                    ++args_pos;
                    if (response_body[args_pos] == '"') {
                        args += '"';
                    } else if (response_body[args_pos] == '\\') {
                        args += '\\';
                    } else if (response_body[args_pos] == 'n') {
                        args += '\n';
                    } else {
                        args += response_body[args_pos];
                    }
                } else {
                    args += response_body[args_pos];
                }
                ++args_pos;
            }
            call.arguments_json = std::move(args);
        }

        if (!call.name.empty()) {
            calls.push_back(std::move(call));
        }

        // Skip to next object or end of array
        auto next_brace = response_body.find('}', pos);
        if (next_brace == std::string_view::npos) {
            break;
        }
        pos = next_brace + 1;
    }

    return calls;
}

} // namespace ahfl::llm_provider
