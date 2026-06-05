// llm_provider.cpp - LLM Provider 单元测试
//
// 验证 PromptBuilder、ResponseParser、LLMProviderConfig、LLMCapabilityProvider 的功能

#include "ahfl/compiler/ir/ir.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/providers/llm/http_client.hpp"
#include "runtime/providers/llm/llm_capability_provider.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"
#include "runtime/providers/llm/prompt_builder.hpp"
#include "runtime/providers/llm/response_parser.hpp"
#include "runtime/providers/llm/tool_calling.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::ir;
using namespace ahfl::llm_provider;
using namespace ahfl::runtime;

// 测试统计
int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

ir::TypeRef make_type_ref(const std::string &name) {
    ir::TypeRefKind kind = ir::TypeRefKind::Struct;
    if (name == "String") {
        kind = ir::TypeRefKind::String;
    } else if (name == "Bool") {
        kind = ir::TypeRefKind::Bool;
    } else if (name == "Int") {
        kind = ir::TypeRefKind::Int;
    } else if (name == "Float") {
        kind = ir::TypeRefKind::Float;
    } else if (name == "Category") {
        kind = ir::TypeRefKind::Enum;
    }

    return ir::TypeRef{
        .kind = kind,
        .display_name = name,
        .canonical_name = name,
    };
}

// ============================================================================
// 构建测试用 IR Program（模拟 ClassifyMessage capability）
// ============================================================================

ir::Program build_test_program() {
    ir::Program program;
    program.format_version = std::string(kFormatVersion);

    // 枚举: Category { General, Technical, Billing }
    ir::EnumDecl category_enum;
    category_enum.name = "Category";
    category_enum.variants = {"General", "Technical", "Billing"};
    program.declarations.push_back(std::move(category_enum));

    // 结构体: ClassifyResult { category: Category, confidence: String }
    ir::StructDecl classify_result;
    classify_result.name = "ClassifyResult";
    classify_result.fields.push_back(ir::FieldDecl{
        .name = "category",
        .default_value = nullptr,
        .type_ref = make_type_ref("Category"),
        .source_range = {},
    });
    classify_result.fields.push_back(ir::FieldDecl{
        .name = "confidence",
        .default_value = nullptr,
        .type_ref = make_type_ref("String"),
        .source_range = {},
    });
    program.declarations.push_back(std::move(classify_result));

    // Capability: ClassifyMessage(message: String) -> ClassifyResult
    ir::CapabilityDecl classify_cap;
    classify_cap.name = "ClassifyMessage";
    classify_cap.params.push_back(ir::ParamDecl{
        .name = "message",
        .type_ref = make_type_ref("String"),
        .source_range = {},
    });
    classify_cap.return_type_ref = make_type_ref("ClassifyResult");
    program.declarations.push_back(std::move(classify_cap));

    // 结构体: SupportResult { response: String, resolved: Bool }
    ir::StructDecl support_result;
    support_result.name = "SupportResult";
    support_result.fields.push_back(ir::FieldDecl{
        .name = "response",
        .default_value = nullptr,
        .type_ref = make_type_ref("String"),
        .source_range = {},
    });
    support_result.fields.push_back(ir::FieldDecl{
        .name = "resolved",
        .default_value = nullptr,
        .type_ref = make_type_ref("Bool"),
        .source_range = {},
    });
    program.declarations.push_back(std::move(support_result));

    // Capability: HandleTechnical(issue: String) -> SupportResult
    ir::CapabilityDecl handle_tech;
    handle_tech.name = "HandleTechnical";
    handle_tech.params.push_back(ir::ParamDecl{
        .name = "issue",
        .type_ref = make_type_ref("String"),
        .source_range = {},
    });
    handle_tech.return_type_ref = make_type_ref("SupportResult");
    program.declarations.push_back(std::move(handle_tech));

    return program;
}

// ============================================================================
// Test: PromptBuilder
// ============================================================================

