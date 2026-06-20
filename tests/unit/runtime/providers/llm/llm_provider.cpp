// llm_provider.cpp - LLM Provider unit tests
//
// Validates the functionality of PromptBuilder, ResponseParser, LLMProviderConfig, and LLMCapabilityProvider

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

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::ir;
using namespace ahfl::llm_provider;
using namespace ahfl::runtime;

// Test statistics
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

bool has_header(const std::vector<std::pair<std::string, std::string>> &headers,
                const std::string &name,
                const std::string &value) {
    for (const auto &[header_name, header_value] : headers) {
        if (header_name == name && header_value == value) {
            return true;
        }
    }
    return false;
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
        .string_bounds = {},
        .decimal_scale = {},
        .first = nullptr,
        .second = nullptr,
    };
}

// ============================================================================
// Build a test IR Program (simulating the ClassifyMessage capability)
// ============================================================================

ir::Program build_test_program() {
    ir::Program program;
    program.format_version = std::string(kFormatVersion);

    // Enum: Category { General, Technical, Billing }
    ir::EnumDecl category_enum;
    category_enum.name = "Category";
    category_enum.variants = {"General", "Technical", "Billing"};
    program.declarations.push_back(std::move(category_enum));

    // Struct: ClassifyResult { category: Category, confidence: String }
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

    // Struct: SupportResult { response: String, resolved: Bool }
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

    // system prompt should contain the capability name and return type description
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

    // user prompt should contain argument values
    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    std::string user = builder.build_user_prompt("ClassifyMessage", args);
    check(user.find("My server is down") != std::string::npos,
          "prompt_builder.user_prompt_contains_arg_value");
    check(user.find("message") != std::string::npos,
          "prompt_builder.user_prompt_contains_param_name");

    // Non-existent capability
    std::string unknown = builder.build_system_prompt("UnknownCapability");
    check(!unknown.empty(), "prompt_builder.unknown_capability_returns_default");
}

// ============================================================================
// Test: ResponseParser - parse struct
// ============================================================================

void test_response_parser_struct() {
    auto program = build_test_program();
    ResponseParser parser(program);

    // Parse valid JSON -> ClassifyResult
    std::string json = R"({"category": "Technical", "confidence": "high"})";
    auto result = parser.parse(json, "ClassifyResult");

    check(result.has_value(), "response_parser.struct_parse_success");
    if (result.has_value()) {
        auto *sv = std::get_if<StructValue>(&result->node);
        check(sv != nullptr, "response_parser.struct_is_struct_value");
        if (sv != nullptr) {
            check(sv->type_name == "ClassifyResult", "response_parser.struct_type_name");

            // Check the category field
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

            // Check the confidence field
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
// Test: ResponseParser - parse enum
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
// Test: ResponseParser - parse bool field
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
// Test: LLMProviderConfig - environment variable expansion
// ============================================================================

void test_config_env_expansion() {
    // Set test environment variables
    setenv("AHFL_TEST_API_KEY", "sk-test-12345", 1);
    setenv("AHFL_TEST_ENDPOINT", "https://api.example.com/v1", 1);

    // Test expand_env_vars
    std::string input = "key=${AHFL_TEST_API_KEY}, url=${AHFL_TEST_ENDPOINT}";
    std::string expanded = expand_env_vars(input);
    check(expanded.find("sk-test-12345") != std::string::npos, "config.env_expansion_api_key");
    check(expanded.find("https://api.example.com/v1") != std::string::npos,
          "config.env_expansion_endpoint");
    check(expanded.find("${") == std::string::npos, "config.env_expansion_no_remaining_vars");

    // Undefined environment variables should be cleared
    std::string with_unknown = "prefix_${AHFL_UNDEFINED_VAR_12345}_suffix";
    std::string result = expand_env_vars(with_unknown);
    check(result == "prefix__suffix", "config.env_expansion_undefined_var_cleared");

    // Test load_config
    std::string json = R"({
        "endpoint": "${AHFL_TEST_ENDPOINT}",
        "model": "glm-4",
        "api_key": "${AHFL_TEST_API_KEY}",
        "oauth2_token_secret": "env:AHFL_OAUTH2_TOKEN",
        "mtls_client_cert_path": "/tmp/ahfl-client.pem",
        "mtls_client_key_path": "/tmp/ahfl-client-key.pem",
        "mtls_ca_cert_path": "/tmp/ahfl-ca.pem",
        "mtls_verify_tls": false,
        "auth_scheme": "api_key_header",
        "auth_header": "x-api-key",
        "temperature": 0.2,
        "max_tokens": 2048,
        "max_prompt_tokens": 3000,
        "max_total_tokens": 6000,
        "prompt_token_cost_per_million": 0.25,
        "completion_token_cost_per_million": 1.25,
        "max_total_cost_usd": 0.5,
        "max_workflow_total_tokens": 12000,
        "max_node_total_tokens": 8000,
        "max_workflow_total_cost_usd": 0.75,
        "max_node_total_cost_usd": 0.25,
        "token_budget_policy": "warn",
        "capability_token_budgets": [
          {
            "capability": "ClassifyMessage",
            "max_tokens": 512,
            "max_prompt_tokens": 2048,
            "max_total_tokens": 4096,
            "max_total_cost_usd": 0.1,
            "policy": "fail"
          }
        ],
        "stream": true,
        "timeout_seconds": 60,
        "max_retries": 3,
        "response_cache_enabled": true,
        "response_cache_max_entries": 4,
        "response_cache_ttl_seconds": 30,
        "response_cache_path": "/tmp/ahfl-llm-response-cache.json",
        "refresh_secrets_before_use": true,
        "tool_choice": "lookup",
        "max_tool_rounds": 2,
        "fallback_providers": [
          {
            "name": "backup",
            "endpoint": "https://backup.example.com/v1",
            "model": "glm-4-backup",
            "api_key_secret": "AHFL_BACKUP_LLM_API_KEY",
            "oauth2_token_secret": "env:AHFL_BACKUP_OAUTH2_TOKEN",
            "mtls_client_cert_path": "/tmp/ahfl-backup-client.pem",
            "mtls_client_key_path": "/tmp/ahfl-backup-client-key.pem",
            "mtls_ca_cert_path": "/tmp/ahfl-backup-ca.pem",
            "mtls_verify_tls": false,
            "auth_scheme": "bearer",
            "auth_header": "Authorization",
            "priority": 10
          }
        ]
    })";
    auto config = load_config(json);
    check(config.endpoint == "https://api.example.com/v1", "config.load_endpoint");
    check(config.model == "glm-4", "config.load_model");
    check(config.api_key == "sk-test-12345", "config.load_api_key");
    check(config.oauth2_token_secret == "env:AHFL_OAUTH2_TOKEN", "config.load_oauth2_token_secret");
    check(config.mtls_client_cert_path == "/tmp/ahfl-client.pem", "config.load_mtls_cert_path");
    check(config.mtls_client_key_path == "/tmp/ahfl-client-key.pem", "config.load_mtls_key_path");
    check(config.mtls_ca_cert_path == "/tmp/ahfl-ca.pem", "config.load_mtls_ca_path");
    check(!config.mtls_verify_tls, "config.load_mtls_verify_tls");
    check(config.auth_scheme == "api_key_header", "config.load_auth_scheme");
    check(config.auth_header == "x-api-key", "config.load_auth_header");
    check(config.temperature == 0.2, "config.load_temperature");
    check(config.max_tokens == 2048, "config.load_max_tokens");
    check(config.max_prompt_tokens == 3000, "config.load_max_prompt_tokens");
    check(config.max_total_tokens == 6000, "config.load_max_total_tokens");
    check(config.prompt_token_cost_per_million == 0.25, "config.load_prompt_token_cost");
    check(config.completion_token_cost_per_million == 1.25, "config.load_completion_token_cost");
    check(config.max_total_cost_usd == 0.5, "config.load_max_total_cost");
    check(config.max_workflow_total_tokens == 12000, "config.load_max_workflow_total_tokens");
    check(config.max_node_total_tokens == 8000, "config.load_max_node_total_tokens");
    check(config.max_workflow_total_cost_usd == 0.75, "config.load_max_workflow_total_cost");
    check(config.max_node_total_cost_usd == 0.25, "config.load_max_node_total_cost");
    check(config.token_budget_policy == "warn", "config.load_token_budget_policy");
    check(config.capability_token_budgets.size() == 1, "config.load_capability_budget_count");
    if (config.capability_token_budgets.size() == 1) {
        const auto &budget = config.capability_token_budgets[0];
        check(budget.capability_name == "ClassifyMessage", "config.load_capability_budget_name");
        check(budget.max_tokens.has_value() && *budget.max_tokens == 512,
              "config.load_capability_budget_max_tokens");
        check(budget.max_prompt_tokens.has_value() && *budget.max_prompt_tokens == 2048,
              "config.load_capability_budget_max_prompt_tokens");
        check(budget.max_total_tokens.has_value() && *budget.max_total_tokens == 4096,
              "config.load_capability_budget_max_total_tokens");
        check(budget.max_total_cost_usd.has_value() && *budget.max_total_cost_usd == 0.1,
              "config.load_capability_budget_max_cost");
        check(budget.policy.has_value() && *budget.policy == "fail",
              "config.load_capability_budget_policy");
    }
    check(config.stream, "config.load_stream");
    check(config.timeout_seconds == 60, "config.load_timeout");
    check(config.max_retries == 3, "config.load_max_retries");
    check(config.response_cache_enabled, "config.load_response_cache_enabled");
    check(config.response_cache_max_entries == 4, "config.load_response_cache_max_entries");
    check(config.response_cache_ttl_seconds == 30, "config.load_response_cache_ttl_seconds");
    check(config.response_cache_path == "/tmp/ahfl-llm-response-cache.json",
          "config.load_response_cache_path");
    check(config.refresh_secrets_before_use, "config.load_refresh_secrets_before_use");
    check(config.tool_choice == "lookup", "config.load_tool_choice");
    check(config.max_tool_rounds == 2, "config.load_max_tool_rounds");
    check(config.fallback_providers.size() == 1, "config.load_fallback_count");
    if (!config.fallback_providers.empty()) {
        check(config.fallback_providers[0].name == "backup", "config.load_fallback_name");
        check(config.fallback_providers[0].endpoint == "https://backup.example.com/v1",
              "config.load_fallback_endpoint");
        check(config.fallback_providers[0].model == "glm-4-backup", "config.load_fallback_model");
        check(config.fallback_providers[0].api_key_secret == "AHFL_BACKUP_LLM_API_KEY",
              "config.load_fallback_secret");
        check(config.fallback_providers[0].oauth2_token_secret == "env:AHFL_BACKUP_OAUTH2_TOKEN",
              "config.load_fallback_oauth2_token_secret");
        check(config.fallback_providers[0].mtls_client_cert_path == "/tmp/ahfl-backup-client.pem",
              "config.load_fallback_mtls_cert_path");
        check(config.fallback_providers[0].mtls_client_key_path ==
                  "/tmp/ahfl-backup-client-key.pem",
              "config.load_fallback_mtls_key_path");
        check(config.fallback_providers[0].mtls_ca_cert_path == "/tmp/ahfl-backup-ca.pem",
              "config.load_fallback_mtls_ca_path");
        check(!config.fallback_providers[0].mtls_verify_tls,
              "config.load_fallback_mtls_verify_tls");
        check(config.fallback_providers[0].mtls_verify_tls_set,
              "config.load_fallback_mtls_verify_tls_set");
        check(config.fallback_providers[0].auth_scheme == "bearer",
              "config.load_fallback_auth_scheme");
        check(config.fallback_providers[0].auth_header == "Authorization",
              "config.load_fallback_auth_header");
        check(config.fallback_providers[0].priority == 10, "config.load_fallback_priority");
    }
    check(!validate_config(config).has_value(), "config.validate_budget_ok");

    // Cleanup
    unsetenv("AHFL_TEST_API_KEY");
    unsetenv("AHFL_TEST_ENDPOINT");
}

void test_config_api_key_secret_handle() {
    const std::string json = R"({
        "endpoint": "https://api.example.com/v1",
        "model": "glm-4",
        "api_key_secret": "AHFL_LLM_API_KEY"
    })";

    auto config = load_config(json);
    check(config.endpoint == "https://api.example.com/v1", "config.secret_endpoint");
    check(config.model == "glm-4", "config.secret_model");
    check(config.api_key.empty(), "config.secret_does_not_materialize_api_key");
    check(config.api_key_secret == "AHFL_LLM_API_KEY", "config.secret_handle");
}

