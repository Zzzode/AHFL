#pragma once

#include <string>
#include <vector>

#include "ahfl/evaluator/value.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/llm_provider/http_client.hpp"
#include "ahfl/llm_provider/llm_provider_config.hpp"
#include "ahfl/llm_provider/prompt_builder.hpp"
#include "ahfl/llm_provider/response_parser.hpp"
#include "ahfl/runtime/capability_bridge.hpp"

namespace ahfl::llm_provider {

// LLM Capability Provider: 将 AHFL capability 调用路由到 LLM API
class LLMCapabilityProvider {
  public:
    LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config);

    // 调用 capability，通过 LLM 获取结果
    [[nodiscard]] evaluator::Value invoke(const std::string &capability_name,
                                          const std::vector<evaluator::Value> &args);

    // 注册所有 capability 到 registry
    void register_all(runtime::CapabilityRegistry &registry);

  private:
    const ir::Program &program_;
    LLMProviderConfig config_;
    HttpClient client_;
    PromptBuilder prompt_builder_;
    ResponseParser response_parser_;

    // 构建发送给 LLM 的请求 JSON
    [[nodiscard]] std::string build_request_json(const std::string &system_prompt,
                                                  const std::string &user_prompt) const;

    // 从 LLM 响应体中提取 content 文本
    [[nodiscard]] std::string extract_content_from_response(const std::string &response_body) const;
};

} // namespace ahfl::llm_provider