void test_prompt_builder() {
    auto program = build_test_program();
    PromptBuilder builder(program);

    // system prompt 应包含 capability 名和返回类型描述
    std::string system = builder.build_system_prompt("ClassifyMessage");
    check(system.find("ClassifyMessage") != std::string::npos,
          "prompt_builder.system_prompt_contains_capability_name");
    check(system.find("category") != std::string::npos,
          "prompt_builder.system_prompt_contains_field_name");
    check(system.find("General") != std::string::npos,
          "prompt_builder.system_prompt_contains_enum_variant");
    check(system.find("Technical") != std::string::npos,
          "prompt_builder.system_prompt_contains_enum_variant_2");
    check(system.find("JSON") != std::string::npos, "prompt_builder.system_prompt_mentions_json");

    // user prompt 应包含参数值
    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    std::string user = builder.build_user_prompt("ClassifyMessage", args);
    check(user.find("My server is down") != std::string::npos,
          "prompt_builder.user_prompt_contains_arg_value");
    check(user.find("message") != std::string::npos,
          "prompt_builder.user_prompt_contains_param_name");

    // 不存在的 capability
    std::string unknown = builder.build_system_prompt("UnknownCapability");
    check(!unknown.empty(), "prompt_builder.unknown_capability_returns_default");
}

// ============================================================================
// Test: ResponseParser - 解析 struct
// ============================================================================

void test_response_parser_struct() {
    auto program = build_test_program();
    ResponseParser parser(program);

    // 解析有效 JSON → ClassifyResult
    std::string json = R"({"category": "Technical", "confidence": "high"})";
    auto result = parser.parse(json, "ClassifyResult");

    check(result.has_value(), "response_parser.struct_parse_success");
    if (result.has_value()) {
        auto *sv = std::get_if<StructValue>(&result->node);
        check(sv != nullptr, "response_parser.struct_is_struct_value");
        if (sv != nullptr) {
            check(sv->type_name == "ClassifyResult", "response_parser.struct_type_name");

            // 检查 category 字段
            auto it_cat = sv->fields.find("category");
            check(it_cat != sv->fields.end(), "response_parser.struct_has_category");
            if (it_cat != sv->fields.end() && it_cat->second) {
                auto *ev = std::get_if<EnumValue>(&it_cat->second->node);
                check(ev != nullptr, "response_parser.category_is_enum");
                if (ev != nullptr) {
                    check(ev->enum_name == "Category", "response_parser.category_enum_name");
                    check(ev->variant == "Technical", "response_parser.category_variant");
                }
            }

            // 检查 confidence 字段
            auto it_conf = sv->fields.find("confidence");
            check(it_conf != sv->fields.end(), "response_parser.struct_has_confidence");
            if (it_conf != sv->fields.end() && it_conf->second) {
                auto *str = std::get_if<StringValue>(&it_conf->second->node);
                check(str != nullptr, "response_parser.confidence_is_string");
                if (str != nullptr) {
                    check(str->value == "high", "response_parser.confidence_value");
                }
            }
        }
    }
}

// ============================================================================
// Test: ResponseParser - 解析枚举
// ============================================================================

void test_response_parser_enum() {
    auto program = build_test_program();
    ResponseParser parser(program);

    auto result = parser.parse("Technical", "Category");
    check(result.has_value(), "response_parser.enum_parse_success");
    if (result.has_value()) {
        auto *ev = std::get_if<EnumValue>(&result->node);
        check(ev != nullptr, "response_parser.enum_is_enum_value");
        if (ev != nullptr) {
            check(ev->enum_name == "Category", "response_parser.enum_name_correct");
            check(ev->variant == "Technical", "response_parser.enum_variant_correct");
        }
    }
}

void test_response_parser_rejects_invalid_enum() {
    auto program = build_test_program();
    ResponseParser parser(program);

    auto result = parser.parse("Security", "Category");
    check(!result.has_value(), "response_parser.invalid_enum_rejected");

    auto diagnostic_result = parser.parse_with_diagnostics("Security", "Category");
    check(!diagnostic_result.success(), "response_parser.invalid_enum_diagnostic_failed");
    check(diagnostic_result.error_message.find("Security") != std::string::npos,
          "response_parser.invalid_enum_diagnostic_has_value");
    check(diagnostic_result.error_message.find("Category") != std::string::npos,
          "response_parser.invalid_enum_diagnostic_has_type");
}