void test_config_secret_provider_chain() {
    const std::string json = R"({
        "endpoint": "https://api.example.com/v1",
        "model": "glm-4",
        "api_key_secret": "cloud:llm/api-key",
        "secret_providers": [
          {
            "kind": "env",
            "prefix": "env",
            "default_for_unqualified": true
          },
          {
            "kind": "vault",
            "prefix": "vault",
            "address": "https://vault.example.com",
            "token_env": "AHFL_VAULT_TOKEN",
            "mount_path": "kv",
            "namespace_path": "team-a",
            "timeout_seconds": 7,
            "verify_tls": false
          },
          {
            "kind": "cloud",
            "prefix": "cloud",
            "address": "https://secrets.example.com",
            "token_env": "AHFL_CLOUD_SECRET_TOKEN",
            "project": "agent-prod",
            "version": "42",
            "timeout_seconds": 3
          }
        ]
    })";

    auto config = load_config(json);
    check(config.api_key_secret == "cloud:llm/api-key", "config.secret_chain_handle");
    check(config.secret_providers.size() == 3, "config.secret_chain_provider_count");
    if (config.secret_providers.size() == 3) {
        check(config.secret_providers[0].kind == "env", "config.secret_chain_env_kind");
        check(config.secret_providers[0].prefix == "env", "config.secret_chain_env_prefix");
        check(config.secret_providers[0].default_for_unqualified,
              "config.secret_chain_env_default");
        check(config.secret_providers[1].kind == "vault", "config.secret_chain_vault_kind");
        check(config.secret_providers[1].prefix == "vault", "config.secret_chain_vault_prefix");
        check(config.secret_providers[1].address == "https://vault.example.com",
              "config.secret_chain_vault_address");
        check(config.secret_providers[1].token_env == "AHFL_VAULT_TOKEN",
              "config.secret_chain_vault_token_env");
        check(config.secret_providers[1].mount_path == "kv", "config.secret_chain_vault_mount");
        check(config.secret_providers[1].namespace_path == "team-a",
              "config.secret_chain_vault_namespace");
        check(config.secret_providers[1].timeout_seconds == 7, "config.secret_chain_vault_timeout");
        check(!config.secret_providers[1].verify_tls, "config.secret_chain_vault_verify_tls");
        check(!config.secret_providers[1].default_for_unqualified,
              "config.secret_chain_vault_not_default");
        check(config.secret_providers[2].kind == "cloud", "config.secret_chain_cloud_kind");
        check(config.secret_providers[2].prefix == "cloud", "config.secret_chain_cloud_prefix");
        check(config.secret_providers[2].address == "https://secrets.example.com",
              "config.secret_chain_cloud_address");
        check(config.secret_providers[2].token_env == "AHFL_CLOUD_SECRET_TOKEN",
              "config.secret_chain_cloud_token_env");
        check(config.secret_providers[2].project == "agent-prod",
              "config.secret_chain_cloud_project");
        check(config.secret_providers[2].version == "42", "config.secret_chain_cloud_version");
        check(config.secret_providers[2].timeout_seconds == 3, "config.secret_chain_cloud_timeout");
        check(!config.secret_providers[2].default_for_unqualified,
              "config.secret_chain_cloud_not_default");
    }
    check(!validate_config(config).has_value(), "config.secret_chain_validate_ok");
}

