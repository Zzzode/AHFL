// llm_provider_config.cpp - 配置加载与环境变量展开

#include "ahfl/llm_provider/llm_provider_config.hpp"

#include <cstdlib>
#include <string>

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

// 简单 JSON 解析：从 JSON 对象字符串中提取指定 key 的 string value
std::string extract_string_field(const std::string &json, const std::string &key) {
    // 查找 "key"
    std::string search_key = "\"" + key + "\"";
    auto pos = json.find(search_key);
    if (pos == std::string::npos) {
        return "";
    }
    // 跳过 key 和冒号
    pos += search_key.size();
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return "";
    }
    ++pos;
    // 跳过空格
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' ||
                                  json[pos] == '\r')) {
        ++pos;
    }
    if (pos >= json.size()) {
        return "";
    }
    // 如果是字符串值
    if (json[pos] == '"') {
        ++pos;
        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.size()) {
                ++pos; // 跳过转义
            }
            value += json[pos];
            ++pos;
        }
        return value;
    }
    // 如果是数字或布尔值
    std::string value;
    while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ' &&
           json[pos] != '\n') {
        value += json[pos];
        ++pos;
    }
    return value;
}

double parse_double(const std::string &s, double default_val) {
    if (s.empty()) {
        return default_val;
    }
    try {
        return std::stod(s);
    } catch (...) {
        return default_val;
    }
}

int parse_int(const std::string &s, int default_val) {
    if (s.empty()) {
        return default_val;
    }
    try {
        return std::stoi(s);
    } catch (...) {
        return default_val;
    }
}

} // namespace

LLMProviderConfig load_config(const std::string &json_content) {
    LLMProviderConfig config;

    // 先展开环境变量
    std::string expanded = expand_env_vars(json_content);

    // 解析各字段
    config.endpoint = extract_string_field(expanded, "endpoint");
    config.model = extract_string_field(expanded, "model");
    config.api_key = extract_string_field(expanded, "api_key");

    auto temp_str = extract_string_field(expanded, "temperature");
    config.temperature = parse_double(temp_str, 0.1);

    auto max_tok_str = extract_string_field(expanded, "max_tokens");
    config.max_tokens = parse_int(max_tok_str, 1024);

    auto json_mode_str = extract_string_field(expanded, "json_mode");
    config.json_mode = (json_mode_str != "false");

    auto timeout_str = extract_string_field(expanded, "timeout_seconds");
    config.timeout_seconds = parse_int(timeout_str, 30);

    auto retries_str = extract_string_field(expanded, "max_retries");
    config.max_retries = parse_int(retries_str, 2);

    return config;
}

} // namespace ahfl::llm_provider