void test_response_parser_rejects_missing_struct_field() {
    auto program = build_test_program();
    ResponseParser parser(program);

    std::string json = R"({"category": "Technical"})";
    auto result = parser.parse(json, "ClassifyResult");
    check(!result.has_value(), "response_parser.missing_field_rejected");

    auto diagnostic_result = parser.parse_with_diagnostics(json, "ClassifyResult");
    check(!diagnostic_result.success(), "response_parser.missing_field_diagnostic_failed");
    check(diagnostic_result.error_message.find("confidence") != std::string::npos,
          "response_parser.missing_field_diagnostic_has_field");
}

// ============================================================================
// Test: ResponseParser - 解析 bool 字段
// ============================================================================

void test_response_parser_bool_field() {
    auto program = build_test_program();
    ResponseParser parser(program);

    std::string json = R"({"response": "Fixed the issue", "resolved": true})";
    auto result = parser.parse(json, "SupportResult");
    check(result.has_value(), "response_parser.bool_field_parse_success");
    if (result.has_value()) {
        auto *sv = std::get_if<StructValue>(&result->node);
        check(sv != nullptr, "response_parser.bool_field_is_struct");
        if (sv != nullptr) {
            auto it = sv->fields.find("resolved");
            check(it != sv->fields.end(), "response_parser.bool_field_exists");
            if (it != sv->fields.end() && it->second) {
                auto *bv = std::get_if<BoolValue>(&it->second->node);
                check(bv != nullptr, "response_parser.bool_field_is_bool");
                if (bv != nullptr) {
                    check(bv->value == true, "response_parser.bool_field_value_true");
                }
            }
        }
    }
}

void test_tool_request_json_uses_json_dom() {
    std::vector<ToolDefinition> tools;
    tools.push_back(ToolDefinition{
        .name = "lookup",
        .description = "Lookup \"quoted\" values\nsafely",
        .params_schema_json = R"({"type":"object","properties":{"query":{"type":"string"}}})",
    });

    const auto request =
        build_tool_request_json("system \"prompt\"", "user\nprompt", "model-x", tools);
    auto parsed = ahfl::json::parse_json(request);
    check(parsed.has_value() && *parsed && (*parsed)->is_object(),
          "tool_request.json_parse_success");
    if (!parsed.has_value() || !*parsed) {
        return;
    }
    const auto *model = (*parsed)->get("model");
    check(model != nullptr && model->as_string().has_value() && *model->as_string() == "model-x",
          "tool_request.model_field");
    const auto *tool_array = (*parsed)->get("tools");
    check(tool_array != nullptr && tool_array->is_array() && tool_array->array_items.size() == 1,
          "tool_request.tools_array");
}

void test_parse_tool_calls_uses_json_dom() {
    const std::string response = R"JSON({
        "choices": [
            {
                "message": {
                    "tool_calls": [
                        {
                            "id": "call_1",
                            "type": "function",
                            "function": {
                                "name": "lookup",
                                "arguments": "{\"query\":\"hello \\\"world\\\"\",\"limit\":2}"
                            }
                        },
                        {
                            "id": "call_2",
                            "type": "function",
                            "function": {
                                "name": "toggle",
                                "arguments": {"enabled": true}
                            }
                        }
                    ]
                }
            }
        ]
    })JSON";

    const auto calls = parse_tool_calls(response);
    check(calls.size() == 2, "tool_calls.count");
    if (calls.size() != 2) {
        return;
    }
    check(calls[0].id == "call_1", "tool_calls.first_id");
    check(calls[0].name == "lookup", "tool_calls.first_name");
    check(calls[0].arguments_json == R"({"query":"hello \"world\"","limit":2})",
          "tool_calls.first_arguments");
    check(calls[1].id == "call_2", "tool_calls.second_id");
    check(calls[1].name == "toggle", "tool_calls.second_name");
    check(calls[1].arguments_json == R"({"enabled":true})", "tool_calls.second_arguments");
}