void test_config_rejects_invalid_secret_provider() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.api_key = "sk-test";
    config.max_tokens = 1024;
    config.max_prompt_tokens = 1024;
    config.max_total_tokens = 4096;
    config.secret_providers.push_back(SecretProviderConfig{
        .kind = "secret-manager",
        .prefix = "secret",
    });

    auto error = validate_config(config);
    check(error.has_value(), "config.invalid_secret_provider_kind_rejected");
    if (error.has_value()) {
        check(error->find("secret_providers kind") != std::string::npos,
              "config.invalid_secret_provider_mentions_kind");
    }

    config.secret_providers.clear();
    config.secret_providers.push_back(SecretProviderConfig{
        .kind = "env",
        .prefix = "env",
    });
    config.secret_providers.push_back(SecretProviderConfig{
        .kind = "vault",
        .prefix = "env",
        .address = "https://vault.example.com",
        .mount_path = "kv",
    });

    error = validate_config(config);
    check(error.has_value(), "config.duplicate_secret_provider_prefix_rejected");
    if (error.has_value()) {
        check(error->find("duplicated") != std::string::npos,
              "config.duplicate_secret_provider_mentions_prefix");
    }

    config.secret_providers.clear();
    config.secret_providers.push_back(SecretProviderConfig{
        .kind = "cloud",
        .prefix = "cloud",
        .address = "https://secrets.example.com",
    });

    error = validate_config(config);
    check(error.has_value(), "config.cloud_secret_provider_missing_token_rejected");
    if (error.has_value()) {
        check(error->find("requires token or token_env") != std::string::npos,
              "config.cloud_secret_provider_missing_token_message");
    }
}

void test_config_rejects_invalid_budget() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.api_key = "sk-test";
    config.max_tokens = 2048;
    config.max_prompt_tokens = 3072;
    config.max_total_tokens = 4096;

    auto error = validate_config(config);
    check(error.has_value(), "config.invalid_budget_rejected");
    if (error.has_value()) {
        check(error->find("max_prompt_tokens + max_tokens") != std::string::npos,
              "config.invalid_budget_mentions_fields");
    }

    config.max_tokens = 1024;
    config.max_prompt_tokens = 1024;
    config.prompt_token_cost_per_million = -0.1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_prompt_token_cost_rejected");
    if (error.has_value()) {
        check(error->find("prompt_token_cost_per_million") != std::string::npos,
              "config.invalid_prompt_token_cost_mentions_field");
    }

    config.prompt_token_cost_per_million = 0.0;
    config.completion_token_cost_per_million = -0.1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_completion_token_cost_rejected");
    if (error.has_value()) {
        check(error->find("completion_token_cost_per_million") != std::string::npos,
              "config.invalid_completion_token_cost_mentions_field");
    }

    config.completion_token_cost_per_million = 0.0;
    config.max_total_cost_usd = -0.1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_max_total_cost_rejected");
    if (error.has_value()) {
        check(error->find("max_total_cost_usd") != std::string::npos,
              "config.invalid_max_total_cost_mentions_field");
    }

    config.max_total_cost_usd = 0.0;
    config.max_workflow_total_tokens = -1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_workflow_token_budget_rejected");
    if (error.has_value()) {
        check(error->find("max_workflow_total_tokens") != std::string::npos,
              "config.invalid_workflow_token_budget_mentions_field");
    }

    config.max_workflow_total_tokens = 0;
    config.max_node_total_tokens = -1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_node_token_budget_rejected");
    if (error.has_value()) {
        check(error->find("max_node_total_tokens") != std::string::npos,
              "config.invalid_node_token_budget_mentions_field");
    }

    config.max_node_total_tokens = 0;
    config.max_workflow_total_cost_usd = -0.1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_workflow_cost_budget_rejected");
    if (error.has_value()) {
        check(error->find("max_workflow_total_cost_usd") != std::string::npos,
              "config.invalid_workflow_cost_budget_mentions_field");
    }

    config.max_workflow_total_cost_usd = 0.0;
    config.max_node_total_cost_usd = -0.1;
    error = validate_config(config);
    check(error.has_value(), "config.invalid_node_cost_budget_rejected");
    if (error.has_value()) {
        check(error->find("max_node_total_cost_usd") != std::string::npos,
              "config.invalid_node_cost_budget_mentions_field");
    }

    config.max_node_total_cost_usd = 0.0;
    config.token_budget_policy = "audit";
    error = validate_config(config);
    check(error.has_value(), "config.invalid_budget_policy_rejected");
    if (error.has_value()) {
        check(error->find("token_budget_policy") != std::string::npos,
              "config.invalid_budget_policy_mentions_field");
    }

    config.token_budget_policy = "fail";
    config.capability_token_budgets.push_back(LLMCapabilityTokenBudget{
        .capability_name = "ClassifyMessage",
        .max_tokens = 2048,
        .max_prompt_tokens = 3072,
        .max_total_tokens = 4096,
    });
    error = validate_config(config);
    check(error.has_value(), "config.invalid_capability_budget_rejected");
    if (error.has_value()) {
        check(error->find("capability_token_budgets") != std::string::npos,
              "config.invalid_capability_budget_mentions_field");
    }
}

void test_config_rejects_invalid_response_cache() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.api_key = "sk-test";
    config.max_tokens = 1024;
    config.max_prompt_tokens = 1024;
    config.max_total_tokens = 4096;
    config.response_cache_enabled = true;
    config.response_cache_max_entries = 0;

    auto error = validate_config(config);
    check(error.has_value(), "config.invalid_cache_rejected");
    if (error.has_value()) {
        check(error->find("response_cache_max_entries") != std::string::npos,
              "config.invalid_cache_mentions_field");
    }

    config.response_cache_max_entries = 8;
    config.response_cache_enabled = false;
    config.response_cache_path = "/tmp/ahfl-cache.json";
    error = validate_config(config);
    check(error.has_value(), "config.cache_path_requires_enabled_rejected");
    if (error.has_value()) {
        check(error->find("response_cache_path") != std::string::npos,
              "config.cache_path_requires_enabled_mentions_field");
    }
}

void test_config_rejects_invalid_auth_strategy() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.api_key = "sk-test";
    config.auth_scheme = "sigv4";

    auto error = validate_config(config);
    check(error.has_value(), "config.invalid_auth_scheme_rejected");
    if (error.has_value()) {
        check(error->find("auth_scheme") != std::string::npos,
              "config.invalid_auth_scheme_mentions_field");
    }

    config.auth_scheme = "api_key_header";
    config.auth_header.clear();
    error = validate_config(config);
    check(error.has_value(), "config.missing_auth_header_rejected");
    if (error.has_value()) {
        check(error->find("auth_header") != std::string::npos,
              "config.missing_auth_header_mentions_field");
    }
}

void test_config_accepts_mtls_auth_strategy() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.auth_scheme = "mtls";
    config.auth_header.clear();
    config.api_key.clear();
    config.mtls_client_cert_path = "/tmp/client.pem";
    config.mtls_client_key_path = "/tmp/client-key.pem";

    auto error = validate_config(config);
    check(!error.has_value(), "config.mtls_valid");

    config.mtls_client_key_path.clear();
    error = validate_config(config);
    check(error.has_value(), "config.mtls_missing_key_rejected");
    if (error.has_value()) {
        check(error->find("mtls_client_key") != std::string::npos,
              "config.mtls_missing_key_message");
    }

    config.mtls_client_key_secret = "env:AHFL_MTLS_KEY_PATH";
    error = validate_config(config);
    check(!error.has_value(), "config.mtls_key_secret_valid");
}

void test_config_accepts_oauth2_auth_strategy() {
    LLMProviderConfig config;
    config.endpoint = "https://api.example.com/v1";
    config.model = "glm-4";
    config.api_key = "access-token";
    config.auth_scheme = "oauth2_client_credentials";
    config.auth_header.clear();

    auto error = validate_config(config);
    check(!error.has_value(), "config.oauth2_client_credentials_valid");

    config.auth_scheme = "oauth2-bearer";
    error = validate_config(config);
    check(!error.has_value(), "config.oauth2_bearer_hyphen_valid");
}

void test_http_client_uses_configured_auth_header() {
    bool saw_auth_header = false;
    HttpClient client(
        "https://api.example.com/v1",
        "sk-test",
        30,
        [&](const std::string &base_url,
            const std::vector<std::pair<std::string, std::string>> &headers,
            int timeout_seconds,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            check(base_url == "https://api.example.com/v1", "http_client.auth.base_url");
            check(timeout_seconds == 30, "http_client.auth.timeout");
            check(request_json == R"({"ok":true})", "http_client.auth.request");
            saw_auth_header = has_header(headers, "x-api-key", "sk-test");
            return ahfl::llm_provider::HttpResponse{.status_code = 200, .body = "{}"};
        },
        HttpAuthConfig{
            .scheme = "api_key_header",
            .header = "x-api-key",
        });

    const auto response = client.chat_completions(R"({"ok":true})");
    check(response.success(), "http_client.auth.response_success");
    check(saw_auth_header, "http_client.auth.custom_header");
}

