// llm_provider_config.cpp - configuration loading and environment variable expansion

#include "runtime/providers/llm/llm_provider_config.hpp"

#include "base/json/json_value.hpp"
#include "runtime/providers/llm/http_client.hpp"

#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

namespace ahfl::llm_provider {

// Expand ${ENV_VAR} environment variable references
std::string expand_env_vars(const std::string &input) {
    std::string result;
    result.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        // Detect the ${ prefix
        if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '{') {
            auto end = input.find('}', i + 2);
            if (end != std::string::npos) {
                // Extract the variable name
                std::string var_name = input.substr(i + 2, end - i - 2);
                // Read the environment variable
                const char *env_val = std::getenv(var_name.c_str());
                if (env_val != nullptr) {
                    result += env_val;
                }
                i = end; // Skip the closing }
                continue;
            }
        }
        result += input[i];
    }
    return result;
}

namespace {

[[nodiscard]] std::string normalized_auth_scheme(std::string_view scheme) {
    std::string normalized;
    normalized.reserve(scheme.size());
    for (const auto ch : scheme) {
        if (ch == '-') {
            normalized.push_back('_');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

[[nodiscard]] bool is_mtls_auth_scheme(std::string_view scheme) {
    return normalized_auth_scheme(scheme) == "mtls";
}

[[nodiscard]] std::optional<std::string> string_field(const ahfl::json::JsonValue &object,
                                                      std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_string();
    if (!value.has_value()) {
        return std::nullopt;
    }
    return std::string(*value);
}

[[nodiscard]] std::optional<double> number_field(const ahfl::json::JsonValue &object,
                                                 std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    return field->as_float();
}

[[nodiscard]] std::optional<int> int_field(const ahfl::json::JsonValue &object,
                                           std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_int();
    if (!value.has_value() || *value < std::numeric_limits<int>::min() ||
        *value > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

[[nodiscard]] std::optional<bool> bool_field(const ahfl::json::JsonValue &object,
                                             std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    return field->as_bool();
}

[[nodiscard]] std::vector<ProviderEntry>
fallback_providers_field(const ahfl::json::JsonValue &object, std::string_view key) {
    std::vector<ProviderEntry> providers;
    const auto *field = object.get(key);
    if (field == nullptr || !field->is_array()) {
        return providers;
    }

    for (std::size_t index = 0; index < field->array_items.size(); ++index) {
        const auto *item = field->array_items[index].get();
        if (item == nullptr || !item->is_object()) {
            continue;
        }

        ProviderEntry entry;
        entry.name = "fallback_" + std::to_string(index + 1);
        if (auto value = string_field(*item, "name"); value.has_value()) {
            entry.name = std::move(*value);
        }
        if (auto value = string_field(*item, "endpoint"); value.has_value()) {
            entry.endpoint = std::move(*value);
        }
        if (auto value = string_field(*item, "model"); value.has_value()) {
            entry.model = std::move(*value);
        }
        if (auto value = string_field(*item, "api_key"); value.has_value()) {
            entry.api_key = std::move(*value);
        }
        if (auto value = string_field(*item, "api_key_secret"); value.has_value()) {
            entry.api_key_secret = std::move(*value);
        }
        if (auto value = string_field(*item, "oauth2_token_secret"); value.has_value()) {
            entry.oauth2_token_secret = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_client_cert_path"); value.has_value()) {
            entry.mtls_client_cert_path = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_client_key_path"); value.has_value()) {
            entry.mtls_client_key_path = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_ca_cert_path"); value.has_value()) {
            entry.mtls_ca_cert_path = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_client_cert_secret"); value.has_value()) {
            entry.mtls_client_cert_secret = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_client_key_secret"); value.has_value()) {
            entry.mtls_client_key_secret = std::move(*value);
        }
        if (auto value = string_field(*item, "mtls_ca_cert_secret"); value.has_value()) {
            entry.mtls_ca_cert_secret = std::move(*value);
        }
        if (auto value = bool_field(*item, "mtls_verify_tls"); value.has_value()) {
            entry.mtls_verify_tls = *value;
            entry.mtls_verify_tls_set = true;
        }
        if (auto value = string_field(*item, "auth_scheme"); value.has_value()) {
            entry.auth_scheme = std::move(*value);
        }
        if (auto value = string_field(*item, "auth_header"); value.has_value()) {
            entry.auth_header = std::move(*value);
        }
        if (auto value = int_field(*item, "priority"); value.has_value()) {
            entry.priority = *value;
        }
        providers.push_back(std::move(entry));
    }

    return providers;
}

[[nodiscard]] SecretProviderConfig secret_provider_field(const ahfl::json::JsonValue &item) {
    SecretProviderConfig entry;
    bool kind_set = false;
    if (auto value = string_field(item, "kind"); value.has_value()) {
        entry.kind = std::move(*value);
        kind_set = true;
    }

    if (auto value = string_field(item, "prefix"); value.has_value()) {
        entry.prefix = std::move(*value);
    } else if (kind_set) {
        entry.prefix = entry.kind;
    }

    if ((entry.kind == "vault" || entry.kind == "cloud") && !item.get("default_for_unqualified")) {
        entry.default_for_unqualified = false;
    }

    if (auto value = bool_field(item, "default_for_unqualified"); value.has_value()) {
        entry.default_for_unqualified = *value;
    }
    if (auto value = string_field(item, "address"); value.has_value()) {
        entry.address = std::move(*value);
    }
    if (auto value = string_field(item, "token"); value.has_value()) {
        entry.token = std::move(*value);
    }
    if (auto value = string_field(item, "token_env"); value.has_value()) {
        entry.token_env = std::move(*value);
    }
    if (auto value = string_field(item, "mount_path"); value.has_value()) {
        entry.mount_path = std::move(*value);
    }
    if (auto value = string_field(item, "namespace_path"); value.has_value()) {
        entry.namespace_path = std::move(*value);
    }
    if (auto value = string_field(item, "project"); value.has_value()) {
        entry.project = std::move(*value);
    }
    if (auto value = string_field(item, "version"); value.has_value()) {
        entry.version = std::move(*value);
    }
    if (auto value = int_field(item, "timeout_seconds"); value.has_value()) {
        entry.timeout_seconds = *value;
    }
    if (auto value = bool_field(item, "verify_tls"); value.has_value()) {
        entry.verify_tls = *value;
    }
    return entry;
}

[[nodiscard]] std::vector<SecretProviderConfig>
secret_providers_field(const ahfl::json::JsonValue &object, std::string_view key) {
    std::vector<SecretProviderConfig> providers;
    const auto *field = object.get(key);
    if (field == nullptr || !field->is_array()) {
        return providers;
    }

    for (const auto &item : field->array_items) {
        if (item != nullptr && item->is_object()) {
            providers.push_back(secret_provider_field(*item));
        }
    }
    return providers;
}

[[nodiscard]] std::string normalized_budget_policy(std::string_view policy) {
    std::string normalized;
    normalized.reserve(policy.size());
    for (const auto ch : policy) {
        if (ch == '-') {
            normalized.push_back('_');
        } else {
            normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
    }
    return normalized;
}

[[nodiscard]] bool is_valid_budget_policy(std::string_view policy) {
    const auto normalized = normalized_budget_policy(policy);
    return normalized == "fail" || normalized == "warn";
}

[[nodiscard]] std::vector<LLMCapabilityTokenBudget>
capability_token_budgets_field(const ahfl::json::JsonValue &object, std::string_view key) {
    std::vector<LLMCapabilityTokenBudget> budgets;
    const auto *field = object.get(key);
    if (field == nullptr || !field->is_array()) {
        return budgets;
    }

    for (const auto &item : field->array_items) {
        if (item == nullptr || !item->is_object()) {
            continue;
        }

        LLMCapabilityTokenBudget budget;
        if (auto value = string_field(*item, "capability"); value.has_value()) {
            budget.capability_name = std::move(*value);
        } else if (auto name = string_field(*item, "capability_name"); name.has_value()) {
            budget.capability_name = std::move(*name);
        }
        if (auto value = int_field(*item, "max_tokens"); value.has_value()) {
            budget.max_tokens = *value;
        }
        if (auto value = int_field(*item, "max_prompt_tokens"); value.has_value()) {
            budget.max_prompt_tokens = *value;
        }
        if (auto value = int_field(*item, "max_total_tokens"); value.has_value()) {
            budget.max_total_tokens = *value;
        }
        if (auto value = number_field(*item, "max_total_cost_usd"); value.has_value()) {
            budget.max_total_cost_usd = *value;
        }
        if (auto value = string_field(*item, "policy"); value.has_value()) {
            budget.policy = normalized_budget_policy(*value);
        }
        budgets.push_back(std::move(budget));
    }

    return budgets;
}

struct MtlsValidationInput {
    std::string_view cert_path;
    std::string_view key_path;
    std::string_view cert_secret;
    std::string_view key_secret;
};

[[nodiscard]] std::optional<std::string> validate_mtls_config(const MtlsValidationInput &mtls,
                                                              std::string_view owner) {
    const auto has_cert = !mtls.cert_path.empty() || !mtls.cert_secret.empty();
    const auto has_key = !mtls.key_path.empty() || !mtls.key_secret.empty();
    if (!has_cert) {
        return std::string(owner) + " auth_scheme 'mtls' requires mtls_client_cert_path or "
                                    "mtls_client_cert_secret";
    }
    if (!has_key) {
        return std::string(owner) + " auth_scheme 'mtls' requires mtls_client_key_path or "
                                    "mtls_client_key_secret";
    }
    return std::nullopt;
}

} // namespace

LLMProviderConfig load_config(const std::string &json_content) {
    LLMProviderConfig config;

    // Expand environment variables first
    std::string expanded = expand_env_vars(json_content);
    auto parsed = ahfl::json::parse_json(expanded);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return config;
    }
    const auto &root = **parsed;

    if (auto value = string_field(root, "endpoint"); value.has_value()) {
        config.endpoint = std::move(*value);
    }
    if (auto value = string_field(root, "model"); value.has_value()) {
        config.model = std::move(*value);
    }
    if (auto value = string_field(root, "api_key"); value.has_value()) {
        config.api_key = std::move(*value);
    }
    if (auto value = string_field(root, "api_key_secret"); value.has_value()) {
        config.api_key_secret = std::move(*value);
    }
    if (auto value = string_field(root, "oauth2_token_secret"); value.has_value()) {
        config.oauth2_token_secret = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_client_cert_path"); value.has_value()) {
        config.mtls_client_cert_path = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_client_key_path"); value.has_value()) {
        config.mtls_client_key_path = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_ca_cert_path"); value.has_value()) {
        config.mtls_ca_cert_path = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_client_cert_secret"); value.has_value()) {
        config.mtls_client_cert_secret = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_client_key_secret"); value.has_value()) {
        config.mtls_client_key_secret = std::move(*value);
    }
    if (auto value = string_field(root, "mtls_ca_cert_secret"); value.has_value()) {
        config.mtls_ca_cert_secret = std::move(*value);
    }
    if (auto value = bool_field(root, "mtls_verify_tls"); value.has_value()) {
        config.mtls_verify_tls = *value;
    }
    if (auto value = string_field(root, "auth_scheme"); value.has_value()) {
        config.auth_scheme = std::move(*value);
    }
    if (auto value = string_field(root, "auth_header"); value.has_value()) {
        config.auth_header = std::move(*value);
    }
    if (auto value = number_field(root, "temperature"); value.has_value()) {
        config.temperature = *value;
    }
    if (auto value = int_field(root, "max_tokens"); value.has_value()) {
        config.max_tokens = *value;
    }
    if (auto value = int_field(root, "max_prompt_tokens"); value.has_value()) {
        config.max_prompt_tokens = *value;
    }
    if (auto value = int_field(root, "max_total_tokens"); value.has_value()) {
        config.max_total_tokens = *value;
    }
    if (auto value = number_field(root, "prompt_token_cost_per_million"); value.has_value()) {
        config.prompt_token_cost_per_million = *value;
    }
    if (auto value = number_field(root, "completion_token_cost_per_million"); value.has_value()) {
        config.completion_token_cost_per_million = *value;
    }
    if (auto value = number_field(root, "max_total_cost_usd"); value.has_value()) {
        config.max_total_cost_usd = *value;
    }
    if (auto value = int_field(root, "max_workflow_total_tokens"); value.has_value()) {
        config.max_workflow_total_tokens = *value;
    }
    if (auto value = int_field(root, "max_node_total_tokens"); value.has_value()) {
        config.max_node_total_tokens = *value;
    }
    if (auto value = number_field(root, "max_workflow_total_cost_usd"); value.has_value()) {
        config.max_workflow_total_cost_usd = *value;
    }
    if (auto value = number_field(root, "max_node_total_cost_usd"); value.has_value()) {
        config.max_node_total_cost_usd = *value;
    }
    if (auto value = string_field(root, "token_budget_policy"); value.has_value()) {
        config.token_budget_policy = normalized_budget_policy(*value);
    }
    config.capability_token_budgets =
        capability_token_budgets_field(root, "capability_token_budgets");
    if (auto value = bool_field(root, "json_mode"); value.has_value()) {
        config.json_mode = *value;
    }
    if (auto value = bool_field(root, "stream"); value.has_value()) {
        config.stream = *value;
    }
    if (auto value = int_field(root, "timeout_seconds"); value.has_value()) {
        config.timeout_seconds = *value;
    }
    if (auto value = int_field(root, "max_retries"); value.has_value()) {
        config.max_retries = *value;
    }
    if (auto value = bool_field(root, "response_cache_enabled"); value.has_value()) {
        config.response_cache_enabled = *value;
    }
    if (auto value = int_field(root, "response_cache_max_entries"); value.has_value()) {
        config.response_cache_max_entries = *value;
    }
    if (auto value = int_field(root, "response_cache_ttl_seconds"); value.has_value()) {
        config.response_cache_ttl_seconds = *value;
    }
    if (auto value = string_field(root, "response_cache_path"); value.has_value()) {
        config.response_cache_path = std::move(*value);
    }
    config.secret_providers = secret_providers_field(root, "secret_providers");
    if (auto value = bool_field(root, "refresh_secrets_before_use"); value.has_value()) {
        config.refresh_secrets_before_use = *value;
    }
    if (auto value = string_field(root, "tool_choice"); value.has_value()) {
        config.tool_choice = std::move(*value);
    }
    if (auto value = int_field(root, "max_tool_rounds"); value.has_value()) {
        config.max_tool_rounds = *value;
    }
    config.fallback_providers = fallback_providers_field(root, "fallback_providers");

    return config;
}

std::optional<std::string> validate_config(const LLMProviderConfig &config) {
    if (config.endpoint.empty() || config.model.empty()) {
        return "LLM config requires endpoint and model";
    }
    if (auto auth_error = validate_auth_config(HttpAuthConfig{
            .scheme = config.auth_scheme,
            .header = config.auth_header,
        });
        auth_error.has_value()) {
        return *auth_error;
    }
    if (is_mtls_auth_scheme(config.auth_scheme)) {
        if (auto mtls_error = validate_mtls_config(
                MtlsValidationInput{
                    .cert_path = config.mtls_client_cert_path,
                    .key_path = config.mtls_client_key_path,
                    .cert_secret = config.mtls_client_cert_secret,
                    .key_secret = config.mtls_client_key_secret,
                },
                "LLM config");
            mtls_error.has_value()) {
            return *mtls_error;
        }
    }
    if (config.max_tokens <= 0) {
        return "LLM config max_tokens must be positive";
    }
    if (config.max_prompt_tokens <= 0) {
        return "LLM config max_prompt_tokens must be positive";
    }
    if (config.max_total_tokens <= 0) {
        return "LLM config max_total_tokens must be positive";
    }
    if (config.max_prompt_tokens + config.max_tokens > config.max_total_tokens) {
        return "LLM config max_prompt_tokens + max_tokens must not exceed max_total_tokens";
    }
    if (config.prompt_token_cost_per_million < 0.0) {
        return "LLM config prompt_token_cost_per_million must be non-negative";
    }
    if (config.completion_token_cost_per_million < 0.0) {
        return "LLM config completion_token_cost_per_million must be non-negative";
    }
    if (config.max_total_cost_usd < 0.0) {
        return "LLM config max_total_cost_usd must be non-negative";
    }
    if (config.max_workflow_total_tokens < 0) {
        return "LLM config max_workflow_total_tokens must be non-negative";
    }
    if (config.max_node_total_tokens < 0) {
        return "LLM config max_node_total_tokens must be non-negative";
    }
    if (config.max_workflow_total_cost_usd < 0.0) {
        return "LLM config max_workflow_total_cost_usd must be non-negative";
    }
    if (config.max_node_total_cost_usd < 0.0) {
        return "LLM config max_node_total_cost_usd must be non-negative";
    }
    if (!is_valid_budget_policy(config.token_budget_policy)) {
        return "LLM config token_budget_policy must be 'fail' or 'warn'";
    }
    std::unordered_set<std::string> capability_budget_names;
    for (const auto &budget : config.capability_token_budgets) {
        if (budget.capability_name.empty()) {
            return "LLM config capability_token_budgets capability must not be empty";
        }
        if (!capability_budget_names.insert(budget.capability_name).second) {
            return "LLM config capability_token_budgets capability '" + budget.capability_name +
                   "' is duplicated";
        }
        const auto max_tokens = budget.max_tokens.value_or(config.max_tokens);
        const auto max_prompt_tokens = budget.max_prompt_tokens.value_or(config.max_prompt_tokens);
        const auto max_total_tokens = budget.max_total_tokens.value_or(config.max_total_tokens);
        if (max_tokens <= 0) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' max_tokens must be positive";
        }
        if (max_prompt_tokens <= 0) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' max_prompt_tokens must be positive";
        }
        if (max_total_tokens <= 0) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' max_total_tokens must be positive";
        }
        if (max_prompt_tokens + max_tokens > max_total_tokens) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' max_prompt_tokens + max_tokens must not exceed max_total_tokens";
        }
        if (budget.max_total_cost_usd.has_value() && *budget.max_total_cost_usd < 0.0) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' max_total_cost_usd must be non-negative";
        }
        if (budget.policy.has_value() && !is_valid_budget_policy(*budget.policy)) {
            return "LLM config capability_token_budgets '" + budget.capability_name +
                   "' policy must be 'fail' or 'warn'";
        }
    }
    if (config.timeout_seconds <= 0) {
        return "LLM config timeout_seconds must be positive";
    }
    if (config.max_retries < 0) {
        return "LLM config max_retries must be non-negative";
    }
    if (config.response_cache_enabled && config.response_cache_max_entries <= 0) {
        return "LLM config response_cache_max_entries must be positive when response cache is "
               "enabled";
    }
    if (config.response_cache_enabled && config.response_cache_ttl_seconds <= 0) {
        return "LLM config response_cache_ttl_seconds must be positive when response cache is "
               "enabled";
    }
    if (!config.response_cache_enabled && !config.response_cache_path.empty()) {
        return "LLM config response_cache_path requires response_cache_enabled";
    }
    if (config.tool_choice.empty()) {
        return "LLM config tool_choice must not be empty";
    }
    if (config.max_tool_rounds < 0) {
        return "LLM config max_tool_rounds must be non-negative";
    }
    std::unordered_set<std::string> secret_provider_prefixes;
    for (const auto &provider : config.secret_providers) {
        if (provider.kind != "env" && provider.kind != "vault" && provider.kind != "cloud") {
            return "LLM config secret_providers kind must be 'env', 'vault', or 'cloud'";
        }
        if (provider.prefix.empty()) {
            return "LLM config secret_providers prefix must not be empty";
        }
        if (!secret_provider_prefixes.insert(provider.prefix).second) {
            return "LLM config secret_providers prefix '" + provider.prefix + "' is duplicated";
        }
        if (provider.kind == "vault") {
            if (provider.address.empty()) {
                return "LLM config vault secret provider address must not be empty";
            }
            if (provider.mount_path.empty()) {
                return "LLM config vault secret provider mount_path must not be empty";
            }
            if (provider.timeout_seconds <= 0) {
                return "LLM config vault secret provider timeout_seconds must be positive";
            }
        }
        if (provider.kind == "cloud") {
            if (provider.address.empty()) {
                return "LLM config cloud secret provider address must not be empty";
            }
            if (provider.token.empty() && provider.token_env.empty()) {
                return "LLM config cloud secret provider requires token or token_env";
            }
            if (provider.version.empty()) {
                return "LLM config cloud secret provider version must not be empty";
            }
            if (provider.timeout_seconds <= 0) {
                return "LLM config cloud secret provider timeout_seconds must be positive";
            }
        }
    }
    for (const auto &provider : config.fallback_providers) {
        if (provider.name.empty()) {
            return "LLM fallback provider name must not be empty";
        }
        if (provider.endpoint.empty() || provider.model.empty()) {
            return "LLM fallback provider '" + provider.name + "' requires endpoint and model";
        }
        const auto scheme =
            provider.auth_scheme.empty() ? config.auth_scheme : provider.auth_scheme;
        const auto header =
            provider.auth_header.empty() ? config.auth_header : provider.auth_header;
        if (auto auth_error = validate_auth_config(HttpAuthConfig{
                .scheme = scheme,
                .header = header,
            });
            auth_error.has_value()) {
            return "LLM fallback provider '" + provider.name + "' " + *auth_error;
        }
        if (is_mtls_auth_scheme(scheme)) {
            const auto cert_path = provider.mtls_client_cert_path.empty()
                                       ? std::string_view(config.mtls_client_cert_path)
                                       : std::string_view(provider.mtls_client_cert_path);
            const auto key_path = provider.mtls_client_key_path.empty()
                                      ? std::string_view(config.mtls_client_key_path)
                                      : std::string_view(provider.mtls_client_key_path);
            const auto cert_secret = provider.mtls_client_cert_secret.empty()
                                         ? std::string_view(config.mtls_client_cert_secret)
                                         : std::string_view(provider.mtls_client_cert_secret);
            const auto key_secret = provider.mtls_client_key_secret.empty()
                                        ? std::string_view(config.mtls_client_key_secret)
                                        : std::string_view(provider.mtls_client_key_secret);
            if (auto mtls_error = validate_mtls_config(
                    MtlsValidationInput{
                        .cert_path = cert_path,
                        .key_path = key_path,
                        .cert_secret = cert_secret,
                        .key_secret = key_secret,
                    },
                    "LLM fallback provider '" + provider.name + "'");
                mtls_error.has_value()) {
                return *mtls_error;
            }
        }
    }
    return std::nullopt;
}

} // namespace ahfl::llm_provider