// ============================================================================
// Test: LLMProviderConfig - 环境变量展开
// ============================================================================

void test_config_env_expansion() {
    // 设置测试环境变量
    setenv("AHFL_TEST_API_KEY", "sk-test-12345", 1);
    setenv("AHFL_TEST_ENDPOINT", "https://api.example.com/v1", 1);

    // 测试 expand_env_vars
    std::string input = "key=${AHFL_TEST_API_KEY}, url=${AHFL_TEST_ENDPOINT}";
    std::string expanded = expand_env_vars(input);
    check(expanded.find("sk-test-12345") != std::string::npos, "config.env_expansion_api_key");
    check(expanded.find("https://api.example.com/v1") != std::string::npos,
          "config.env_expansion_endpoint");
    check(expanded.find("${") == std::string::npos, "config.env_expansion_no_remaining_vars");

    // 未定义的环境变量应被清除
    std::string with_unknown = "prefix_${AHFL_UNDEFINED_VAR_12345}_suffix";
    std::string result = expand_env_vars(with_unknown);
    check(result == "prefix__suffix", "config.env_expansion_undefined_var_cleared");

    // 测试 load_config
    std::string json = R"({
        "endpoint": "${AHFL_TEST_ENDPOINT}",
        "model": "glm-4",
        "api_key": "${AHFL_TEST_API_KEY}",
        "temperature": 0.2,
        "max_tokens": 2048,
        "timeout_seconds": 60,
        "max_retries": 3
    })";
    auto config = load_config(json);
    check(config.endpoint == "https://api.example.com/v1", "config.load_endpoint");
    check(config.model == "glm-4", "config.load_model");
    check(config.api_key == "sk-test-12345", "config.load_api_key");
    check(config.temperature == 0.2, "config.load_temperature");
    check(config.max_tokens == 2048, "config.load_max_tokens");
    check(config.timeout_seconds == 60, "config.load_timeout");
    check(config.max_retries == 3, "config.load_max_retries");

    // 清理
    unsetenv("AHFL_TEST_API_KEY");
    unsetenv("AHFL_TEST_ENDPOINT");
}

// ============================================================================
// Test: LLMCapabilityProvider 端到端（使用 mock HTTP）
// ============================================================================

void test_llm_capability_provider_register() {
    // 验证 register_all 将 capability 注册到 registry
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "http://localhost:9999";
    config.model = "test-model";
    config.api_key = "test-key";

    LLMCapabilityProvider provider(program, config);
    CapabilityRegistry registry;
    provider.register_all(registry);

    // 验证 capability 已注册
    check(registry.has("ClassifyMessage"), "provider.register_classify_message");
    check(registry.has("HandleTechnical"), "provider.register_handle_technical");
}

void test_llm_capability_provider_unknown_capability_returns_error() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "http://localhost:9999";
    config.model = "test-model";
    config.api_key = "test-key";

    LLMCapabilityProvider provider(program, config);
    auto result = provider.invoke("UnknownCapability", {});

    check(result.status == CapabilityCallStatus::Error, "provider.unknown.status_error");
    check(!result.value.has_value(), "provider.unknown.no_value");
    check(!result.error_message.empty(), "provider.unknown.has_error_message");
}

} // namespace

int main() {
    test_prompt_builder();
    test_response_parser_struct();
    test_response_parser_enum();
    test_response_parser_rejects_invalid_enum();
    test_response_parser_rejects_missing_struct_field();
    test_response_parser_bool_field();
    test_tool_request_json_uses_json_dom();
    test_parse_tool_calls_uses_json_dom();
    test_config_env_expansion();
    test_llm_capability_provider_register();
    test_llm_capability_provider_unknown_capability_returns_error();

    std::cout << "\n=== LLM Provider Tests ===\n";
    std::cout << pass_count << "/" << test_count << " passed\n";

    return (pass_count == test_count) ? 0 : 1;
}