void test_http_client_uses_oauth2_bearer_token() {
    bool saw_auth_header = false;
    HttpClient client(
        "https://api.example.com/v1",
        "oauth2-access-token",
        30,
        [&](const std::string &base_url,
            const std::vector<std::pair<std::string, std::string>> &headers,
            int timeout_seconds,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            check(base_url == "https://api.example.com/v1", "http_client.oauth2.base_url");
            check(timeout_seconds == 30, "http_client.oauth2.timeout");
            check(request_json == R"({"ok":true})", "http_client.oauth2.request");
            saw_auth_header = has_header(headers, "Authorization", "Bearer oauth2-access-token");
            return ahfl::llm_provider::HttpResponse{.status_code = 200, .body = "{}"};
        },
        HttpAuthConfig{
            .scheme = "oauth2_client_credentials",
        });

    const auto response = client.chat_completions(R"({"ok":true})");
    check(response.success(), "http_client.oauth2.response_success");
    check(saw_auth_header, "http_client.oauth2.bearer_header");
}

void test_http_client_omits_auth_header_for_mtls() {
    const auto headers = chat_completion_headers(
        "", HttpAuthConfig{.scheme = "mtls", .mtls_client_cert_path = "/tmp/client.pem"});
    check(has_header(headers, "Content-Type", "application/json"),
          "http_client.mtls.content_type_header");
    check(!has_header(headers, "Authorization", "Bearer "), "http_client.mtls.no_bearer_header");
    check(!has_header(headers, "x-api-key", ""), "http_client.mtls.no_api_key_header");
}

// ============================================================================
// Test: LLMCapabilityProvider end-to-end (using mock HTTP)
// ============================================================================

void test_llm_capability_provider_register() {
    // Verify register_all registers the capability into the registry
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "http://localhost:9999";
    config.model = "test-model";
    config.api_key = "test-key";

    LLMCapabilityProvider provider(program, config);
    CapabilityRegistry registry;
    provider.register_all(registry);

    // Verify the capability is registered
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

void test_provider_registry_selection_order() {
    ProviderRegistry registry;
    registry.add_provider(ProviderEntry{
        .name = "degraded-high",
        .endpoint = "https://degraded.example.com/v1",
        .api_key = "degraded-key",
        .api_key_secret = {},
        .model = "degraded-model",
        .priority = 20,
        .status = ProviderStatus::Degraded,
    });
    registry.add_provider(ProviderEntry{
        .name = "available-low",
        .endpoint = "https://available.example.com/v1",
        .api_key = "available-key",
        .api_key_secret = {},
        .model = "available-model",
        .priority = 10,
        .status = ProviderStatus::Available,
    });
    registry.add_provider(ProviderEntry{
        .name = "unavailable",
        .endpoint = "https://unavailable.example.com/v1",
        .api_key = "unavailable-key",
        .api_key_secret = {},
        .model = "unavailable-model",
        .priority = 100,
        .status = ProviderStatus::Unavailable,
    });

    auto ordered = registry.selection_order();
    check(ordered.size() == 2, "provider_registry.selection_order_count");
    if (ordered.size() == 2) {
        check(ordered[0].name == "degraded-high", "provider_registry.selection_order_priority");
        check(ordered[1].name == "available-low", "provider_registry.selection_order_second");
    }
}

void test_llm_capability_provider_enforces_prompt_budget_before_http() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "http://localhost:1";
    config.model = "test-model";
    config.api_key = "test-key";
    config.max_tokens = 1;
    config.max_prompt_tokens = 1;
    config.max_total_tokens = 2;

    LLMCapabilityProvider provider(program, config);
    std::vector<Value> args;
    args.push_back(make_string("hello"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Error, "provider.budget.status_error");
    check(!result.value.has_value(), "provider.budget.no_value");
    check(result.attempts == 0, "provider.budget.no_http_attempt");
    check(result.error_message.find("prompt budget") != std::string::npos,
          "provider.budget.error_message");

    const auto &budget_events = provider.token_budget_events();
    check(budget_events.size() == 1, "provider.budget.event_count");
    if (budget_events.size() == 1) {
        check(budget_events[0].kind == LLMTokenBudgetEventKind::PromptRejected,
              "provider.budget.event_rejected");
        check(budget_events[0].model == "test-model", "provider.budget.event_model");
        check(budget_events[0].capability_name == "ClassifyMessage",
              "provider.budget.event_capability");
        check(budget_events[0].system_prompt_tokens >= budget_events[0].effective_prompt_tokens,
              "provider.budget.event_exhausted_by_system_prompt");
        check(budget_events[0].message.find("prompt budget") != std::string::npos,
              "provider.budget.event_message");
        check(budget_events[0].diagnostic_code ==
                  ahfl::error_codes::runtime::LLMPromptBudgetRejected.full_code(),
              "provider.budget.event_diagnostic_code");
        check(budget_events[0].secret_free, "provider.budget.event_secret_free");
    }
}

void test_llm_capability_provider_falls_back_after_http_failure() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://primary.example.com/v1";
    config.model = "primary-model";
    config.api_key = "primary-key";
    config.auth_scheme = "api_key_header";
    config.auth_header = "x-primary-key";
    config.max_retries = 0;
    config.prompt_token_cost_per_million = 0.25;
    config.completion_token_cost_per_million = 1.25;
    config.fallback_providers.push_back(ProviderEntry{
        .name = "backup",
        .endpoint = "https://backup.example.com/v1",
        .api_key = "backup-key",
        .api_key_secret = {},
        .auth_scheme = "api_key_header",
        .auth_header = "x-backup-key",
        .model = "backup-model",
        .priority = 10,
        .status = ProviderStatus::Available,
    });

    std::vector<std::string> endpoints;
    std::vector<std::string> models;
    std::vector<bool> saw_expected_auth_headers;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string &base_url,
            const std::vector<std::pair<std::string, std::string>> &headers,
            int /*timeout_seconds*/,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            endpoints.push_back(base_url);
            saw_expected_auth_headers.push_back(
                base_url == "https://primary.example.com/v1"
                    ? has_header(headers, "x-primary-key", "primary-key")
                    : has_header(headers, "x-backup-key", "backup-key"));
            auto parsed = ahfl::json::parse_json(request_json);
            if (parsed.has_value() && *parsed) {
                const auto *model = (*parsed)->get("model");
                if (model != nullptr && model->as_string().has_value()) {
                    models.push_back(std::string(*model->as_string()));
                }
            }
            if (base_url == "https://primary.example.com/v1") {
                return ahfl::llm_provider::HttpResponse{.status_code = 503, .body = "unavailable"};
            }
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"high\"}"}}],"usage":{"prompt_tokens":12,"completion_tokens":8,"total_tokens":20}})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Success, "provider.fallback.status_success");
    check(result.value.has_value(), "provider.fallback.has_value");
    check(result.attempts == 2, "provider.fallback.attempts");
    check(endpoints.size() == 2, "provider.fallback.endpoint_count");
    if (endpoints.size() == 2) {
        check(endpoints[0] == "https://primary.example.com/v1", "provider.fallback.primary_first");
        check(endpoints[1] == "https://backup.example.com/v1", "provider.fallback.backup_second");
    }
    check(models.size() == 2, "provider.fallback.model_count");
    if (models.size() == 2) {
        check(models[0] == "primary-model", "provider.fallback.primary_model");
        check(models[1] == "backup-model", "provider.fallback.backup_model");
    }
    check(saw_expected_auth_headers.size() == 2, "provider.fallback.auth_header_count");
    if (saw_expected_auth_headers.size() == 2) {
        check(saw_expected_auth_headers[0], "provider.fallback.primary_auth_header");
        check(saw_expected_auth_headers[1], "provider.fallback.backup_auth_header");
    }

    const auto &events = provider.provider_health_events();
    check(events.size() == 2, "provider.fallback.health_event_count");
    if (events.size() == 2) {
        check(events[0].kind == LLMProviderHealthEventKind::ProviderDegraded,
              "provider.fallback.health_primary_degraded");
        check(events[0].provider_name == "primary", "provider.fallback.health_primary_name");
        check(events[0].status_code == 503, "provider.fallback.health_primary_status");
        check(events[0].attempts_after == 1, "provider.fallback.health_primary_attempts");
        check(events[0].message.find("status=503") != std::string::npos,
              "provider.fallback.health_primary_message");
        check(events[1].kind == LLMProviderHealthEventKind::FallbackSelected,
              "provider.fallback.health_selected_kind");
        check(events[1].provider_name == "primary", "provider.fallback.health_selected_source");
        check(events[1].selected_provider_name == "backup",
              "provider.fallback.health_selected_target");
        check(events[1].status_code == 200, "provider.fallback.health_selected_status");
        check(events[1].attempts_after == 2, "provider.fallback.health_selected_attempts");
        check(events[0].secret_free && events[1].secret_free,
              "provider.fallback.health_secret_free");
    }

    const auto &usage_events = provider.token_usage_events();
    check(usage_events.size() == 1, "provider.fallback.token_usage_event_count");
    if (usage_events.size() == 1) {
        check(usage_events[0].provider_name == "backup", "provider.fallback.token_usage_provider");
        check(usage_events[0].model == "backup-model", "provider.fallback.token_usage_model");
        check(usage_events[0].prompt_tokens == 12, "provider.fallback.prompt_tokens");
        check(usage_events[0].completion_tokens == 8, "provider.fallback.completion_tokens");
        check(usage_events[0].total_tokens == 20, "provider.fallback.total_tokens");
        check(usage_events[0].prompt_cost_usd > 0.0, "provider.fallback.prompt_cost");
        check(usage_events[0].completion_cost_usd > 0.0, "provider.fallback.completion_cost");
        check(usage_events[0].total_cost_usd > usage_events[0].prompt_cost_usd,
              "provider.fallback.total_cost");
        check(usage_events[0].cost_estimated, "provider.fallback.cost_estimated");
        check(usage_events[0].secret_free, "provider.fallback.token_usage_secret_free");
    }

    const auto &budget_events = provider.token_budget_events();
    check(budget_events.size() == 2, "provider.fallback.token_budget_event_count");
    if (budget_events.size() == 2) {
        check(budget_events[0].kind == LLMTokenBudgetEventKind::PromptAccepted,
              "provider.fallback.prompt_budget_accepted");
        check(budget_events[0].capability_name == "ClassifyMessage",
              "provider.fallback.prompt_budget_capability");
        check(budget_events[1].kind == LLMTokenBudgetEventKind::UsageWithinBudget,
              "provider.fallback.usage_budget_within");
        check(budget_events[1].provider_name == "backup",
              "provider.fallback.usage_budget_provider");
        check(budget_events[1].capability_name == "ClassifyMessage",
              "provider.fallback.usage_budget_capability");
        check(budget_events[1].total_tokens == 20, "provider.fallback.usage_budget_total");
        check(budget_events[0].secret_free && budget_events[1].secret_free,
              "provider.fallback.token_budget_secret_free");
    }
}

