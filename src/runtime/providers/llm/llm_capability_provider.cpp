// llm_capability_provider.cpp - LLM Capability Provider 组合模块

#include "runtime/providers/llm/llm_capability_provider.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "base/json/json_value.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::llm_provider {

namespace {

using ahfl::json::JsonValue;

/// Internal chat message representation for multi-turn conversations.
struct ChatMessage {
    std::string role;
    std::string content;
    std::vector<ToolCall> tool_calls; // only for assistant messages
    std::string tool_call_id;         // only for tool role messages
};

/// Build a JSON message object from a ChatMessage.
[[nodiscard]] std::unique_ptr<JsonValue> serialize_chat_message(const ChatMessage &msg) {
    auto obj = JsonValue::make_object();
    obj->set("role", JsonValue::make_string(msg.role));

    if (msg.role == "tool") {
        // Tool result messages have content and tool_call_id
        obj->set("content", JsonValue::make_string(msg.content));
        obj->set("tool_call_id", JsonValue::make_string(msg.tool_call_id));
    } else if (msg.role == "assistant" && !msg.tool_calls.empty()) {
        // Assistant message with tool_calls: content may be null or present
        if (!msg.content.empty()) {
            obj->set("content", JsonValue::make_string(msg.content));
        } else {
            obj->set("content", JsonValue::make_null());
        }
        auto tc_array = JsonValue::make_array();
        for (const auto &tc : msg.tool_calls) {
            auto tc_obj = JsonValue::make_object();
            tc_obj->set("id", JsonValue::make_string(tc.id));
            tc_obj->set("type", JsonValue::make_string("function"));
            auto fn_obj = JsonValue::make_object();
            fn_obj->set("name", JsonValue::make_string(tc.name));
            fn_obj->set("arguments", JsonValue::make_string(tc.arguments_json));
            tc_obj->set("function", std::move(fn_obj));
            tc_array->push(std::move(tc_obj));
        }
        obj->set("tool_calls", std::move(tc_array));
    } else {
        // system / user / plain assistant messages
        obj->set("content", JsonValue::make_string(msg.content));
    }

    return obj;
}

} // namespace

LLMCapabilityProvider::LLMCapabilityProvider(const ir::Program &program, LLMProviderConfig config)
    : program_(program), index_(program_), config_(std::move(config)),
      client_(config_.endpoint, config_.api_key, config_.timeout_seconds),
      prompt_builder_(program_), response_parser_(program_) {}

void LLMCapabilityProvider::set_tools(std::vector<ToolDefinition> tools, ToolExecutor executor) {
    tools_ = std::move(tools);
    tool_executor_ = std::move(executor);
}

std::string LLMCapabilityProvider::build_request_json(const std::string &system_prompt,
                                                      const std::string &user_prompt) const {
    auto root = JsonValue::make_object();
    root->set("model", JsonValue::make_string(config_.model));

    auto messages = JsonValue::make_array();
    auto system = JsonValue::make_object();
    system->set("role", JsonValue::make_string("system"));
    system->set("content", JsonValue::make_string(system_prompt));
    messages->push(std::move(system));

    auto user = JsonValue::make_object();
    user->set("role", JsonValue::make_string("user"));
    user->set("content", JsonValue::make_string(user_prompt));
    messages->push(std::move(user));
    root->set("messages", std::move(messages));

    root->set("temperature", JsonValue::make_float(config_.temperature));
    root->set("max_tokens", JsonValue::make_int(config_.max_tokens));

    if (config_.json_mode) {
        auto format = JsonValue::make_object();
        format->set("type", JsonValue::make_string("json_object"));
        root->set("response_format", std::move(format));
    }

    return ahfl::json::serialize_json(*root);
}

std::string LLMCapabilityProvider::build_full_request_json(
    const std::vector<std::pair<std::string, std::string>> & /*messages*/) const {
    // This overload exists for the interface but the tool-calling loop uses
    // the ChatMessage-based internal builder below. Kept for ABI compat.
    return {};
}

