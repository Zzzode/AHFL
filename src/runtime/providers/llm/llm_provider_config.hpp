#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "runtime/providers/llm/provider_registry.hpp"

namespace ahfl::llm_provider {

struct SecretProviderConfig {
    std::string kind{"env"};   // env、vault 或 cloud
    std::string prefix{"env"}; // secret handle prefix, e.g. env:NAME / vault:path
    bool default_for_unqualified{true};

    // Vault / cloud Secret Manager settings.
    std::string address{"http://127.0.0.1:8200"};
    std::string token;
    std::string token_env;
    std::string mount_path{"secret"};
    std::string namespace_path;
    std::string project;
    std::string version{"latest"};
    int timeout_seconds{5};
    bool verify_tls{true};
};

struct LLMCapabilityTokenBudget {
    std::string capability_name;
    std::optional<int> max_tokens;
    std::optional<int> max_prompt_tokens;
    std::optional<int> max_total_tokens;
    std::optional<double> max_total_cost_usd;
    std::optional<std::string> policy;
};

// LLM 服务配置
struct LLMProviderConfig {
    std::string endpoint;              // API 端点, e.g. "https://open.bigmodel.cn/api/paas/v4"
    std::string model;                 // 模型名称, e.g. "glm-4"
    std::string api_key;               // API 密钥，仅允许作为兼容路径
    std::string api_key_secret;        // 环境变量 secret handle，优先于 api_key
    std::string oauth2_token_secret;   // OAuth2 access token secret handle
    std::string mtls_client_cert_path; // mTLS client certificate path reference
    std::string mtls_client_key_path;  // mTLS client private key path reference
    std::string mtls_ca_cert_path;     // Optional mTLS CA certificate path reference
    std::string mtls_client_cert_secret;
    std::string mtls_client_key_secret;
    std::string mtls_ca_cert_secret;
    bool mtls_verify_tls{true};
    std::string auth_scheme{"bearer"}; // bearer、api_key_header、OAuth2 bearer 或 mTLS
    std::string auth_header{"Authorization"};
    double temperature = 0.1; // 低温度确保确定性输出
    int max_tokens = 1024;
    int max_prompt_tokens = 3072;
    int max_total_tokens = 4096;
    double prompt_token_cost_per_million = 0.0;
    double completion_token_cost_per_million = 0.0;
    double max_total_cost_usd = 0.0;
    int max_workflow_total_tokens = 0;
    int max_node_total_tokens = 0;
    double max_workflow_total_cost_usd = 0.0;
    double max_node_total_cost_usd = 0.0;
    std::string token_budget_policy{"fail"}; // fail 或 warn
    std::vector<LLMCapabilityTokenBudget> capability_token_budgets;
    bool json_mode = true; // 强制 JSON 输出
    bool stream = false;   // 使用 OpenAI-compatible streaming 响应
    int timeout_seconds = 30;
    int max_retries = 2;
    bool response_cache_enabled = false;
    int response_cache_max_entries = 128;
    int response_cache_ttl_seconds = 300;
    std::string response_cache_path;
    std::vector<SecretProviderConfig> secret_providers;
    bool refresh_secrets_before_use = false;

    // Tool calling settings
    std::string tool_choice{"auto"}; // "auto", "none", or specific tool name
    int max_tool_rounds{5};          // max tool call iterations before giving up

    std::vector<ProviderEntry> fallback_providers;
};

// 从 JSON 内容字符串加载配置，支持 ${ENV_VAR} 环境变量展开
[[nodiscard]] LLMProviderConfig load_config(const std::string &json_content);

// 校验运行期必需字段和预算边界；返回空表示配置可用。
[[nodiscard]] std::optional<std::string> validate_config(const LLMProviderConfig &config);

// 展开字符串中的 ${ENV_VAR} 引用
[[nodiscard]] std::string expand_env_vars(const std::string &input);

} // namespace ahfl::llm_provider
