// llm_provider_config.cpp - 配置加载与环境变量展开

#include "runtime/providers/llm/llm_provider_config.hpp"

#include "base/json/json_value.hpp"

#include <cstdlib>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace ahfl::llm_provider {

// 展开 ${ENV_VAR} 环境变量引用
std::string expand_env_vars(const std::string &input) {
    std::string result;
    result.reserve(input.size());

    for (std::size_t i = 0; i < input.size(); ++i) {
        // 检测 ${ 开头
        if (input[i] == '$' && i + 1 < input.size() && input[i + 1] == '{') {
            auto end = input.find('}', i + 2);
            if (end != std::string::npos) {
                // 提取变量名
                std::string var_name = input.substr(i + 2, end - i - 2);
                // 读取环境变量
                const char *env_val = std::getenv(var_name.c_str());
                if (env_val != nullptr) {
                    result += env_val;
                }
                i = end; // 跳过 }
                continue;
            }
        }
        result += input[i];
    }
    return result;
}

namespace {

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

} // namespace

LLMProviderConfig load_config(const std::string &json_content) {
    LLMProviderConfig config;

    // 先展开环境变量
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
    if (auto value = number_field(root, "temperature"); value.has_value()) {
        config.temperature = *value;
    }
    if (auto value = int_field(root, "max_tokens"); value.has_value()) {
        config.max_tokens = *value;
    }
    if (auto value = bool_field(root, "json_mode"); value.has_value()) {
        config.json_mode = *value;
    }
    if (auto value = int_field(root, "timeout_seconds"); value.has_value()) {
        config.timeout_seconds = *value;
    }
    if (auto value = int_field(root, "max_retries"); value.has_value()) {
        config.max_retries = *value;
    }

    return config;
}

} // namespace ahfl::llm_provider
