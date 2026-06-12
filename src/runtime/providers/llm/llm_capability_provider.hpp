#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/providers/llm/http_client.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"
#include "runtime/providers/llm/prompt_builder.hpp"
#include "runtime/providers/llm/response_parser.hpp"
#include "runtime/providers/llm/tool_calling.hpp"

namespace ahfl::llm_provider {

/// Structured content extracted from an LLM response.
struct LLMResponseContent {
    std::string content;
    std::vector<ToolCall> tool_calls;

    [[nodiscard]] bool has_tool_calls() const {
        return !tool_calls.empty();
    }
};

// LLM Capability Provider: 将 AHFL capability 调用路由到 LLM API
class LLMCapabilityProvider {
  public:
    /// A tool executor that can run tools and return results.
    using ToolExecutor = std::function<ToolCallResult(const ToolCall &)>;

    LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config);

    // 调用 capability，通过 LLM 获取结果
    [[nodiscard]] runtime::CapabilityCallResult invoke(const std::string &capability_name,
                                                       const std::vector<evaluator::Value> &args);

    // 注册所有 capability 到 registry
    void register_all(runtime::CapabilityRegistry &registry);

    /// Set available tools and their executor for function calling.
    void set_tools(std::vector<ToolDefinition> tools, ToolExecutor executor);

  private:
    const ir::Program &program_;
    ir::ProgramIndex index_;
    LLMProviderConfig config_;
    HttpClient client_;
    PromptBuilder prompt_builder_;
    ResponseParser response_parser_;

    // Tool calling state
    std::vector<ToolDefinition> tools_;
    ToolExecutor tool_executor_;

    // 构建发送给 LLM 的请求 JSON (simple two-message format)
    [[nodiscard]] std::string build_request_json(const std::string &system_prompt,
                                                 const std::string &user_prompt) const;

    // 从 LLM 响应体中提取 content 文本 (backward compat wrapper)
    [[nodiscard]] std::string extract_content_from_response(const std::string &response_body) const;

    // Extract structured response including tool_calls from LLM response body
    [[nodiscard]] LLMResponseContent extract_response(const std::string &response_body) const;

    // Build a full multi-turn request JSON with tool definitions
    [[nodiscard]] std::string build_full_request_json(
        const std::vector<std::pair<std::string, std::string>> &messages) const;
};

} // namespace ahfl::llm_provider