LLMResponseContent
LLMCapabilityProvider::extract_response(const std::string &response_body) const {
    LLMResponseContent result;
    auto parsed = ahfl::json::parse_json(response_body);
    if (!parsed.has_value() || !*parsed) {
        return result;
    }

    const auto *choices = (*parsed)->get("choices");
    if (choices == nullptr || !choices->is_array() || choices->array_items.empty()) {
        return result;
    }
    const auto *choice = choices->array_items.front().get();
    const auto *message = choice != nullptr ? choice->get("message") : nullptr;
    if (message == nullptr) {
        return result;
    }

    // Extract content
    const auto *content = message->get("content");
    if (content != nullptr) {
        if (auto value = content->as_string(); value.has_value()) {
            result.content = std::string(*value);
        }
    }

    // Extract tool_calls
    const auto *tool_calls_node = message->get("tool_calls");
    if (tool_calls_node != nullptr && tool_calls_node->is_array()) {
        for (const auto &item : tool_calls_node->array_items) {
            if (item == nullptr || !item->is_object()) {
                continue;
            }
            ToolCall call;
            // Extract id
            if (const auto *id_node = item->get("id"); id_node != nullptr) {
                if (auto id_str = id_node->as_string(); id_str.has_value()) {
                    call.id = std::string(*id_str);
                }
            }
            // Extract function.name and function.arguments
            const auto *function = item->get("function");
            if (function != nullptr && function->is_object()) {
                if (const auto *name_node = function->get("name"); name_node != nullptr) {
                    if (auto name_str = name_node->as_string(); name_str.has_value()) {
                        call.name = std::string(*name_str);
                    }
                }
                if (const auto *args_node = function->get("arguments"); args_node != nullptr) {
                    if (auto args_str = args_node->as_string(); args_str.has_value()) {
                        call.arguments_json = std::string(*args_str);
                    } else if (!args_node->is_null()) {
                        call.arguments_json = ahfl::json::serialize_json(*args_node);
                    }
                }
            }
            if (!call.name.empty()) {
                result.tool_calls.push_back(std::move(call));
            }
        }
    }

    return result;
}

std::string
LLMCapabilityProvider::extract_content_from_response(const std::string &response_body) const {
    return extract_response(response_body).content;
}