void test_llm_capability_provider_records_usage_budget_exceeded() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://usage.example.com/v1";
    config.model = "usage-model";
    config.api_key = "usage-key";
    config.max_retries = 0;

    LLMCapabilityProvider provider(
        program,
        config,
        [](const std::string & /*base_url*/,
           const std::vector<std::pair<std::string, std::string>> & /*headers*/,
           int /*timeout_seconds*/,
           const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"large\"}"}}],"usage":{"prompt_tokens":4000,"completion_tokens":97,"total_tokens":4097}})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Success,
          "provider.usage_budget_exceeded.status_success");
    check(result.value.has_value(), "provider.usage_budget_exceeded.has_value");

    const auto &budget_events = provider.token_budget_events();
    check(budget_events.size() == 2, "provider.usage_budget_exceeded.event_count");
    if (budget_events.size() == 2) {
        check(budget_events[0].kind == LLMTokenBudgetEventKind::PromptAccepted,
              "provider.usage_budget_exceeded.prompt_accepted");
        check(budget_events[1].kind == LLMTokenBudgetEventKind::UsageExceededBudget,
              "provider.usage_budget_exceeded.usage_exceeded");
        check(budget_events[1].provider_name == "primary",
              "provider.usage_budget_exceeded.provider");
        check(budget_events[1].capability_name == "ClassifyMessage",
              "provider.usage_budget_exceeded.capability");
        check(budget_events[1].total_tokens == 4097, "provider.usage_budget_exceeded.total_tokens");
        check(budget_events[1].max_total_tokens == 4096,
              "provider.usage_budget_exceeded.max_total_tokens");
        check(budget_events[1].diagnostic_code ==
                  ahfl::error_codes::runtime::LLMTokenBudgetExceeded.full_code(),
              "provider.usage_budget_exceeded.diagnostic_code");
        check(budget_events[1].secret_free, "provider.usage_budget_exceeded.secret_free");
    }
}

void test_llm_capability_provider_applies_capability_budget_policy() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://budget.example.com/v1";
    config.model = "budget-model";
    config.api_key = "budget-key";
    config.max_retries = 0;
    config.prompt_token_cost_per_million = 1000.0;
    config.completion_token_cost_per_million = 1000.0;
    config.capability_token_budgets.push_back(LLMCapabilityTokenBudget{
        .capability_name = "ClassifyMessage",
        .max_tokens = std::optional<int>{7},
        .max_prompt_tokens = std::optional<int>{1000},
        .max_total_tokens = std::optional<int>{5000},
        .max_total_cost_usd = std::optional<double>{0.001},
        .policy = std::optional<std::string>{"warn"},
    });

    int request_max_tokens = 0;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string & /*base_url*/,
            const std::vector<std::pair<std::string, std::string>> & /*headers*/,
            int /*timeout_seconds*/,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            auto parsed = ahfl::json::parse_json(request_json);
            if (parsed.has_value() && *parsed) {
                const auto *max_tokens = (*parsed)->get("max_tokens");
                if (max_tokens != nullptr && max_tokens->as_int().has_value()) {
                    request_max_tokens = static_cast<int>(*max_tokens->as_int());
                }
            }
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"budgeted\"}"}}],"usage":{"prompt_tokens":12,"completion_tokens":8,"total_tokens":20}})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Success,
          "provider.capability_budget.status_success");
    check(result.value.has_value(), "provider.capability_budget.has_value");
    check(request_max_tokens == 7, "provider.capability_budget.request_max_tokens");

    const auto &budget_events = provider.token_budget_events();
    check(budget_events.size() == 3, "provider.capability_budget.event_count");
    if (budget_events.size() == 3) {
        check(budget_events[0].kind == LLMTokenBudgetEventKind::PromptAccepted,
              "provider.capability_budget.prompt_accepted");
        check(budget_events[0].max_response_tokens == 7,
              "provider.capability_budget.prompt_max_response");
        check(budget_events[0].max_total_tokens == 5000,
              "provider.capability_budget.prompt_max_total");
        check(budget_events[0].policy == "warn", "provider.capability_budget.prompt_policy");
        check(budget_events[1].kind == LLMTokenBudgetEventKind::UsageWithinBudget,
              "provider.capability_budget.usage_within");
        check(budget_events[1].max_total_tokens == 5000,
              "provider.capability_budget.usage_max_total");
        check(budget_events[2].kind == LLMTokenBudgetEventKind::CostExceededBudget,
              "provider.capability_budget.cost_exceeded");
        check(budget_events[2].policy == "warn", "provider.capability_budget.cost_policy");
        check(budget_events[2].total_cost_usd > budget_events[2].max_total_cost_usd,
              "provider.capability_budget.cost_over_limit");
        check(budget_events[2].diagnostic_code ==
                  ahfl::error_codes::runtime::LLMCostBudgetExceeded.full_code(),
              "provider.capability_budget.cost_diagnostic_code");
        check(budget_events[2].secret_free, "provider.capability_budget.secret_free");
    }
}

