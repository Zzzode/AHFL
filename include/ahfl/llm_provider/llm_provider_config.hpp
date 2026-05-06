#pragma once

#include <string>

namespace ahfl::llm_provider {

// LLM 服务配置
struct LLMProviderConfig {
    std::string endpoint;     // API 端点, e.g. "https://open.bigmodel.cn/api/paas/v4"
    std::string model;        // 模型名称, e.g. "glm-4"
    std::string api_key;      // API 密钥
    double temperature = 0.1; // 低温度确保确定性输出
    int max_tokens = 1024;
    bool json_mode = true; // 强制 JSON 输出
    int timeout_seconds = 30;
    int max_retries = 2;
};

// 从 JSON 内容字符串加载配置，支持 ${ENV_VAR} 环境变量展开
[[nodiscard]] LLMProviderConfig load_config(const std::string &json_content);

// 展开字符串中的 ${ENV_VAR} 引用
[[nodiscard]] std::string expand_env_vars(const std::string &input);

} // namespace ahfl::llm_provider
