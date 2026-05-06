// llm_capability_provider.cpp - LLM Capability Provider 组合模块

#include "ahfl/llm_provider/llm_capability_provider.hpp"

#include <iostream>
#include <sstream>
#include <variant>

namespace ahfl::llm_provider {

LLMCapabilityProvider::LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config)
    : program_(program), config_(std::move(config)),
      client_(config_.endpoint, config_.api_key, config_.timeout_seconds),
      prompt_builder_(program_), response_parser_(program_) {}

std::string LLMCapabilityProvider::build_request_json(const std::string &system_prompt,
                                                      const std::string &user_prompt) const {
    // 手写 JSON 构建（避免依赖外部 JSON 库）
    // 转义字符串中的特殊字符
    auto escape_json_string = [](const std::string &s) -> std::string {
        std::string result;
        result.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
                break;
            }
        }
        return result;
    };

    std::ostringstream oss;
    oss << "{";
    oss << "\"model\":\"" << escape_json_string(config_.model) << "\",";
    oss << "\"messages\":[";
    oss << "{\"role\":\"system\",\"content\":\"" << escape_json_string(system_prompt) << "\"},";
    oss << "{\"role\":\"user\",\"content\":\"" << escape_json_string(user_prompt) << "\"}";
    oss << "],";
    oss << "\"temperature\":" << config_.temperature << ",";
    oss << "\"max_tokens\":" << config_.max_tokens;

    // JSON mode（如果支持）
    if (config_.json_mode) {
        oss << ",\"response_format\":{\"type\":\"json_object\"}";
    }

    oss << "}";
    return oss.str();
}

std::string
LLMCapabilityProvider::extract_content_from_response(const std::string &response_body) const {
    // 从 OpenAI-compatible 响应中提取 choices[0].message.content
    // 简单查找 "content" 字段
    std::string search = "\"content\"";
    auto pos = response_body.find(search);
    if (pos == std::string::npos) {
        return "";
    }

    // 跳过 key 和冒号
    pos += search.size();
    pos = response_body.find(':', pos);
    if (pos == std::string::npos) {
        return "";
    }
    ++pos;

    // 跳过空白
    while (pos < response_body.size() &&
           (response_body[pos] == ' ' || response_body[pos] == '\t' || response_body[pos] == '\n' ||
            response_body[pos] == '\r')) {
        ++pos;
    }

    if (pos >= response_body.size() || response_body[pos] != '"') {
        return "";
    }

    // 提取字符串值
    ++pos;
    std::string content;
    while (pos < response_body.size() && response_body[pos] != '"') {
        if (response_body[pos] == '\\' && pos + 1 < response_body.size()) {
            ++pos;
            switch (response_body[pos]) {
            case 'n':
                content += '\n';
                break;
            case 't':
                content += '\t';
                break;
            case '"':
                content += '"';
                break;
            case '\\':
                content += '\\';
                break;
            default:
                content += response_body[pos];
                break;
            }
        } else {
            content += response_body[pos];
        }
        ++pos;
    }

    return content;
}

evaluator::Value LLMCapabilityProvider::invoke(const std::string &capability_name,
                                               const std::vector<evaluator::Value> &args) {
    // 查找 capability 的返回类型
    std::string return_type;
    for (const auto &decl : program_.declarations) {
        if (auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            if (cap->name == capability_name) {
                return_type = cap->return_type;
                break;
            }
        }
    }

    if (return_type.empty()) {
        std::cerr << "[LLMProvider] capability not found: " << capability_name << "\n";
        return evaluator::make_none();
    }

    // 构建 prompt
    std::string system_prompt = prompt_builder_.build_system_prompt(capability_name);
    std::string user_prompt = prompt_builder_.build_user_prompt(capability_name, args);

    // 构建请求 JSON
    std::string request_json = build_request_json(system_prompt, user_prompt);

    // 带重试的 HTTP 调用
    HttpResponse response;
    int attempts = 0;
    while (attempts <= config_.max_retries) {
        response = client_.chat_completions(request_json);
        if (response.success()) {
            break;
        }
        ++attempts;
    }

    if (!response.success()) {
        std::cerr << "[LLMProvider] HTTP request failed after " << attempts
                  << " attempts, status=" << response.status_code << "\n";
        return evaluator::make_none();
    }

    // 提取 LLM 返回的 content
    std::string content = extract_content_from_response(response.body);
    if (content.empty()) {
        std::cerr << "[LLMProvider] empty content in response\n";
        return evaluator::make_none();
    }

    // 解析为 AHFL Value
    auto result = response_parser_.parse(content, return_type);
    if (!result.has_value()) {
        std::cerr << "[LLMProvider] failed to parse response for type: " << return_type << "\n";
        return evaluator::make_none();
    }

    return std::move(*result);
}

void LLMCapabilityProvider::register_all(runtime::CapabilityRegistry &registry) {
    // 遍历所有 CapabilityDecl，注册到 registry
    for (const auto &decl : program_.declarations) {
        if (auto *cap = std::get_if<ir::CapabilityDecl>(&decl)) {
            std::string cap_name = cap->name;
            registry.register_function(cap_name,
                                       [this, cap_name](const std::vector<evaluator::Value> &args) {
                                           return this->invoke(cap_name, args);
                                       });
        }
    }
}

} // namespace ahfl::llm_provider