void test_llm_capability_provider_records_workflow_node_cumulative_budget() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://budget.example.com/v1";
    config.model = "budget-model";
    config.api_key = "budget-key";
    config.max_retries = 0;
    config.max_workflow_total_tokens = 35;
    config.max_node_total_tokens = 35;

    LLMCapabilityProvider provider(
        program,
        config,
        [](const std::string & /*base_url*/,
           const std::vector<std::pair<std::string, std::string>> & /*headers*/,
           int /*timeout_seconds*/,
           const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"budgeted\"}"}}],"usage":{"prompt_tokens":12,"completion_tokens":8,"total_tokens":20}})",
            };
        });

    ahfl::runtime::CapabilityInvocationContext context{
        .workflow_name = "support::TicketWorkflow",
        .workflow_node_name = "classify",
        .agent_name = "support::ClassifyAgent",
        .state_name = "Done",
        .workflow_node_execution_index = 2,
        .has_workflow_node_context = true,
    };

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto first = provider.invoke_with_context(context, "ClassifyMessage", args);
    auto second = provider.invoke_with_context(context, "ClassifyMessage", args);

    check(first.status == CapabilityCallStatus::Success,
          "provider.cumulative_budget.first_success");
    check(second.status == CapabilityCallStatus::Success,
          "provider.cumulative_budget.second_success");

    const auto &budget_events = provider.token_budget_events();
    check(budget_events.size() == 8, "provider.cumulative_budget.event_count");
    if (budget_events.size() == 8) {
        check(budget_events[0].workflow_name == "support::TicketWorkflow",
              "provider.cumulative_budget.prompt_workflow");
        check(budget_events[0].workflow_node_name == "classify",
              "provider.cumulative_budget.prompt_node");
        check(budget_events[0].agent_name == "support::ClassifyAgent",
              "provider.cumulative_budget.prompt_agent");
        check(budget_events[0].state_name == "Done", "provider.cumulative_budget.prompt_state");
        check(budget_events[0].workflow_node_execution_index == 2,
              "provider.cumulative_budget.prompt_node_index");
        check(budget_events[2].kind == LLMTokenBudgetEventKind::WorkflowUsageWithinBudget,
              "provider.cumulative_budget.first_workflow_within");
        check(budget_events[2].cumulative_workflow_tokens == 20,
              "provider.cumulative_budget.first_workflow_cumulative");
        check(budget_events[3].kind == LLMTokenBudgetEventKind::NodeUsageWithinBudget,
              "provider.cumulative_budget.first_node_within");
        check(budget_events[3].cumulative_node_tokens == 20,
              "provider.cumulative_budget.first_node_cumulative");
        check(budget_events[6].kind == LLMTokenBudgetEventKind::WorkflowUsageExceededBudget,
              "provider.cumulative_budget.second_workflow_exceeded");
        check(budget_events[6].cumulative_workflow_tokens == 40,
              "provider.cumulative_budget.second_workflow_cumulative");
        check(budget_events[6].max_workflow_total_tokens == 35,
              "provider.cumulative_budget.workflow_max");
        check(budget_events[6].diagnostic_code ==
                  ahfl::error_codes::runtime::LLMTokenBudgetExceeded.full_code(),
              "provider.cumulative_budget.workflow_diagnostic");
        check(budget_events[7].kind == LLMTokenBudgetEventKind::NodeUsageExceededBudget,
              "provider.cumulative_budget.second_node_exceeded");
        check(budget_events[7].cumulative_node_tokens == 40,
              "provider.cumulative_budget.second_node_cumulative");
        check(budget_events[7].max_node_total_tokens == 35, "provider.cumulative_budget.node_max");
        check(budget_events[7].has_workflow_node_context,
              "provider.cumulative_budget.has_node_context");
        check(budget_events[7].secret_free, "provider.cumulative_budget.secret_free");
    }

    const auto &usage_events = provider.token_usage_events();
    check(usage_events.size() == 2, "provider.cumulative_budget.usage_event_count");
    if (usage_events.size() == 2) {
        check(usage_events[0].workflow_name == "support::TicketWorkflow",
              "provider.cumulative_budget.usage_workflow");
        check(usage_events[0].workflow_node_name == "classify",
              "provider.cumulative_budget.usage_node");
        check(usage_events[0].state_name == "Done", "provider.cumulative_budget.usage_state");
    }
}

void test_llm_capability_provider_records_fallback_exhaustion() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://primary.example.com/v1";
    config.model = "primary-model";
    config.api_key = "primary-key";
    config.max_retries = 1;
    config.fallback_providers.push_back(ProviderEntry{
        .name = "backup",
        .endpoint = "https://backup.example.com/v1",
        .api_key = "backup-key",
        .api_key_secret = {},
        .auth_scheme = "bearer",
        .auth_header = "Authorization",
        .model = "backup-model",
        .priority = 10,
        .status = ProviderStatus::Available,
    });

    LLMCapabilityProvider provider(
        program,
        config,
        [](const std::string &base_url,
           const std::vector<std::pair<std::string, std::string>> & /*headers*/,
           int /*timeout_seconds*/,
           const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            if (base_url == "https://primary.example.com/v1") {
                return ahfl::llm_provider::HttpResponse{.status_code = 503, .body = "primary down"};
            }
            return ahfl::llm_provider::HttpResponse{.status_code = 502, .body = "backup down"};
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::RetryExhausted,
          "provider.fallback_exhausted.status");
    check(!result.value.has_value(), "provider.fallback_exhausted.no_value");
    check(result.attempts == 4, "provider.fallback_exhausted.attempts");
    check(result.error_message.find("primary(status=503") != std::string::npos,
          "provider.fallback_exhausted.primary_message");
    check(result.error_message.find("backup(status=502") != std::string::npos,
          "provider.fallback_exhausted.backup_message");

    const auto &events = provider.provider_health_events();
    check(events.size() == 3, "provider.fallback_exhausted.health_event_count");
    if (events.size() == 3) {
        check(events[0].kind == LLMProviderHealthEventKind::ProviderDegraded,
              "provider.fallback_exhausted.primary_degraded");
        check(events[0].provider_name == "primary", "provider.fallback_exhausted.primary_name");
        check(events[0].status_code == 503, "provider.fallback_exhausted.primary_status");
        check(events[1].kind == LLMProviderHealthEventKind::ProviderDegraded,
              "provider.fallback_exhausted.backup_degraded");
        check(events[1].provider_name == "backup", "provider.fallback_exhausted.backup_name");
        check(events[1].status_code == 502, "provider.fallback_exhausted.backup_status");
        check(events[2].kind == LLMProviderHealthEventKind::FallbackExhausted,
              "provider.fallback_exhausted.exhausted_kind");
        check(events[2].attempts_after == 4, "provider.fallback_exhausted.exhausted_attempts");
        check(events[2].message.find("fallback exhausted") != std::string::npos,
              "provider.fallback_exhausted.exhausted_message");
        check(events[0].secret_free && events[1].secret_free && events[2].secret_free,
              "provider.fallback_exhausted.secret_free");
    }
}

