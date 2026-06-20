#pragma once

#include <string>
#include <vector>

namespace ahfl::llm_provider {

/// Definition of a tool that can be called by the LLM.
struct ToolDefinition {
    std::string name;
    std::string description;
    std::string params_schema_json;
};

/// A tool call requested by the LLM.
struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;
};

/// Result of executing a tool call.
struct ToolCallResult {
    std::string tool_call_id;
    std::string content;
};

/// Build a request JSON body that includes tool definitions.
[[nodiscard]] std::string build_tool_request_json(std::string_view system_prompt,
                                                  std::string_view user_prompt,
                                                  std::string_view model,
                                                  const std::vector<ToolDefinition> &tools);

/// Parse tool calls from an LLM response body.
[[nodiscard]] std::vector<ToolCall> parse_tool_calls(std::string_view response_body);

} // namespace ahfl::llm_provider