runtime::CapabilityCallResult
LLMCapabilityProvider::invoke(const std::string &capability_name,
                              const std::vector<evaluator::Value> &args) {
    // 查找 capability 的返回类型
    std::string return_type;
    if (const auto *capability = index_.find_capability(capability_name); capability != nullptr) {
        return_type = std::string(ir::type_canonical_name(capability->return_type_ref, "Any"));
    }

    if (return_type.empty()) {
        return runtime::CapabilityCallResult{
            .status = runtime::CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "capability not found: " + capability_name,
            .attempts = 0,
        };
    }

    // 构建 prompt
    std::string system_prompt = prompt_builder_.build_system_prompt(capability_name);
    std::string user_prompt = prompt_builder_.build_user_prompt(capability_name, args);

    // --- Non-tool path: original behavior when no tools are configured ---
    if (tools_.empty()) {
        std::string request_json = build_request_json(system_prompt, user_prompt);

        // 带重试的 HTTP 调用
        HttpResponse response;
        int attempts = 0;
        for (int attempt = 1; attempt <= config_.max_retries + 1; ++attempt) {
            attempts = attempt;
            response = client_.chat_completions(request_json);
            if (response.success()) {
                break;
            }
        }

        if (!response.success()) {
            return runtime::CapabilityCallResult{
                .status = config_.max_retries > 0 ? runtime::CapabilityCallStatus::RetryExhausted
                                                  : runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message =
                    "HTTP request failed, status=" + std::to_string(response.status_code),
                .attempts = static_cast<std::size_t>(attempts),
            };
        }

        std::string content = extract_content_from_response(response.body);
        if (content.empty()) {
            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "empty content in LLM response",
                .attempts = static_cast<std::size_t>(attempts),
            };
        }

        auto result = response_parser_.parse_with_diagnostics(content, return_type);
        if (!result.success()) {
            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "failed to parse LLM response for type '" + return_type +
                                 "': " + result.error_message,
                .attempts = static_cast<std::size_t>(attempts),
            };
        }

        return runtime::CapabilityCallResult{
            .status = runtime::CapabilityCallStatus::Success,
            .value = std::move(*result.value),
            .error_message = {},
            .attempts = static_cast<std::size_t>(attempts),
        };
    }

    // --- Tool-calling path ---
    // Build conversation as a vector of ChatMessages
    std::vector<ChatMessage> conversation;
    conversation.push_back(ChatMessage{.role = "system", .content = system_prompt});
    conversation.push_back(ChatMessage{.role = "user", .content = user_prompt});

    int total_attempts = 0;

    for (int round = 0; round < config_.max_tool_rounds; ++round) {
        // Build request JSON with tools and full conversation
        auto root = JsonValue::make_object();
        root->set("model", JsonValue::make_string(config_.model));

        auto messages_json = JsonValue::make_array();
        for (const auto &msg : conversation) {
            messages_json->push(serialize_chat_message(msg));
        }
        root->set("messages", std::move(messages_json));

        root->set("temperature", JsonValue::make_float(config_.temperature));
        root->set("max_tokens", JsonValue::make_int(config_.max_tokens));

        // Add tools
        auto tool_array = JsonValue::make_array();
        for (const auto &tool : tools_) {
            auto function = JsonValue::make_object();
            function->set("name", JsonValue::make_string(tool.name));
            function->set("description", JsonValue::make_string(tool.description));

            // Parse parameters schema
            auto params_parsed = ahfl::json::parse_json(tool.params_schema_json);
            if (params_parsed.has_value() && *params_parsed) {
                function->set("parameters", std::move(*params_parsed));
            } else {
                function->set("parameters", JsonValue::make_string(tool.params_schema_json));
            }

            auto tool_object = JsonValue::make_object();
            tool_object->set("type", JsonValue::make_string("function"));
            tool_object->set("function", std::move(function));
            tool_array->push(std::move(tool_object));
        }
        root->set("tools", std::move(tool_array));

        // Add tool_choice
        if (config_.tool_choice == "auto" || config_.tool_choice == "none") {
            root->set("tool_choice", JsonValue::make_string(config_.tool_choice));
        } else {
            // Specific tool name: {"type": "function", "function": {"name": "..."}}
            auto tc_obj = JsonValue::make_object();
            tc_obj->set("type", JsonValue::make_string("function"));
            auto fn_name_obj = JsonValue::make_object();
            fn_name_obj->set("name", JsonValue::make_string(config_.tool_choice));
            tc_obj->set("function", std::move(fn_name_obj));
            root->set("tool_choice", std::move(tc_obj));
        }

        if (config_.json_mode && round == 0) {
            // Only set json_mode on initial request; tool-calling responses
            // may not conform to JSON format constraint during intermediate rounds.
        }

        std::string request_json = ahfl::json::serialize_json(*root);

        // Send with retry
        HttpResponse response;
        for (int attempt = 1; attempt <= config_.max_retries + 1; ++attempt) {
            ++total_attempts;
            response = client_.chat_completions(request_json);
            if (response.success()) {
                break;
            }
        }

        if (!response.success()) {
            return runtime::CapabilityCallResult{
                .status = config_.max_retries > 0 ? runtime::CapabilityCallStatus::RetryExhausted
                                                  : runtime::CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message =
                    "HTTP request failed, status=" + std::to_string(response.status_code),
                .attempts = static_cast<std::size_t>(total_attempts),
            };
        }

        // Parse structured response
        LLMResponseContent llm_response = extract_response(response.body);

        if (!llm_response.has_tool_calls()) {
            // No tool calls: final answer
            if (llm_response.content.empty()) {
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "empty content in LLM response",
                    .attempts = static_cast<std::size_t>(total_attempts),
                };
            }

            auto result =
                response_parser_.parse_with_diagnostics(llm_response.content, return_type);
            if (!result.success()) {
                return runtime::CapabilityCallResult{
                    .status = runtime::CapabilityCallStatus::Error,
                    .value = std::nullopt,
                    .error_message = "failed to parse LLM response for type '" + return_type +
                                     "': " + result.error_message,
                    .attempts = static_cast<std::size_t>(total_attempts),
                };
            }

            return runtime::CapabilityCallResult{
                .status = runtime::CapabilityCallStatus::Success,
                .value = std::move(*result.value),
                .error_message = {},
                .attempts = static_cast<std::size_t>(total_attempts),
            };
        }

        // Has tool calls: execute them and continue the loop
        // Append the assistant message with tool_calls to conversation
        conversation.push_back(ChatMessage{
            .role = "assistant",
            .content = llm_response.content,
            .tool_calls = llm_response.tool_calls,
        });

        // Execute each tool and append results
        for (const auto &tool_call : llm_response.tool_calls) {
            ToolCallResult tool_result = tool_executor_(tool_call);
            conversation.push_back(ChatMessage{
                .role = "tool",
                .content = tool_result.content,
                .tool_calls = {},
                .tool_call_id = tool_result.tool_call_id,
            });
        }
    }

    // Exceeded max_tool_rounds
    return runtime::CapabilityCallResult{
        .status = runtime::CapabilityCallStatus::Error,
        .value = std::nullopt,
        .error_message = "exceeded max tool call rounds (" +
                         std::to_string(config_.max_tool_rounds) + ")",
        .attempts = static_cast<std::size_t>(total_attempts),
    };
}

void LLMCapabilityProvider::register_all(runtime::CapabilityRegistry &registry) {
    // 遍历所有 CapabilityDecl，注册到 registry
    for (const auto *capability : index_.capabilities()) {
        std::string cap_name = capability->name;
        runtime::CapabilityBinding binding;
        binding.name = cap_name;
        binding.handler = [this, cap_name](const std::vector<evaluator::Value> &args) {
            return this->invoke(cap_name, args);
        };
        registry.register_capability(std::move(binding));
    }
}

} // namespace ahfl::llm_provider