void test_llm_capability_provider_parses_streaming_response() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://stream.example.com/v1";
    config.model = "stream-model";
    config.api_key = "stream-key";
    config.stream = true;
    config.max_retries = 0;

    bool stream_requested = false;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string & /*base_url*/,
            const std::vector<std::pair<std::string, std::string>> & /*headers*/,
            int /*timeout_seconds*/,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            auto parsed = ahfl::json::parse_json(request_json);
            if (parsed.has_value() && *parsed) {
                const auto *stream = (*parsed)->get("stream");
                stream_requested = stream != nullptr && stream->as_bool().value_or(false);
            }
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"category\\\":\\\"\"}}]}\n"
                    "data: "
                    "{\"choices\":[{\"delta\":{\"content\":\"Technical\\\",\\\"confidence\\\":"
                    "\\\"high\\\"}\"}}]}\n"
                    "data: [DONE]\n",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(stream_requested, "provider.streaming.request_flag");
    check(result.status == CapabilityCallStatus::Success, "provider.streaming.status_success");
    check(result.value.has_value(), "provider.streaming.has_value");
    check(result.attempts == 1, "provider.streaming.attempts");

    const auto &events = provider.streaming_events();
    check(events.size() == 3, "provider.streaming.event_count");
    if (events.size() == 3) {
        check(events[0].kind == LLMStreamingEventKind::Chunk,
              "provider.streaming.first_chunk_kind");
        check(events[0].provider_name == "primary", "provider.streaming.first_chunk_provider");
        check(events[0].chunk_index == 1, "provider.streaming.first_chunk_index");
        check(events[0].chunk_bytes > 0, "provider.streaming.first_chunk_bytes");
        check(events[0].total_content_bytes == events[0].chunk_bytes,
              "provider.streaming.first_chunk_total");
        check(events[1].kind == LLMStreamingEventKind::Chunk,
              "provider.streaming.second_chunk_kind");
        check(events[1].chunk_index == 2, "provider.streaming.second_chunk_index");
        check(events[1].total_content_bytes > events[0].total_content_bytes,
              "provider.streaming.second_chunk_total");
        check(events[2].kind == LLMStreamingEventKind::Completed,
              "provider.streaming.completed_kind");
        check(events[2].completed, "provider.streaming.completed_flag");
        check(events[2].total_content_bytes == events[1].total_content_bytes,
              "provider.streaming.completed_total");
        check(events[0].secret_free && events[1].secret_free && events[2].secret_free,
              "provider.streaming.events_secret_free");
    }
}

void test_llm_capability_provider_rejects_incomplete_streaming_response() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://stream.example.com/v1";
    config.model = "stream-model";
    config.api_key = "stream-key";
    config.stream = true;
    config.max_retries = 0;

    LLMCapabilityProvider provider(
        program,
        config,
        [](const std::string & /*base_url*/,
           const std::vector<std::pair<std::string, std::string>> & /*headers*/,
           int /*timeout_seconds*/,
           const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body = "data: {\"choices\":[{\"delta\":{\"content\":\"{\\\"category\\\":"
                        "\\\"Technical\\\",\\\"confidence\\\":\\\"partial\\\"}\"}}]}\n",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Error,
          "provider.streaming_incomplete.status_error");
    check(!result.value.has_value(), "provider.streaming_incomplete.no_value");
    check(result.attempts == 1, "provider.streaming_incomplete.attempts");
    check(result.error_message.find("missing [DONE]") != std::string::npos,
          "provider.streaming_incomplete.error_message");

    const auto &events = provider.streaming_events();
    check(events.size() == 2, "provider.streaming_incomplete.event_count");
    if (events.size() == 2) {
        check(events[0].kind == LLMStreamingEventKind::Chunk,
              "provider.streaming_incomplete.chunk_kind");
        check(events[0].chunk_index == 1, "provider.streaming_incomplete.chunk_index");
        check(events[0].chunk_bytes > 0, "provider.streaming_incomplete.chunk_bytes");
        check(events[1].kind == LLMStreamingEventKind::Interrupted,
              "provider.streaming_incomplete.interrupted_kind");
        check(!events[1].completed, "provider.streaming_incomplete.interrupted_flag");
        check(events[1].total_content_bytes == events[0].total_content_bytes,
              "provider.streaming_incomplete.interrupted_total");
        check(events[0].secret_free && events[1].secret_free,
              "provider.streaming_incomplete.events_secret_free");
    }
}

void test_llm_capability_provider_uses_response_cache_when_enabled() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://cache.example.com/v1";
    config.model = "cache-model";
    config.api_key = "cache-key";
    config.max_retries = 0;
    config.response_cache_enabled = true;
    config.response_cache_max_entries = 8;
    config.response_cache_ttl_seconds = 60;

    int request_count = 0;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string & /*base_url*/,
            const std::vector<std::pair<std::string, std::string>> & /*headers*/,
            int /*timeout_seconds*/,
            const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            ++request_count;
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"cached\"}"}}]})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto first = provider.invoke("ClassifyMessage", args);
    auto second = provider.invoke("ClassifyMessage", args);

    check(first.status == CapabilityCallStatus::Success, "provider.cache.first_success");
    check(second.status == CapabilityCallStatus::Success, "provider.cache.second_success");
    check(first.attempts == 1, "provider.cache.first_attempt");
    check(second.attempts == 0, "provider.cache.second_attempt_cache_hit");
    check(request_count == 1, "provider.cache.single_http_request");

    const auto &events = provider.response_cache_audit_events();
    check(events.size() == 3, "provider.cache.audit_event_count");
    if (events.size() == 3) {
        check(events[0].kind == LLMResponseCacheAuditEventKind::Miss,
              "provider.cache.audit_first_miss");
        check(events[1].kind == LLMResponseCacheAuditEventKind::Write,
              "provider.cache.audit_second_write");
        check(events[2].kind == LLMResponseCacheAuditEventKind::Hit,
              "provider.cache.audit_third_hit");
        check(events[0].model == "cache-model", "provider.cache.audit_model");
        check(events[0].cache_key_version == std::string(kResponseCacheKeyVersion),
              "provider.cache.audit_key_version");
        check(!events[0].key_fingerprint.empty(), "provider.cache.audit_fingerprint");
        check(events[0].key_fingerprint == events[1].key_fingerprint,
              "provider.cache.audit_write_same_key");
        check(events[1].key_fingerprint == events[2].key_fingerprint,
              "provider.cache.audit_hit_same_key");
        check(events[0].system_prompt_bytes > 0, "provider.cache.audit_system_prompt_size");
        check(events[0].user_prompt_bytes > 0, "provider.cache.audit_user_prompt_size");
        check(events[0].response_bytes == 0, "provider.cache.audit_miss_response_size");
        check(events[1].response_bytes > 0, "provider.cache.audit_write_response_size");
        check(events[2].response_bytes > 0, "provider.cache.audit_hit_response_size");
        check(events[0].cache_size_after == 0, "provider.cache.audit_miss_size_after");
        check(events[1].cache_size_after == 1, "provider.cache.audit_write_size_after");
        check(events[2].cache_size_after == 1, "provider.cache.audit_hit_size_after");
        check(events[0].secret_free && events[1].secret_free && events[2].secret_free,
              "provider.cache.audit_secret_free");
    }
}

void test_llm_capability_provider_persists_response_cache() {
    auto program = build_test_program();
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto cache_path = std::filesystem::temp_directory_path() /
                            ("ahfl-llm-response-cache-" + std::to_string(unique_suffix) + ".json");
    std::filesystem::remove(cache_path);

    LLMProviderConfig config;
    config.endpoint = "https://cache.example.com/v1";
    config.model = "cache-model";
    config.api_key = "cache-key";
    config.max_retries = 0;
    config.response_cache_enabled = true;
    config.response_cache_max_entries = 8;
    config.response_cache_ttl_seconds = 60;
    config.response_cache_path = cache_path.string();

    int request_count = 0;
    {
        LLMCapabilityProvider provider(
            program,
            config,
            [&](const std::string & /*base_url*/,
                const std::vector<std::pair<std::string, std::string>> & /*headers*/,
                int /*timeout_seconds*/,
                const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
                ++request_count;
                return ahfl::llm_provider::HttpResponse{
                    .status_code = 200,
                    .body =
                        R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"persisted\"}"}}]})",
                };
            });

        std::vector<Value> args;
        args.push_back(make_string("My server is down"));
        auto result = provider.invoke("ClassifyMessage", args);
        check(result.status == CapabilityCallStatus::Success,
              "provider.persistent_cache.first_success");
        check(result.value.has_value(), "provider.persistent_cache.first_has_value");

        const auto &events = provider.response_cache_audit_events();
        bool saw_persist = false;
        for (const auto &event : events) {
            if (event.kind == LLMResponseCacheAuditEventKind::SnapshotPersisted) {
                saw_persist = true;
                check(event.persistent, "provider.persistent_cache.persist_event_persistent");
                check(event.snapshot_entry_count == 1,
                      "provider.persistent_cache.persist_entry_count");
            }
        }
        check(saw_persist, "provider.persistent_cache.saw_persist_event");
    }

    check(request_count == 1, "provider.persistent_cache.first_http_count");
    check(std::filesystem::exists(cache_path), "provider.persistent_cache.file_exists");
    std::ifstream cache_file(cache_path, std::ios::binary);
    std::ostringstream cache_text;
    cache_text << cache_file.rdbuf();
    check(cache_text.str().find("My server is down") == std::string::npos,
          "provider.persistent_cache.no_prompt_leak");
    check(cache_text.str().find("ahfl.llm_response_cache.v0") != std::string::npos,
          "provider.persistent_cache.schema");

    {
        LLMCapabilityProvider provider(
            program,
            config,
            [&](const std::string & /*base_url*/,
                const std::vector<std::pair<std::string, std::string>> & /*headers*/,
                int /*timeout_seconds*/,
                const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
                ++request_count;
                return ahfl::llm_provider::HttpResponse{
                    .status_code = 500,
                    .body = "persistent cache miss",
                };
            });

        std::vector<Value> args;
        args.push_back(make_string("My server is down"));
        auto result = provider.invoke("ClassifyMessage", args);
        check(result.status == CapabilityCallStatus::Success,
              "provider.persistent_cache.second_success");
        check(result.value.has_value(), "provider.persistent_cache.second_has_value");

        const auto &events = provider.response_cache_audit_events();
        bool saw_load = false;
        bool saw_hit = false;
        for (const auto &event : events) {
            if (event.kind == LLMResponseCacheAuditEventKind::SnapshotLoaded) {
                saw_load = true;
                check(event.snapshot_entry_count == 1,
                      "provider.persistent_cache.load_entry_count");
                check(event.persistent, "provider.persistent_cache.load_event_persistent");
            }
            if (event.kind == LLMResponseCacheAuditEventKind::Hit) {
                saw_hit = true;
                check(event.persistent, "provider.persistent_cache.hit_event_persistent");
            }
        }
        check(saw_load, "provider.persistent_cache.saw_load_event");
        check(saw_hit, "provider.persistent_cache.saw_hit_event");
    }

    check(request_count == 1, "provider.persistent_cache.second_no_http");
    std::filesystem::remove(cache_path);
}

