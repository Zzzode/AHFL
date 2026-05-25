// llm_capability_provider.cpp - LLM Capability Provider 组合模块

#include "llm_provider/llm_capability_provider.hpp"

#include "json/json_value.hpp"

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
    auto root = ahfl::json::JsonValue::make_object();
    root->set("model", ahfl::json::JsonValue::make_string(config_.model));

    auto messages = ahfl::json::JsonValue::make_array();
    auto system = ahfl::json::JsonValue::make_object();
    system->set("role", ahfl::json::JsonValue::make_string("system"));
    system->set("content", ahfl::json::JsonValue::make_string(system_prompt));
    messages->push(std::move(system));

    auto user = ahfl::json::JsonValue::make_object();
    user->set("role", ahfl::json::JsonValue::make_string("user"));
    user->set("content", ahfl::json::JsonValue::make_string(user_prompt));
    messages->push(std::move(user));
    root->set("messages", std::move(messages));

    root->set("temperature", ahfl::json::JsonValue::make_float(config_.temperature));
    root->set("max_tokens", ahfl::json::JsonValue::make_int(config_.max_tokens));

    if (config_.json_mode) {
        auto format = ahfl::json::JsonValue::make_object();
        format->set("type", ahfl::json::JsonValue::make_string("json_object"));
        root->set("response_format", std::move(format));
    }

    return ahfl::json::serialize_json(*root);
}

std::string
LLMCapabilityProvider::extract_content_from_response(const std::string &response_body) const {
    auto parsed = ahfl::json::parse_json(response_body);
    if (!parsed.has_value() || !*parsed) {
        return "";
    }

    const auto *choices = (*parsed)->get("choices");
    if (choices == nullptr || !choices->is_array() || choices->array_items.empty()) {
        return "";
    }
    const auto *choice = choices->array_items.front().get();
    const auto *message = choice != nullptr ? choice->get("message") : nullptr;
    const auto *content = message != nullptr ? message->get("content") : nullptr;
    if (content == nullptr) {
        return "";
    }
    if (auto value = content->as_string(); value.has_value()) {
        return std::string(*value);
    }
    return "";
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