void test_llm_capability_provider_cache_disabled_by_default() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://cache-disabled.example.com/v1";
    config.model = "cache-disabled-model";
    config.api_key = "cache-key";
    config.max_retries = 0;

    int request_count = 0;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string & /*base_url*/,
            const std::vector<std::pair<std::string, std::string>> & /*headers*/,
            int /*timeout_seconds*/,
            const std::string & /*request_json*/) -> ahfl::llm_provider::HttpResponse {
            ++request_count;
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"fresh\"}"}}]})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto first = provider.invoke("ClassifyMessage", args);
    auto second = provider.invoke("ClassifyMessage", args);

    check(first.status == CapabilityCallStatus::Success, "provider.cache_disabled.first_success");
    check(second.status == CapabilityCallStatus::Success, "provider.cache_disabled.second_success");
    check(first.attempts == 1, "provider.cache_disabled.first_attempt");
    check(second.attempts == 1, "provider.cache_disabled.second_attempt");
    check(request_count == 2, "provider.cache_disabled.two_http_requests");
    check(provider.response_cache_audit_events().empty(),
          "provider.cache_disabled.no_audit_events");
}

void test_llm_capability_provider_executes_tool_calling_round() {
    auto program = build_test_program();
    LLMProviderConfig config;
    config.endpoint = "https://tools.example.com/v1";
    config.model = "tool-model";
    config.api_key = "tool-key";
    config.max_retries = 0;
    config.max_tool_rounds = 3;

    int request_count = 0;
    bool first_request_has_tools = false;
    bool second_request_has_tool_result = false;
    bool executor_called = false;
    LLMCapabilityProvider provider(
        program,
        config,
        [&](const std::string & /*base_url*/,
            const std::vector<std::pair<std::string, std::string>> & /*headers*/,
            int /*timeout_seconds*/,
            const std::string &request_json) -> ahfl::llm_provider::HttpResponse {
            ++request_count;
            auto parsed = ahfl::json::parse_json(request_json);
            if (parsed.has_value() && *parsed) {
                if (request_count == 1) {
                    const auto *tools = (*parsed)->get("tools");
                    first_request_has_tools =
                        tools != nullptr && tools->is_array() && tools->array_items.size() == 1;
                } else if (request_count == 2) {
                    const auto *messages = (*parsed)->get("messages");
                    if (messages != nullptr && messages->is_array()) {
                        for (const auto &message : messages->array_items) {
                            if (message == nullptr || !message->is_object()) {
                                continue;
                            }
                            const auto *role = message->get("role");
                            const auto *content = message->get("content");
                            if (role != nullptr && content != nullptr &&
                                role->as_string().value_or("") == "tool" &&
                                content->as_string().value_or("") == R"({"hint":"Technical"})") {
                                second_request_has_tool_result = true;
                            }
                        }
                    }
                }
            }

            if (request_count == 1) {
                return ahfl::llm_provider::HttpResponse{
                    .status_code = 200,
                    .body =
                        R"({"choices":[{"message":{"content":null,"tool_calls":[{"id":"call_1","type":"function","function":{"name":"lookup","arguments":"{\"query\":\"server\"}"}}]}}]})",
                };
            }
            return ahfl::llm_provider::HttpResponse{
                .status_code = 200,
                .body =
                    R"({"choices":[{"message":{"content":"{\"category\":\"Technical\",\"confidence\":\"tool-backed\"}"}}]})",
            };
        });

    provider.set_tools(
        {ToolDefinition{
            .name = "lookup",
            .description = "Lookup support context",
            .params_schema_json = R"({"type":"object","properties":{"query":{"type":"string"}}})",
        }},
        [&](const ToolCall &call) -> ToolCallResult {
            executor_called = true;
            check(call.id == "call_1", "provider.tool_calling.call_id");
            check(call.name == "lookup", "provider.tool_calling.call_name");
            check(call.arguments_json.find("server") != std::string::npos,
                  "provider.tool_calling.arguments");
            return ToolCallResult{
                .tool_call_id = call.id,
                .content = R"({"hint":"Technical"})",
            };
        });

    std::vector<Value> args;
    args.push_back(make_string("My server is down"));
    auto result = provider.invoke("ClassifyMessage", args);

    check(result.status == CapabilityCallStatus::Success, "provider.tool_calling.status_success");
    check(result.value.has_value(), "provider.tool_calling.has_value");
    check(result.attempts == 2, "provider.tool_calling.attempts");
    check(request_count == 2, "provider.tool_calling.request_count");
    check(first_request_has_tools, "provider.tool_calling.first_request_tools");
    check(second_request_has_tool_result, "provider.tool_calling.second_request_tool_result");
    check(executor_called, "provider.tool_calling.executor_called");
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
    test_config_api_key_secret_handle();
    test_config_secret_provider_chain();
    test_config_rejects_invalid_secret_provider();
    test_config_rejects_invalid_budget();
    test_config_rejects_invalid_response_cache();
    test_config_rejects_invalid_auth_strategy();
    test_config_accepts_mtls_auth_strategy();
    test_config_accepts_oauth2_auth_strategy();
    test_http_client_uses_configured_auth_header();
    test_http_client_uses_oauth2_bearer_token();
    test_http_client_omits_auth_header_for_mtls();
    test_llm_capability_provider_register();
    test_llm_capability_provider_unknown_capability_returns_error();
    test_provider_registry_selection_order();
    test_llm_capability_provider_enforces_prompt_budget_before_http();
    test_llm_capability_provider_falls_back_after_http_failure();
    test_llm_capability_provider_records_usage_budget_exceeded();
    test_llm_capability_provider_applies_capability_budget_policy();
    test_llm_capability_provider_records_workflow_node_cumulative_budget();
    test_llm_capability_provider_records_fallback_exhaustion();
    test_llm_capability_provider_parses_streaming_response();
    test_llm_capability_provider_rejects_incomplete_streaming_response();
    test_llm_capability_provider_uses_response_cache_when_enabled();
    test_llm_capability_provider_persists_response_cache();
    test_llm_capability_provider_cache_disabled_by_default();
    test_llm_capability_provider_executes_tool_calling_round();

    std::cout << "\n=== LLM Provider Tests ===\n";
    std::cout << pass_count << "/" << test_count << " passed\n";

    return (pass_count == test_count) ? 0 : 1;
}
