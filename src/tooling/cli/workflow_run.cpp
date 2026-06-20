#include "tooling/cli/workflow_run.hpp"

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "base/json/json_value.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "runtime/engine/capability_bridge.hpp"
#include "runtime/engine/response_schema_validator.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"
#include "runtime/providers/llm/llm_capability_provider.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"
#include "runtime/providers/llm/tool_calling.hpp"
#include "runtime/providers/secret/cloud_secret_provider.hpp"
#include "runtime/providers/secret/secret_provider.hpp"
#include "runtime/providers/secret/vault_provider.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::cli {
namespace {

using ahfl::evaluator::print_value;
using ahfl::evaluator::value_from_json;
using ahfl::evaluator::value_to_json;
using ahfl::llm_provider::LLMCapabilityProvider;
using ahfl::llm_provider::LLMProviderHealthEvent;
using ahfl::llm_provider::LLMProviderHealthEventKind;
using ahfl::llm_provider::LLMResponseCacheAuditEventKind;
using ahfl::llm_provider::LLMStreamingEventKind;
using ahfl::llm_provider::LLMTokenBudgetEventKind;
using ahfl::llm_provider::load_config;
using ahfl::llm_provider::ToolCall;
using ahfl::llm_provider::ToolCallResult;
using ahfl::llm_provider::ToolDefinition;
using ahfl::llm_provider::validate_config;
using ahfl::runtime::AgentStatus;
using ahfl::runtime::CapabilityCallStatus;
using ahfl::runtime::CapabilityRegistry;
using ahfl::runtime::CapabilityResponseFormat;
using ahfl::runtime::CircuitBreakerConfig;
using ahfl::runtime::GrpcJsonTranscodingCapabilityConfig;
using ahfl::runtime::HTTPCapabilityConfig;
using ahfl::runtime::make_grpc_json_transcoding_capability;
using ahfl::runtime::make_http_capability;
using ahfl::runtime::RetryConfig;
using ahfl::runtime::TimeoutConfig;
using ahfl::runtime::WorkflowResult;
using ahfl::runtime::WorkflowRuntime;
using ahfl::runtime::WorkflowRuntimeConfig;
using ahfl::runtime::WorkflowStatus;
using ahfl::secret::AuthConfig;
using ahfl::secret::AuthScheme;
using ahfl::secret::CloudSecretManagerConfig;
using ahfl::secret::CloudSecretManagerProvider;
using ahfl::secret::EnvSecretProvider;
using ahfl::secret::SecretManager;
using ahfl::secret::SecretProviderChain;
using ahfl::secret::VaultConfig;
using ahfl::secret::VaultSecretProvider;

[[nodiscard]] std::filesystem::path default_llm_config_path() {
    const char *home = std::getenv("HOME");
    if (home == nullptr) {
        return {};
    }
    return std::filesystem::path(home) / ".ahfl" / "llm_config.json";
}

[[nodiscard]] std::filesystem::path
llm_config_path_from_options(const CommandLineOptions &options) {
    if (options.llm_config_descriptor.has_value()) {
        return std::filesystem::path(std::string(*options.llm_config_descriptor));
    }
    return default_llm_config_path();
}

[[nodiscard]] std::optional<std::string>
read_text_file(const std::filesystem::path &path, std::string_view description, std::ostream &err) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        err << "error: failed to open " << description << ": " << ahfl::display_path(path) << '\n';
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

[[nodiscard]] std::optional<std::string> env_var(std::string_view key) {
    std::string name(key);
    const char *value = std::getenv(name.c_str());
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

[[nodiscard]] std::shared_ptr<SecretManager>
build_llm_secret_manager(const ahfl::llm_provider::LLMProviderConfig &config, std::ostream &err) {
    auto chain = std::make_unique<SecretProviderChain>();

    auto providers = config.secret_providers;
    if (providers.empty()) {
        providers.push_back(ahfl::llm_provider::SecretProviderConfig{});
    }

    for (const auto &provider : providers) {
        if (provider.kind == "env") {
            chain->add_provider(provider.prefix,
                                std::make_unique<EnvSecretProvider>(),
                                provider.default_for_unqualified);
            continue;
        }

        if (provider.kind == "vault") {
            VaultConfig vault_config;
            vault_config.address = provider.address;
            vault_config.token = provider.token;
            if (vault_config.token.empty() && !provider.token_env.empty()) {
                auto token = env_var(provider.token_env);
                if (!token.has_value() || token->empty()) {
                    err << "error: failed to resolve Vault token_env '" << provider.token_env
                        << "' for LLM secret provider prefix '" << provider.prefix << "'\n";
                    return nullptr;
                }
                vault_config.token = std::move(*token);
            }
            if (vault_config.token.empty()) {
                err << "error: LLM vault secret provider prefix '" << provider.prefix
                    << "' requires token or token_env\n";
                return nullptr;
            }
            vault_config.mount_path = provider.mount_path;
            vault_config.namespace_path = provider.namespace_path;
            vault_config.timeout = std::chrono::seconds{provider.timeout_seconds};
            vault_config.verify_tls = provider.verify_tls;

            auto vault = std::make_unique<VaultSecretProvider>(std::move(vault_config));
            vault->authenticate();
            chain->add_provider(
                provider.prefix, std::move(vault), provider.default_for_unqualified);
            continue;
        }

        if (provider.kind == "cloud") {
            CloudSecretManagerConfig cloud_config;
            cloud_config.address = provider.address;
            cloud_config.token = provider.token;
            if (cloud_config.token.empty() && !provider.token_env.empty()) {
                auto token = env_var(provider.token_env);
                if (!token.has_value() || token->empty()) {
                    err << "error: failed to resolve cloud secret token_env '" << provider.token_env
                        << "' for LLM secret provider prefix '" << provider.prefix << "'\n";
                    return nullptr;
                }
                cloud_config.token = std::move(*token);
            }
            if (cloud_config.token.empty()) {
                err << "error: LLM cloud secret provider prefix '" << provider.prefix
                    << "' requires token or token_env\n";
                return nullptr;
            }
            cloud_config.project = provider.project;
            cloud_config.version = provider.version;
            cloud_config.timeout = std::chrono::seconds{provider.timeout_seconds};

            chain->add_provider(
                provider.prefix,
                std::make_unique<CloudSecretManagerProvider>(std::move(cloud_config)),
                provider.default_for_unqualified);
            continue;
        }
    }

    return std::make_shared<SecretManager>(std::move(chain));
}

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

[[nodiscard]] bool is_oauth2_auth_scheme(std::string_view scheme) {
    const auto normalized = normalized_auth_scheme(scheme);
    return normalized == "oauth2_bearer" || normalized == "oauth2_client_credentials";
}

[[nodiscard]] bool is_mtls_auth_scheme(std::string_view scheme) {
    return normalized_auth_scheme(scheme) == "mtls";
}

[[nodiscard]] bool resolve_llm_secret_handle(std::string_view secret_handle,
                                             std::string_view diagnostic_name,
                                             SecretManager &secrets,
                                             std::string &credential,
                                             std::ostream &err) {
    auto resolved = secrets.get(secret_handle, "ahflc run");
    if (!resolved.has_value() || resolved->empty()) {
        err << "error: failed to resolve " << diagnostic_name << " '" << secret_handle
            << "' from configured secret providers\n";
        return false;
    }
    credential = std::move(*resolved);
    return true;
}

void refresh_llm_secret_handle_if_requested(bool refresh_secrets_before_use,
                                            std::string_view secret_handle,
                                            std::string_view diagnostic_name,
                                            SecretManager &secrets) {
    if (refresh_secrets_before_use && !secret_handle.empty()) {
        secrets.refresh(secret_handle, diagnostic_name);
    }
}

[[nodiscard]] bool resolve_llm_mtls_identity(std::string_view owner,
                                             std::string_view cert_secret,
                                             std::string_view key_secret,
                                             std::string_view ca_secret,
                                             std::string &cert_path,
                                             std::string &key_path,
                                             std::string &ca_path,
                                             bool refresh_secrets_before_use,
                                             SecretManager &secrets,
                                             std::ostream &err) {
    if (!cert_secret.empty()) {
        const auto diagnostic = std::string(owner) + " mtls_client_cert_secret";
        refresh_llm_secret_handle_if_requested(
            refresh_secrets_before_use, cert_secret, diagnostic, secrets);
        if (!resolve_llm_secret_handle(cert_secret, diagnostic, secrets, cert_path, err)) {
            return false;
        }
    }
    if (!key_secret.empty()) {
        const auto diagnostic = std::string(owner) + " mtls_client_key_secret";
        refresh_llm_secret_handle_if_requested(
            refresh_secrets_before_use, key_secret, diagnostic, secrets);
        if (!resolve_llm_secret_handle(key_secret, diagnostic, secrets, key_path, err)) {
            return false;
        }
    }
    if (!ca_secret.empty()) {
        const auto diagnostic = std::string(owner) + " mtls_ca_cert_secret";
        refresh_llm_secret_handle_if_requested(
            refresh_secrets_before_use, ca_secret, diagnostic, secrets);
        if (!resolve_llm_secret_handle(ca_secret, diagnostic, secrets, ca_path, err)) {
            return false;
        }
    }
    if (cert_path.empty()) {
        err << "error: " << owner
            << " auth_scheme 'mtls' requires mtls_client_cert_path or mtls_client_cert_secret\n";
        return false;
    }
    if (key_path.empty()) {
        err << "error: " << owner
            << " auth_scheme 'mtls' requires mtls_client_key_path or mtls_client_key_secret\n";
        return false;
    }
    return true;
}

[[nodiscard]] bool resolve_llm_credentials(ahfl::llm_provider::LLMProviderConfig &config,
                                           SecretManager &secrets,
                                           std::ostream &err) {
    const auto primary_uses_oauth2 = is_oauth2_auth_scheme(config.auth_scheme);
    const auto primary_uses_mtls = is_mtls_auth_scheme(config.auth_scheme);
    if (primary_uses_mtls && !resolve_llm_mtls_identity("LLM config",
                                                        config.mtls_client_cert_secret,
                                                        config.mtls_client_key_secret,
                                                        config.mtls_ca_cert_secret,
                                                        config.mtls_client_cert_path,
                                                        config.mtls_client_key_path,
                                                        config.mtls_ca_cert_path,
                                                        config.refresh_secrets_before_use,
                                                        secrets,
                                                        err)) {
        return false;
    }

    if (primary_uses_oauth2 && !config.oauth2_token_secret.empty()) {
        refresh_llm_secret_handle_if_requested(config.refresh_secrets_before_use,
                                               config.oauth2_token_secret,
                                               "LLM oauth2_token_secret",
                                               secrets);
        if (!resolve_llm_secret_handle(config.oauth2_token_secret,
                                       "LLM oauth2_token_secret",
                                       secrets,
                                       config.api_key,
                                       err)) {
            return false;
        }
    } else if (!config.api_key_secret.empty()) {
        refresh_llm_secret_handle_if_requested(config.refresh_secrets_before_use,
                                               config.api_key_secret,
                                               "LLM api_key_secret",
                                               secrets);
        if (!resolve_llm_secret_handle(
                config.api_key_secret, "LLM api_key_secret", secrets, config.api_key, err)) {
            return false;
        }
    }

    if (!primary_uses_mtls && config.api_key.empty()) {
        if (primary_uses_oauth2) {
            err << "error: LLM config auth_scheme '" << config.auth_scheme
                << "' requires oauth2_token_secret, api_key_secret, or api_key\n";
        } else {
            err << "error: LLM config requires api_key or api_key_secret\n";
        }
        return false;
    }

    for (auto &provider : config.fallback_providers) {
        const auto provider_scheme =
            provider.auth_scheme.empty() ? config.auth_scheme : provider.auth_scheme;
        const auto provider_uses_oauth2 = is_oauth2_auth_scheme(provider_scheme);
        const auto provider_uses_mtls = is_mtls_auth_scheme(provider_scheme);
        if (provider_uses_mtls) {
            std::string owner = "LLM fallback provider '";
            owner += provider.name;
            owner += "'";
            std::string cert_path = provider.mtls_client_cert_path.empty()
                                        ? config.mtls_client_cert_path
                                        : provider.mtls_client_cert_path;
            std::string key_path = provider.mtls_client_key_path.empty()
                                       ? config.mtls_client_key_path
                                       : provider.mtls_client_key_path;
            std::string ca_path = provider.mtls_ca_cert_path.empty() ? config.mtls_ca_cert_path
                                                                     : provider.mtls_ca_cert_path;
            if (!resolve_llm_mtls_identity(owner,
                                           provider.mtls_client_cert_secret,
                                           provider.mtls_client_key_secret,
                                           provider.mtls_ca_cert_secret,
                                           cert_path,
                                           key_path,
                                           ca_path,
                                           config.refresh_secrets_before_use,
                                           secrets,
                                           err)) {
                return false;
            }
            provider.mtls_client_cert_path = std::move(cert_path);
            provider.mtls_client_key_path = std::move(key_path);
            provider.mtls_ca_cert_path = std::move(ca_path);
        }

        if (provider_uses_oauth2 && !provider.oauth2_token_secret.empty()) {
            std::string diagnostic = "LLM fallback provider '";
            diagnostic += provider.name;
            diagnostic += "' oauth2_token_secret";
            refresh_llm_secret_handle_if_requested(config.refresh_secrets_before_use,
                                                   provider.oauth2_token_secret,
                                                   diagnostic,
                                                   secrets);
            if (!resolve_llm_secret_handle(
                    provider.oauth2_token_secret, diagnostic, secrets, provider.api_key, err)) {
                return false;
            }
        } else if (!provider.api_key_secret.empty()) {
            std::string diagnostic = "LLM fallback provider '";
            diagnostic += provider.name;
            diagnostic += "' api_key_secret";
            refresh_llm_secret_handle_if_requested(
                config.refresh_secrets_before_use, provider.api_key_secret, diagnostic, secrets);
            if (!resolve_llm_secret_handle(
                    provider.api_key_secret, diagnostic, secrets, provider.api_key, err)) {
                return false;
            }
        }

        if (!provider_uses_mtls && provider.api_key.empty()) {
            if (provider_uses_oauth2) {
                err << "error: LLM fallback provider '" << provider.name << "' auth_scheme '"
                    << provider_scheme
                    << "' requires oauth2_token_secret, api_key_secret, or api_key\n";
            } else {
                err << "error: LLM fallback provider '" << provider.name
                    << "' requires api_key or api_key_secret\n";
            }
            return false;
        }
    }

    return true;
}

[[nodiscard]] std::uint64_t fnv1a_hash(std::string_view value) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto ch : value) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string hex_hash(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::nouppercase << value;
    return out.str();
}

[[nodiscard]] std::string tool_name_for_selector(std::string_view selector) {
    std::string tool_name{"ahfl_"};
    tool_name.reserve(tool_name.size() + selector.size());
    for (const auto ch : selector) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0 || ch == '_' || ch == '-') {
            tool_name.push_back(ch);
        } else {
            tool_name.push_back('_');
        }
    }

    constexpr std::size_t kMaxToolNameLength = 64;
    if (tool_name.size() > kMaxToolNameLength) {
        const auto suffix = "_" + hex_hash(fnv1a_hash(selector));
        tool_name.resize(kMaxToolNameLength - suffix.size());
        tool_name += suffix;
    }
    return tool_name;
}

[[nodiscard]] std::optional<std::string> mock_selector(const ahfl::dry_run::CapabilityMock &mock) {
    if (mock.capability_name.has_value()) {
        return *mock.capability_name;
    }
    if (mock.binding_key.has_value()) {
        return *mock.binding_key;
    }
    return std::nullopt;
}

[[nodiscard]] std::string make_tool_error_content(std::string_view tool_name,
                                                  std::string_view message) {
    auto root = ahfl::json::JsonValue::make_object();
    root->set("error", ahfl::json::JsonValue::make_string(std::string(message)));
    root->set("tool", ahfl::json::JsonValue::make_string(std::string(tool_name)));
    return ahfl::json::serialize_json(*root);
}

[[nodiscard]] std::optional<ahfl::dry_run::CapabilityMockSet>
load_capability_mock_set(const CommandLineOptions &options, std::ostream &err) {
    if (!options.capability_mocks_descriptor.has_value()) {
        return std::nullopt;
    }

    const std::filesystem::path path{std::string(*options.capability_mocks_descriptor)};
    auto content = read_text_file(path, "capability mocks", err);
    if (!content.has_value()) {
        return std::nullopt;
    }

    auto parse_result = ahfl::dry_run::parse_capability_mock_set_json(*content);
    if (parse_result.has_errors() || !parse_result.mock_set.has_value()) {
        parse_result.diagnostics.render(err);
        return std::nullopt;
    }

    return std::move(*parse_result.mock_set);
}

struct RuntimeToolCatalogEntry {
    ToolDefinition definition;
    enum class OutcomeKind {
        Result,
        Error,
        Timeout,
    };

    struct Outcome {
        OutcomeKind kind{OutcomeKind::Result};
        std::optional<ahfl::evaluator::Value> result{};
        std::string error_message{};
        std::chrono::milliseconds timeout{0};
    };

    Outcome outcome;
};

struct RuntimeToolCatalogDescriptor {
    std::vector<RuntimeToolCatalogEntry> entries;
};

struct RuntimeToolSet {
    std::vector<ToolDefinition> tools;
    std::shared_ptr<CapabilityRegistry> registry{std::make_shared<CapabilityRegistry>()};
    std::unordered_set<std::string> names;
};

[[nodiscard]] std::optional<std::string>
required_json_string_field(const ahfl::json::JsonValue &object,
                           std::string_view field_name,
                           std::string_view context,
                           std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        err << "error: " << context << " requires string field '" << field_name << "'\n";
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be a string\n";
        return std::nullopt;
    }
    return std::string(*value);
}

[[nodiscard]] std::optional<std::string>
optional_json_string_field(const ahfl::json::JsonValue &object,
                           std::string_view field_name,
                           std::string_view context,
                           std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_string();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be a string\n";
        return std::nullopt;
    }
    return std::string(*value);
}

[[nodiscard]] std::optional<std::int64_t>
required_positive_json_int_field(const ahfl::json::JsonValue &object,
                                 std::string_view field_name,
                                 std::string_view context,
                                 std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        err << "error: " << context << " requires integer field '" << field_name << "'\n";
        return std::nullopt;
    }
    auto value = field->as_int();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be an integer\n";
        return std::nullopt;
    }
    if (*value <= 0) {
        err << "error: " << context << " field '" << field_name << "' must be positive\n";
        return std::nullopt;
    }
    return *value;
}

[[nodiscard]] std::optional<std::int64_t>
optional_json_int_field(const ahfl::json::JsonValue &object,
                        std::string_view field_name,
                        std::string_view context,
                        std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_int();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be an integer\n";
        return std::nullopt;
    }
    return *value;
}

[[nodiscard]] std::optional<double> optional_json_number_field(const ahfl::json::JsonValue &object,
                                                               std::string_view field_name,
                                                               std::string_view context,
                                                               std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_float();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be a number\n";
        return std::nullopt;
    }
    return *value;
}

[[nodiscard]] std::optional<bool> optional_json_bool_field(const ahfl::json::JsonValue &object,
                                                           std::string_view field_name,
                                                           std::string_view context,
                                                           std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        return std::nullopt;
    }
    auto value = field->as_bool();
    if (!value.has_value()) {
        err << "error: " << context << " field '" << field_name << "' must be a boolean\n";
        return std::nullopt;
    }
    return *value;
}

[[nodiscard]] bool assign_optional_milliseconds_field(const ahfl::json::JsonValue &object,
                                                      std::string_view field_name,
                                                      std::string_view context,
                                                      std::chrono::milliseconds &target,
                                                      bool require_positive,
                                                      std::ostream &err) {
    auto value = optional_json_int_field(object, field_name, context, err);
    if (object.get(field_name) != nullptr && !value.has_value()) {
        return false;
    }
    if (!value.has_value()) {
        return true;
    }
    if ((require_positive && *value <= 0) || (!require_positive && *value < 0)) {
        err << "error: " << context << " field '" << field_name << "' must be "
            << (require_positive ? "positive" : "non-negative") << "\n";
        return false;
    }
    target = std::chrono::milliseconds{*value};
    return true;
}

[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>>
optional_string_map_field(const ahfl::json::JsonValue &object,
                          std::string_view field_name,
                          std::string_view context,
                          std::ostream &err) {
    const auto *field = object.get(field_name);
    if (field == nullptr) {
        return std::nullopt;
    }
    if (!field->is_object()) {
        err << "error: " << context << " field '" << field_name << "' must be an object\n";
        return std::nullopt;
    }
    std::unordered_map<std::string, std::string> result;
    for (const auto &[key, value] : field->object_fields) {
        if (value == nullptr) {
            err << "error: " << context << " field '" << field_name << "." << key
                << "' must be a string\n";
            return std::nullopt;
        }
        auto string_value = value->as_string();
        if (!string_value.has_value()) {
            err << "error: " << context << " field '" << field_name << "." << key
                << "' must be a string\n";
            return std::nullopt;
        }
        result.emplace(key, std::string(*string_value));
    }
    return result;
}

[[nodiscard]] std::optional<CapabilityResponseFormat> runtime_binding_response_format(
    const ahfl::json::JsonValue &object, std::string_view context, std::ostream &err) {
    auto response_format = optional_json_string_field(object, "response_format", context, err);
    if (object.get("response_format") != nullptr && !response_format.has_value()) {
        return std::nullopt;
    }
    if (!response_format.has_value() || *response_format == "json") {
        return CapabilityResponseFormat::Json;
    }
    if (*response_format == "text" || *response_format == "text_plain") {
        return CapabilityResponseFormat::TextPlain;
    }
    err << "error: " << context << " field 'response_format' must be 'json' or 'text_plain'\n";
    return std::nullopt;
}

[[nodiscard]] bool apply_retry_config(const ahfl::json::JsonValue &object,
                                      std::string_view context,
                                      RetryConfig &retry,
                                      std::ostream &err) {
    if (auto max_retries = optional_json_int_field(object, "max_retries", context, err);
        object.get("max_retries") != nullptr) {
        if (!max_retries.has_value()) {
            return false;
        }
        if (*max_retries < 0) {
            err << "error: " << context << " field 'max_retries' must be non-negative\n";
            return false;
        }
        retry.max_retries = static_cast<std::size_t>(*max_retries);
    }
    if (!assign_optional_milliseconds_field(
            object, "initial_delay_ms", context, retry.initial_delay, false, err)) {
        return false;
    }
    if (auto multiplier = optional_json_number_field(object, "backoff_multiplier", context, err);
        object.get("backoff_multiplier") != nullptr) {
        if (!multiplier.has_value()) {
            return false;
        }
        if (*multiplier <= 0.0) {
            err << "error: " << context << " field 'backoff_multiplier' must be greater than 0\n";
            return false;
        }
        retry.backoff_multiplier = *multiplier;
    }

    const auto *retry_field = object.get("retry");
    if (retry_field == nullptr) {
        return true;
    }
    if (!retry_field->is_object()) {
        err << "error: " << context << " field 'retry' must be an object\n";
        return false;
    }
    const auto retry_context = std::string(context) + ".retry";
    return apply_retry_config(*retry_field, retry_context, retry, err);
}

[[nodiscard]] bool apply_timeout_config(const ahfl::json::JsonValue &object,
                                        std::string_view context,
                                        TimeoutConfig &timeout,
                                        std::ostream &err) {
    return assign_optional_milliseconds_field(
        object, "timeout_ms", context, timeout.deadline, true, err);
}

[[nodiscard]] bool apply_circuit_breaker_config(const ahfl::json::JsonValue &object,
                                                std::string_view context,
                                                CircuitBreakerConfig &config,
                                                std::ostream &err) {
    const auto *field = object.get("circuit_breaker");
    if (field == nullptr) {
        return true;
    }
    if (!field->is_object()) {
        err << "error: " << context << " field 'circuit_breaker' must be an object\n";
        return false;
    }

    const auto cb_context = std::string(context) + ".circuit_breaker";
    if (auto enabled = optional_json_bool_field(*field, "enabled", cb_context, err);
        field->get("enabled") != nullptr) {
        if (!enabled.has_value()) {
            return false;
        }
        config.enabled = *enabled;
    }
    if (auto threshold = optional_json_int_field(*field, "failure_threshold", cb_context, err);
        field->get("failure_threshold") != nullptr) {
        if (!threshold.has_value()) {
            return false;
        }
        if (*threshold <= 0) {
            err << "error: " << cb_context << " field 'failure_threshold' must be positive\n";
            return false;
        }
        config.failure_threshold = static_cast<std::size_t>(*threshold);
    }
    std::chrono::milliseconds recovery_window{config.recovery_window};
    if (!assign_optional_milliseconds_field(
            *field, "recovery_window_ms", cb_context, recovery_window, true, err)) {
        return false;
    }
    config.recovery_window = std::chrono::duration_cast<std::chrono::seconds>(recovery_window);
    return true;
}

[[nodiscard]] std::optional<AuthConfig> runtime_binding_auth_config(
    const ahfl::json::JsonValue &object, std::string_view context, std::ostream &err) {
    const auto *auth = object.get("auth");
    if (auth == nullptr) {
        return std::nullopt;
    }
    if (!auth->is_object()) {
        err << "error: " << context << " field 'auth' must be an object\n";
        return std::nullopt;
    }

    const auto auth_context = std::string(context) + ".auth";
    auto scheme = required_json_string_field(*auth, "scheme", auth_context, err);
    if (!scheme.has_value()) {
        return std::nullopt;
    }

    AuthConfig config;
    const auto normalized = normalized_auth_scheme(*scheme);
    if (normalized == "none") {
        config.scheme = AuthScheme::None;
    } else if (normalized == "bearer" || normalized == "bearer_token") {
        config.scheme = AuthScheme::BearerToken;
    } else if (normalized == "oauth2_bearer" || normalized == "oauth2_client_credentials") {
        config.scheme = AuthScheme::OAuth2ClientCredentials;
    } else if (normalized == "mtls") {
        config.scheme = AuthScheme::MTLS;
    } else {
        err << "error: " << auth_context
            << " field 'scheme' must be 'none', 'bearer', 'oauth2_client_credentials', or 'mtls'\n";
        return std::nullopt;
    }

    auto token_key = optional_json_string_field(*auth, "token_key", auth_context, err);
    if (auth->get("token_key") != nullptr && !token_key.has_value()) {
        return std::nullopt;
    }
    if (token_key.has_value()) {
        config.token_key = std::move(*token_key);
    }
    auto client_id_key = optional_json_string_field(*auth, "client_id_key", auth_context, err);
    if (auth->get("client_id_key") != nullptr && !client_id_key.has_value()) {
        return std::nullopt;
    }
    if (client_id_key.has_value()) {
        config.client_id_key = std::move(*client_id_key);
    }
    auto cert_path_key = optional_json_string_field(*auth, "cert_path_key", auth_context, err);
    if (auth->get("cert_path_key") != nullptr && !cert_path_key.has_value()) {
        return std::nullopt;
    }
    if (cert_path_key.has_value()) {
        config.cert_path_key = std::move(*cert_path_key);
    }
    auto key_path_key = optional_json_string_field(*auth, "key_path_key", auth_context, err);
    if (auth->get("key_path_key") != nullptr && !key_path_key.has_value()) {
        return std::nullopt;
    }
    if (key_path_key.has_value()) {
        config.key_path_key = std::move(*key_path_key);
    }
    auto token_url = optional_json_string_field(*auth, "token_url", auth_context, err);
    if (auth->get("token_url") != nullptr && !token_url.has_value()) {
        return std::nullopt;
    }
    if (token_url.has_value()) {
        config.token_url = std::move(*token_url);
    }
    return config;
}

[[nodiscard]] std::unique_ptr<ahfl::ir::TypeRef>
clone_type_ref_ptr(const ahfl::ir::TypeRef *type_ref) {
    if (type_ref == nullptr) {
        return nullptr;
    }
    auto clone = std::make_unique<ahfl::ir::TypeRef>();
    clone->kind = type_ref->kind;
    clone->display_name = type_ref->display_name;
    clone->canonical_name = type_ref->canonical_name;
    clone->variant_name = type_ref->variant_name;
    clone->string_bounds = type_ref->string_bounds;
    clone->decimal_scale = type_ref->decimal_scale;
    clone->source_range = type_ref->source_range;
    clone->first = clone_type_ref_ptr(type_ref->first.get());
    clone->second = clone_type_ref_ptr(type_ref->second.get());
    return clone;
}

[[nodiscard]] std::shared_ptr<const ahfl::ir::TypeRef>
capability_response_schema(const ahfl::ir::CapabilityDecl &capability) {
    auto clone = clone_type_ref_ptr(&capability.return_type_ref);
    return std::shared_ptr<const ahfl::ir::TypeRef>(std::move(clone));
}

[[nodiscard]] std::shared_ptr<CapabilityRegistry>
load_runtime_capability_bindings(const ahfl::ir::Program &program,
                                 const CommandLineOptions &options,
                                 std::shared_ptr<SecretManager> secrets,
                                 std::ostream &err) {
    if (!options.capability_bindings_descriptor.has_value()) {
        return nullptr;
    }

    const std::filesystem::path path{std::string(*options.capability_bindings_descriptor)};
    auto content = read_text_file(path, "runtime capability bindings", err);
    if (!content.has_value()) {
        return nullptr;
    }

    auto parsed = ahfl::json::parse_json(*content);
    if (!parsed.has_value() || !*parsed || !(**parsed).is_object()) {
        err << "error: failed to parse runtime capability bindings JSON\n";
        return nullptr;
    }
    const auto &root = **parsed;

    auto schema = required_json_string_field(root, "schema", "runtime capability bindings", err);
    if (!schema.has_value()) {
        return nullptr;
    }
    if (*schema != "ahfl.runtime_capability_bindings.v0") {
        err << "error: unsupported runtime capability bindings schema '" << *schema << "'\n";
        return nullptr;
    }

    const auto *bindings = root.get("bindings");
    if (bindings == nullptr || !bindings->is_array()) {
        err << "error: runtime capability bindings requires array field 'bindings'\n";
        return nullptr;
    }

    const ahfl::ir::ProgramIndex index{program};
    auto registry = std::make_shared<CapabilityRegistry>();
    std::unordered_set<std::string> names;
    for (std::size_t binding_index = 0; binding_index < bindings->array_items.size();
         ++binding_index) {
        const auto &item = bindings->array_items[binding_index];
        const auto context = "runtime capability bindings[" + std::to_string(binding_index) + "]";
        if (item == nullptr || !item->is_object()) {
            err << "error: " << context << " must be an object\n";
            return nullptr;
        }

        auto capability_name = required_json_string_field(*item, "capability", context, err);
        if (!capability_name.has_value()) {
            return nullptr;
        }
        if (capability_name->empty()) {
            err << "error: " << context << " field 'capability' must not be empty\n";
            return nullptr;
        }
        if (!names.insert(*capability_name).second) {
            err << "error: duplicate runtime capability binding for '" << *capability_name << "'\n";
            return nullptr;
        }

        const auto *capability = index.find_capability(*capability_name);
        if (capability == nullptr) {
            err << "error: " << context << " references unknown capability '" << *capability_name
                << "'\n";
            return nullptr;
        }

        auto transport = required_json_string_field(*item, "transport", context, err);
        if (!transport.has_value()) {
            return nullptr;
        }
        auto response_format = runtime_binding_response_format(*item, context, err);
        if (!response_format.has_value()) {
            return nullptr;
        }

        if (*transport == "http") {
            HTTPCapabilityConfig config;
            auto url = required_json_string_field(*item, "url", context, err);
            if (!url.has_value()) {
                return nullptr;
            }
            config.url = std::move(*url);
            if (auto method = optional_json_string_field(*item, "method", context, err);
                item->get("method") != nullptr) {
                if (!method.has_value()) {
                    return nullptr;
                }
                if (method->empty()) {
                    err << "error: " << context << " field 'method' must not be empty\n";
                    return nullptr;
                }
                config.method = std::move(*method);
            }
            if (auto headers = optional_string_map_field(*item, "headers", context, err);
                item->get("headers") != nullptr) {
                if (!headers.has_value()) {
                    return nullptr;
                }
                config.headers = std::move(*headers);
            }
            config.response_format = *response_format;
            if (!apply_timeout_config(*item, context, config.timeout, err) ||
                !apply_retry_config(*item, context, config.retry, err) ||
                !apply_circuit_breaker_config(*item, context, config.circuit_breaker, err)) {
                return nullptr;
            }
            if (auto auth = runtime_binding_auth_config(*item, context, err);
                item->get("auth") != nullptr) {
                if (!auth.has_value()) {
                    return nullptr;
                }
                config.auth = std::move(*auth);
                config.secret_manager = secrets;
            }
            config.response_schema = capability_response_schema(*capability);
            registry->register_capability(
                make_http_capability(*capability_name, std::move(config)));
            continue;
        }

        if (*transport == "grpc_json_transcoding") {
            GrpcJsonTranscodingCapabilityConfig config;
            auto endpoint = required_json_string_field(*item, "endpoint", context, err);
            if (!endpoint.has_value()) {
                return nullptr;
            }
            auto service = required_json_string_field(*item, "service", context, err);
            if (!service.has_value()) {
                return nullptr;
            }
            auto method = required_json_string_field(*item, "method", context, err);
            if (!method.has_value()) {
                return nullptr;
            }
            config.endpoint = std::move(*endpoint);
            config.service = std::move(*service);
            config.method = std::move(*method);
            config.response_format = *response_format;
            if (!apply_timeout_config(*item, context, config.timeout, err) ||
                !apply_retry_config(*item, context, config.retry, err) ||
                !apply_circuit_breaker_config(*item, context, config.circuit_breaker, err)) {
                return nullptr;
            }
            if (auto auth = runtime_binding_auth_config(*item, context, err);
                item->get("auth") != nullptr) {
                if (!auth.has_value()) {
                    return nullptr;
                }
                config.auth = std::move(*auth);
                config.secret_manager = secrets;
            }
            config.response_schema = capability_response_schema(*capability);
            registry->register_capability(
                make_grpc_json_transcoding_capability(*capability_name, std::move(config)));
            continue;
        }

        err << "error: " << context
            << " field 'transport' must be 'http' or 'grpc_json_transcoding'\n";
        return nullptr;
    }

    return registry;
}

[[nodiscard]] bool validate_tool_parameters_schema(std::string_view schema_json,
                                                   std::string_view context,
                                                   std::ostream &err) {
    auto parsed = ahfl::json::parse_json(schema_json);
    if (!parsed.has_value() || !*parsed || !(**parsed).is_object()) {
        err << "error: " << context << " parameters schema must be a JSON object\n";
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::string> tool_parameters_schema_json(
    const ahfl::json::JsonValue &object, std::string_view context, std::ostream &err) {
    constexpr std::string_view kDefaultParametersSchema =
        R"({"type":"object","additionalProperties":true})";

    if (const auto *schema_field = object.get("params_schema_json"); schema_field != nullptr) {
        std::string schema_json;
        if (auto schema_string = schema_field->as_string(); schema_string.has_value()) {
            schema_json = std::string(*schema_string);
        } else {
            schema_json = ahfl::json::serialize_json(*schema_field);
        }
        if (!validate_tool_parameters_schema(schema_json, context, err)) {
            return std::nullopt;
        }
        return schema_json;
    }

    if (const auto *parameters_field = object.get("parameters"); parameters_field != nullptr) {
        auto schema_json = ahfl::json::serialize_json(*parameters_field);
        if (!validate_tool_parameters_schema(schema_json, context, err)) {
            return std::nullopt;
        }
        return schema_json;
    }

    return std::string(kDefaultParametersSchema);
}

[[nodiscard]] std::optional<RuntimeToolCatalogEntry::Outcome> tool_catalog_outcome(
    const ahfl::json::JsonValue &object, std::string_view context, std::ostream &err) {
    const auto *result_field = object.get("result");
    const auto *failure_field = object.get("failure");
    if ((result_field == nullptr) == (failure_field == nullptr)) {
        err << "error: " << context << " requires exactly one of 'result' or 'failure'\n";
        return std::nullopt;
    }

    if (result_field != nullptr) {
        auto result = value_from_json(ahfl::json::serialize_json(*result_field));
        if (!result.has_value()) {
            err << "error: " << context << " field 'result' must be AHFL value JSON\n";
            return std::nullopt;
        }
        return RuntimeToolCatalogEntry::Outcome{
            .kind = RuntimeToolCatalogEntry::OutcomeKind::Result,
            .result = std::move(*result),
        };
    }

    if (!failure_field->is_object()) {
        err << "error: " << context << " field 'failure' must be an object\n";
        return std::nullopt;
    }

    const auto failure_context = std::string(context) + ".failure";
    auto kind = required_json_string_field(*failure_field, "kind", failure_context, err);
    if (!kind.has_value()) {
        return std::nullopt;
    }
    if (*kind == "error") {
        auto message = required_json_string_field(*failure_field, "message", failure_context, err);
        if (!message.has_value()) {
            return std::nullopt;
        }
        return RuntimeToolCatalogEntry::Outcome{
            .kind = RuntimeToolCatalogEntry::OutcomeKind::Error,
            .error_message = std::move(*message),
        };
    }
    if (*kind == "timeout") {
        auto timeout_ms =
            required_positive_json_int_field(*failure_field, "timeout_ms", failure_context, err);
        if (!timeout_ms.has_value()) {
            return std::nullopt;
        }
        auto message = optional_json_string_field(*failure_field, "message", failure_context, err);
        if (failure_field->get("message") != nullptr && !message.has_value()) {
            return std::nullopt;
        }
        if (!message.has_value() || message->empty()) {
            message = "tool timed out after " + std::to_string(*timeout_ms) + "ms";
        }
        return RuntimeToolCatalogEntry::Outcome{
            .kind = RuntimeToolCatalogEntry::OutcomeKind::Timeout,
            .error_message = std::move(*message),
            .timeout = std::chrono::milliseconds{*timeout_ms},
        };
    }

    err << "error: " << failure_context << " field 'kind' must be 'error' or 'timeout'\n";
    return std::nullopt;
}

[[nodiscard]] std::optional<RuntimeToolCatalogDescriptor>
load_tool_catalog_descriptor(const CommandLineOptions &options, std::ostream &err) {
    if (!options.tool_catalog_descriptor.has_value()) {
        return std::nullopt;
    }

    const std::filesystem::path path{std::string(*options.tool_catalog_descriptor)};
    auto content = read_text_file(path, "LLM tool catalog", err);
    if (!content.has_value()) {
        return std::nullopt;
    }

    auto parsed = ahfl::json::parse_json(*content);
    if (!parsed.has_value() || !*parsed || !(**parsed).is_object()) {
        err << "error: failed to parse LLM tool catalog JSON\n";
        return std::nullopt;
    }
    const auto &root = **parsed;

    auto schema = required_json_string_field(root, "schema", "LLM tool catalog", err);
    if (!schema.has_value()) {
        return std::nullopt;
    }
    if (*schema != "ahfl.llm_tool_catalog.v0") {
        err << "error: unsupported LLM tool catalog schema '" << *schema << "'\n";
        return std::nullopt;
    }

    const auto *tools = root.get("tools");
    if (tools == nullptr || !tools->is_array()) {
        err << "error: LLM tool catalog requires array field 'tools'\n";
        return std::nullopt;
    }

    RuntimeToolCatalogDescriptor descriptor;
    descriptor.entries.reserve(tools->array_items.size());
    for (std::size_t index = 0; index < tools->array_items.size(); ++index) {
        const auto &item = tools->array_items[index];
        const auto context = "LLM tool catalog tools[" + std::to_string(index) + "]";
        if (item == nullptr || !item->is_object()) {
            err << "error: " << context << " must be an object\n";
            return std::nullopt;
        }

        auto name = required_json_string_field(*item, "name", context, err);
        if (!name.has_value()) {
            return std::nullopt;
        }
        if (name->empty()) {
            err << "error: " << context << " field 'name' must not be empty\n";
            return std::nullopt;
        }

        auto description = optional_json_string_field(*item, "description", context, err);
        if (item->get("description") != nullptr && !description.has_value()) {
            return std::nullopt;
        }
        if (!description.has_value() || description->empty()) {
            description = "Invoke AHFL runtime tool '" + *name + "'";
        }

        auto params_schema = tool_parameters_schema_json(*item, context, err);
        if (!params_schema.has_value()) {
            return std::nullopt;
        }

        auto outcome = tool_catalog_outcome(*item, context, err);
        if (!outcome.has_value()) {
            return std::nullopt;
        }

        descriptor.entries.push_back(RuntimeToolCatalogEntry{
            .definition =
                ToolDefinition{
                    .name = std::move(*name),
                    .description = std::move(*description),
                    .params_schema_json = std::move(*params_schema),
                },
            .outcome = std::move(*outcome),
        });
    }

    return descriptor;
}

[[nodiscard]] bool add_runtime_tool(RuntimeToolSet &runtime_tools,
                                    ToolDefinition definition,
                                    ahfl::evaluator::Value result,
                                    std::string_view source,
                                    std::ostream &err) {
    const auto tool_name = definition.name;
    if (!runtime_tools.names.insert(tool_name).second) {
        err << "error: duplicate LLM runtime tool name '" << tool_name << "' from " << source
            << "\n";
        return false;
    }

    runtime_tools.registry->register_mock(tool_name, std::move(result));
    runtime_tools.tools.push_back(std::move(definition));
    return true;
}

[[nodiscard]] bool add_mock_tools(RuntimeToolSet &runtime_tools,
                                  const ahfl::dry_run::CapabilityMockSet &mock_set,
                                  std::ostream &err) {
    for (const auto &mock : mock_set.mocks) {
        auto selector = mock_selector(mock);
        if (!selector.has_value()) {
            err << "error: capability mock must specify capability_name or binding_key\n";
            return false;
        }

        const auto tool_name = tool_name_for_selector(*selector);
        std::string description = "Invoke AHFL capability mock '" + *selector + "'";
        if (mock.invocation_label.has_value() && !mock.invocation_label->empty()) {
            description += " for invocation '" + *mock.invocation_label + "'";
        }
        if (!add_runtime_tool(
                runtime_tools,
                ToolDefinition{
                    .name = tool_name,
                    .description = std::move(description),
                    .params_schema_json = R"({"type":"object","additionalProperties":true})",
                },
                ahfl::evaluator::make_string(mock.result_fixture),
                "capability mocks",
                err)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] ahfl::runtime::CapabilityBinding
make_catalog_capability_binding(std::string name, RuntimeToolCatalogEntry::Outcome outcome) {
    ahfl::runtime::CapabilityBinding binding;
    binding.name = std::move(name);
    auto shared_outcome = std::make_shared<RuntimeToolCatalogEntry::Outcome>(std::move(outcome));
    binding.handler =
        [shared_outcome](
            const std::vector<ahfl::evaluator::Value> &) -> ahfl::runtime::CapabilityCallResult {
        switch (shared_outcome->kind) {
        case RuntimeToolCatalogEntry::OutcomeKind::Result:
            if (shared_outcome->result.has_value()) {
                return ahfl::runtime::CapabilityCallResult{
                    .status = CapabilityCallStatus::Success,
                    .value = ahfl::evaluator::clone_value(*shared_outcome->result),
                    .error_message = {},
                    .attempts = 1,
                };
            }
            return ahfl::runtime::CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = "catalog tool result is missing",
                .attempts = 1,
            };
        case RuntimeToolCatalogEntry::OutcomeKind::Error:
            return ahfl::runtime::CapabilityCallResult{
                .status = CapabilityCallStatus::Error,
                .value = std::nullopt,
                .error_message = shared_outcome->error_message,
                .attempts = 1,
            };
        case RuntimeToolCatalogEntry::OutcomeKind::Timeout:
            return ahfl::runtime::CapabilityCallResult{
                .status = CapabilityCallStatus::Timeout,
                .value = std::nullopt,
                .error_message = shared_outcome->error_message,
                .attempts = 1,
            };
        }
        return ahfl::runtime::CapabilityCallResult{
            .status = CapabilityCallStatus::Error,
            .value = std::nullopt,
            .error_message = "catalog tool execution failed",
            .attempts = 1,
        };
    };
    return binding;
}

[[nodiscard]] bool add_catalog_tools(RuntimeToolSet &runtime_tools,
                                     RuntimeToolCatalogDescriptor &descriptor,
                                     std::ostream &err) {
    for (auto &entry : descriptor.entries) {
        const auto tool_name = entry.definition.name;
        if (!runtime_tools.names.insert(tool_name).second) {
            err << "error: duplicate LLM runtime tool name '" << tool_name
                << "' from LLM tool catalog\n";
            return false;
        }
        runtime_tools.registry->register_capability(
            make_catalog_capability_binding(tool_name, std::move(entry.outcome)));
        runtime_tools.tools.push_back(std::move(entry.definition));
    }
    return true;
}

void install_runtime_tools(LLMCapabilityProvider &provider, RuntimeToolSet runtime_tools) {
    if (runtime_tools.tools.empty()) {
        return;
    }

    provider.set_tools(
        std::move(runtime_tools.tools),
        [tool_registry =
             std::move(runtime_tools.registry)](const ToolCall &tool_call) -> ToolCallResult {
            std::vector<ahfl::evaluator::Value> args;
            if (!tool_call.arguments_json.empty()) {
                auto parsed_args = value_from_json(tool_call.arguments_json);
                if (!parsed_args.has_value()) {
                    return ToolCallResult{
                        .tool_call_id = tool_call.id,
                        .content =
                            make_tool_error_content(tool_call.name, "invalid tool arguments JSON"),
                        .success = false,
                        .error_message = "invalid tool arguments JSON",
                    };
                }
                args.push_back(std::move(*parsed_args));
            }

            auto result = tool_registry->invoke(tool_call.name, args);
            if (result.status != CapabilityCallStatus::Success || !result.value.has_value()) {
                const auto error_message = result.error_message.empty()
                                               ? std::string{"tool execution failed"}
                                               : result.error_message;
                return ToolCallResult{
                    .tool_call_id = tool_call.id,
                    .content = make_tool_error_content(tool_call.name, error_message),
                    .success = false,
                    .error_message = error_message,
                };
            }

            return ToolCallResult{
                .tool_call_id = tool_call.id,
                .content = value_to_json(*result.value),
            };
        });
}

[[nodiscard]] bool configure_runtime_tools(LLMCapabilityProvider &provider,
                                           const CommandLineOptions &options,
                                           std::ostream &err) {
    RuntimeToolSet runtime_tools;

    if (options.capability_mocks_descriptor.has_value()) {
        auto mock_set = load_capability_mock_set(options, err);
        if (!mock_set.has_value()) {
            return false;
        }
        if (!add_mock_tools(runtime_tools, *mock_set, err)) {
            return false;
        }
    }

    if (options.tool_catalog_descriptor.has_value()) {
        auto tool_catalog = load_tool_catalog_descriptor(options, err);
        if (!tool_catalog.has_value()) {
            return false;
        }
        if (!add_catalog_tools(runtime_tools, *tool_catalog, err)) {
            return false;
        }
    }

    install_runtime_tools(provider, std::move(runtime_tools));
    return true;
}

[[nodiscard]] const char *workflow_status_name(WorkflowStatus status) {
    switch (status) {
    case WorkflowStatus::Completed:
        return "Completed";
    case WorkflowStatus::NodeFailed:
        return "NodeFailed";
    case WorkflowStatus::DependencyFailed:
        return "DependencyFailed";
    case WorkflowStatus::EvalError:
        return "EvalError";
    }
    return "Unknown";
}

[[nodiscard]] const char *agent_status_name(AgentStatus status) {
    switch (status) {
    case AgentStatus::Running:
        return "Running";
    case AgentStatus::Completed:
        return "Completed";
    case AgentStatus::Failed:
        return "Failed";
    case AgentStatus::QuotaExceeded:
        return "QuotaExceeded";
    case AgentStatus::InvalidTransition:
        return "InvalidTransition";
    case AgentStatus::InfiniteLoop:
        return "InfiniteLoop";
    }
    return "Unknown";
}

[[nodiscard]] const char *cache_event_kind_name(LLMResponseCacheAuditEventKind kind) {
    switch (kind) {
    case LLMResponseCacheAuditEventKind::Miss:
        return "miss";
    case LLMResponseCacheAuditEventKind::Hit:
        return "hit";
    case LLMResponseCacheAuditEventKind::Write:
        return "write";
    case LLMResponseCacheAuditEventKind::Invalidated:
        return "invalidated";
    case LLMResponseCacheAuditEventKind::SnapshotLoaded:
        return "snapshot_loaded";
    case LLMResponseCacheAuditEventKind::SnapshotPersisted:
        return "snapshot_persisted";
    case LLMResponseCacheAuditEventKind::SnapshotLoadFailed:
        return "snapshot_load_failed";
    case LLMResponseCacheAuditEventKind::SnapshotPersistFailed:
        return "snapshot_persist_failed";
    }
    return "unknown";
}

[[nodiscard]] const char *provider_health_event_kind_name(LLMProviderHealthEventKind kind) {
    switch (kind) {
    case LLMProviderHealthEventKind::ProviderDegraded:
        return "provider_degraded";
    case LLMProviderHealthEventKind::FallbackSelected:
        return "fallback_selected";
    case LLMProviderHealthEventKind::FallbackExhausted:
        return "fallback_exhausted";
    }
    return "unknown";
}

[[nodiscard]] const char *streaming_event_kind_name(LLMStreamingEventKind kind) {
    switch (kind) {
    case LLMStreamingEventKind::Chunk:
        return "chunk";
    case LLMStreamingEventKind::Completed:
        return "completed";
    case LLMStreamingEventKind::Interrupted:
        return "interrupted";
    }
    return "unknown";
}

[[nodiscard]] const char *token_budget_event_kind_name(LLMTokenBudgetEventKind kind) {
    switch (kind) {
    case LLMTokenBudgetEventKind::PromptAccepted:
        return "prompt_accepted";
    case LLMTokenBudgetEventKind::PromptTruncated:
        return "prompt_truncated";
    case LLMTokenBudgetEventKind::PromptRejected:
        return "prompt_rejected";
    case LLMTokenBudgetEventKind::UsageWithinBudget:
        return "usage_within_budget";
    case LLMTokenBudgetEventKind::UsageExceededBudget:
        return "usage_exceeded_budget";
    case LLMTokenBudgetEventKind::CostWithinBudget:
        return "cost_within_budget";
    case LLMTokenBudgetEventKind::CostExceededBudget:
        return "cost_exceeded_budget";
    case LLMTokenBudgetEventKind::WorkflowUsageWithinBudget:
        return "workflow_usage_within_budget";
    case LLMTokenBudgetEventKind::WorkflowUsageExceededBudget:
        return "workflow_usage_exceeded_budget";
    case LLMTokenBudgetEventKind::NodeUsageWithinBudget:
        return "node_usage_within_budget";
    case LLMTokenBudgetEventKind::NodeUsageExceededBudget:
        return "node_usage_exceeded_budget";
    case LLMTokenBudgetEventKind::WorkflowCostWithinBudget:
        return "workflow_cost_within_budget";
    case LLMTokenBudgetEventKind::WorkflowCostExceededBudget:
        return "workflow_cost_exceeded_budget";
    case LLMTokenBudgetEventKind::NodeCostWithinBudget:
        return "node_cost_within_budget";
    case LLMTokenBudgetEventKind::NodeCostExceededBudget:
        return "node_cost_exceeded_budget";
    }
    return "unknown";
}

[[nodiscard]] const char *secret_access_event_kind_name(ahfl::secret::SecretAccessEventKind kind) {
    switch (kind) {
    case ahfl::secret::SecretAccessEventKind::Resolve:
        return "resolve";
    case ahfl::secret::SecretAccessEventKind::Refresh:
        return "refresh";
    }
    return "unknown";
}

[[nodiscard]] std::string secret_handle_prefix(std::string_view key) {
    const auto separator = key.find(':');
    if (separator == std::string_view::npos || separator == 0) {
        return "unqualified";
    }
    return std::string(key.substr(0, separator));
}

[[nodiscard]] std::unique_ptr<ahfl::json::JsonValue> json_size(std::size_t value) {
    return ahfl::json::JsonValue::make_int(static_cast<std::int64_t>(value));
}

struct ProviderDegradationSummary {
    std::string status{"healthy"};
    std::string outcome{"healthy"};
    std::string source_provider;
    std::string selected_provider;
    std::vector<std::string> degraded_providers;
    std::size_t total_attempts{0};
    bool fallback_exhausted{false};
    bool secret_free{true};
};

[[nodiscard]] ProviderDegradationSummary
provider_degradation_summary(const std::vector<LLMProviderHealthEvent> &events) {
    ProviderDegradationSummary summary;
    std::unordered_set<std::string> seen_degraded;
    bool saw_selected = false;
    bool saw_degraded = false;

    for (const auto &event : events) {
        summary.total_attempts = std::max(summary.total_attempts, event.attempts_after);
        summary.secret_free = summary.secret_free && event.secret_free;
        switch (event.kind) {
        case LLMProviderHealthEventKind::ProviderDegraded:
            saw_degraded = true;
            if (!event.provider_name.empty() && seen_degraded.insert(event.provider_name).second) {
                summary.degraded_providers.push_back(event.provider_name);
            }
            break;
        case LLMProviderHealthEventKind::FallbackSelected:
            saw_selected = true;
            summary.source_provider = event.provider_name;
            summary.selected_provider = event.selected_provider_name;
            break;
        case LLMProviderHealthEventKind::FallbackExhausted:
            summary.fallback_exhausted = true;
            break;
        }
    }

    if (saw_selected) {
        summary.status = "recovered";
        summary.outcome = "fallback_selected";
    } else if (summary.fallback_exhausted) {
        summary.status = "exhausted";
        summary.outcome = "fallback_exhausted";
    } else if (saw_degraded) {
        summary.status = "degraded";
        summary.outcome = "provider_degraded";
    }
    return summary;
}

[[nodiscard]] std::unique_ptr<ahfl::json::JsonValue>
build_provider_degradation_summary_json(const ProviderDegradationSummary &summary) {
    auto object = ahfl::json::JsonValue::make_object();
    object->set("schema",
                ahfl::json::JsonValue::make_string("ahfl.llm_provider_degradation_summary.v0"));
    object->set("secret_free", ahfl::json::JsonValue::make_bool(summary.secret_free));
    object->set("status", ahfl::json::JsonValue::make_string(summary.status));
    object->set("outcome", ahfl::json::JsonValue::make_string(summary.outcome));
    object->set("source_provider", ahfl::json::JsonValue::make_string(summary.source_provider));
    object->set("selected_provider", ahfl::json::JsonValue::make_string(summary.selected_provider));
    object->set("total_attempts", json_size(summary.total_attempts));
    object->set("fallback_exhausted", ahfl::json::JsonValue::make_bool(summary.fallback_exhausted));
    object->set("degraded_provider_count", json_size(summary.degraded_providers.size()));

    auto degraded = ahfl::json::JsonValue::make_array();
    for (const auto &provider_name : summary.degraded_providers) {
        degraded->push(ahfl::json::JsonValue::make_string(provider_name));
    }
    object->set("degraded_providers", std::move(degraded));
    return object;
}

[[nodiscard]] std::unique_ptr<ahfl::json::JsonValue>
build_llm_provider_observability_json(const LLMCapabilityProvider &provider,
                                      const ahfl::secret::SecretAuditLog *secret_audit_log) {
    const auto &cache_events = provider.response_cache_audit_events();
    const auto &health_events = provider.provider_health_events();
    const auto &streaming_events = provider.streaming_events();
    const auto &token_usage_events = provider.token_usage_events();
    const auto &token_budget_events = provider.token_budget_events();
    const auto secret_event_count =
        secret_audit_log == nullptr ? std::size_t{0} : secret_audit_log->events().size();

    auto root = ahfl::json::JsonValue::make_object();
    root->set("schema", ahfl::json::JsonValue::make_string("ahfl.llm_provider_observability.v0"));
    root->set("secret_free", ahfl::json::JsonValue::make_bool(true));
    root->set("cache_event_count", json_size(cache_events.size()));
    root->set("provider_health_event_count", json_size(health_events.size()));
    root->set("streaming_event_count", json_size(streaming_events.size()));
    root->set("token_usage_event_count", json_size(token_usage_events.size()));
    root->set("token_budget_event_count", json_size(token_budget_events.size()));
    root->set("secret_lifecycle_event_count", json_size(secret_event_count));

    auto cache_array = ahfl::json::JsonValue::make_array();
    for (std::size_t index = 0; index < cache_events.size(); ++index) {
        const auto &event = cache_events[index];
        auto item = ahfl::json::JsonValue::make_object();
        item->set("index", json_size(index));
        item->set("kind", ahfl::json::JsonValue::make_string(cache_event_kind_name(event.kind)));
        item->set("model", ahfl::json::JsonValue::make_string(event.model));
        item->set("cache_key_version", ahfl::json::JsonValue::make_string(event.cache_key_version));
        item->set("key_fingerprint", ahfl::json::JsonValue::make_string(event.key_fingerprint));
        item->set("system_prompt_bytes", json_size(event.system_prompt_bytes));
        item->set("user_prompt_bytes", json_size(event.user_prompt_bytes));
        item->set("response_bytes", json_size(event.response_bytes));
        item->set("cache_size_after", json_size(event.cache_size_after));
        item->set("snapshot_entry_count", json_size(event.snapshot_entry_count));
        item->set("persistent", ahfl::json::JsonValue::make_bool(event.persistent));
        item->set("secret_free", ahfl::json::JsonValue::make_bool(event.secret_free));
        cache_array->push(std::move(item));
    }
    root->set("cache_events", std::move(cache_array));

    auto health_array = ahfl::json::JsonValue::make_array();
    for (std::size_t index = 0; index < health_events.size(); ++index) {
        const auto &event = health_events[index];
        auto item = ahfl::json::JsonValue::make_object();
        item->set("index", json_size(index));
        item->set("kind",
                  ahfl::json::JsonValue::make_string(provider_health_event_kind_name(event.kind)));
        item->set("provider", ahfl::json::JsonValue::make_string(event.provider_name));
        item->set("selected_provider",
                  ahfl::json::JsonValue::make_string(event.selected_provider_name));
        item->set("status_code", ahfl::json::JsonValue::make_int(event.status_code));
        item->set("attempts_after", json_size(event.attempts_after));
        item->set("secret_free", ahfl::json::JsonValue::make_bool(event.secret_free));
        health_array->push(std::move(item));
    }
    root->set("provider_health_events", std::move(health_array));
    root->set("provider_degradation_summary",
              build_provider_degradation_summary_json(provider_degradation_summary(health_events)));

    auto stream_array = ahfl::json::JsonValue::make_array();
    for (std::size_t index = 0; index < streaming_events.size(); ++index) {
        const auto &event = streaming_events[index];
        auto item = ahfl::json::JsonValue::make_object();
        item->set("index", json_size(index));
        item->set("kind",
                  ahfl::json::JsonValue::make_string(streaming_event_kind_name(event.kind)));
        item->set("provider", ahfl::json::JsonValue::make_string(event.provider_name));
        item->set("chunk_index", json_size(event.chunk_index));
        item->set("chunk_bytes", json_size(event.chunk_bytes));
        item->set("total_content_bytes", json_size(event.total_content_bytes));
        item->set("completed", ahfl::json::JsonValue::make_bool(event.completed));
        item->set("secret_free", ahfl::json::JsonValue::make_bool(event.secret_free));
        stream_array->push(std::move(item));
    }
    root->set("streaming_events", std::move(stream_array));

    auto token_usage_array = ahfl::json::JsonValue::make_array();
    for (std::size_t index = 0; index < token_usage_events.size(); ++index) {
        const auto &event = token_usage_events[index];
        auto item = ahfl::json::JsonValue::make_object();
        item->set("index", json_size(index));
        item->set("provider", ahfl::json::JsonValue::make_string(event.provider_name));
        item->set("model", ahfl::json::JsonValue::make_string(event.model));
        item->set("workflow", ahfl::json::JsonValue::make_string(event.workflow_name));
        item->set("workflow_node", ahfl::json::JsonValue::make_string(event.workflow_node_name));
        item->set("agent", ahfl::json::JsonValue::make_string(event.agent_name));
        item->set("state", ahfl::json::JsonValue::make_string(event.state_name));
        item->set("workflow_node_execution_index", json_size(event.workflow_node_execution_index));
        item->set("has_workflow_node_context",
                  ahfl::json::JsonValue::make_bool(event.has_workflow_node_context));
        item->set("prompt_tokens", json_size(event.prompt_tokens));
        item->set("completion_tokens", json_size(event.completion_tokens));
        item->set("total_tokens", json_size(event.total_tokens));
        item->set("prompt_cost_usd", ahfl::json::JsonValue::make_float(event.prompt_cost_usd));
        item->set("completion_cost_usd",
                  ahfl::json::JsonValue::make_float(event.completion_cost_usd));
        item->set("total_cost_usd", ahfl::json::JsonValue::make_float(event.total_cost_usd));
        item->set("cost_estimated", ahfl::json::JsonValue::make_bool(event.cost_estimated));
        item->set("secret_free", ahfl::json::JsonValue::make_bool(event.secret_free));
        token_usage_array->push(std::move(item));
    }
    root->set("token_usage_events", std::move(token_usage_array));

    auto token_budget_array = ahfl::json::JsonValue::make_array();
    for (std::size_t index = 0; index < token_budget_events.size(); ++index) {
        const auto &event = token_budget_events[index];
        auto item = ahfl::json::JsonValue::make_object();
        item->set("index", json_size(index));
        item->set("kind",
                  ahfl::json::JsonValue::make_string(token_budget_event_kind_name(event.kind)));
        item->set("provider", ahfl::json::JsonValue::make_string(event.provider_name));
        item->set("model", ahfl::json::JsonValue::make_string(event.model));
        item->set("capability", ahfl::json::JsonValue::make_string(event.capability_name));
        item->set("workflow", ahfl::json::JsonValue::make_string(event.workflow_name));
        item->set("workflow_node", ahfl::json::JsonValue::make_string(event.workflow_node_name));
        item->set("agent", ahfl::json::JsonValue::make_string(event.agent_name));
        item->set("state", ahfl::json::JsonValue::make_string(event.state_name));
        item->set("workflow_node_execution_index", json_size(event.workflow_node_execution_index));
        item->set("has_workflow_node_context",
                  ahfl::json::JsonValue::make_bool(event.has_workflow_node_context));
        item->set("max_total_tokens", json_size(event.max_total_tokens));
        item->set("max_prompt_tokens", json_size(event.max_prompt_tokens));
        item->set("max_response_tokens", json_size(event.max_response_tokens));
        item->set("max_workflow_total_tokens", json_size(event.max_workflow_total_tokens));
        item->set("max_node_total_tokens", json_size(event.max_node_total_tokens));
        item->set("effective_prompt_tokens", json_size(event.effective_prompt_tokens));
        item->set("system_prompt_tokens", json_size(event.system_prompt_tokens));
        item->set("user_prompt_tokens_before", json_size(event.user_prompt_tokens_before));
        item->set("user_prompt_tokens_after", json_size(event.user_prompt_tokens_after));
        item->set("prompt_tokens", json_size(event.prompt_tokens));
        item->set("completion_tokens", json_size(event.completion_tokens));
        item->set("total_tokens", json_size(event.total_tokens));
        item->set("cumulative_workflow_tokens", json_size(event.cumulative_workflow_tokens));
        item->set("cumulative_node_tokens", json_size(event.cumulative_node_tokens));
        item->set("max_total_cost_usd",
                  ahfl::json::JsonValue::make_float(event.max_total_cost_usd));
        item->set("max_workflow_total_cost_usd",
                  ahfl::json::JsonValue::make_float(event.max_workflow_total_cost_usd));
        item->set("max_node_total_cost_usd",
                  ahfl::json::JsonValue::make_float(event.max_node_total_cost_usd));
        item->set("total_cost_usd", ahfl::json::JsonValue::make_float(event.total_cost_usd));
        item->set("cumulative_workflow_cost_usd",
                  ahfl::json::JsonValue::make_float(event.cumulative_workflow_cost_usd));
        item->set("cumulative_node_cost_usd",
                  ahfl::json::JsonValue::make_float(event.cumulative_node_cost_usd));
        item->set("truncated", ahfl::json::JsonValue::make_bool(event.truncated));
        item->set("policy", ahfl::json::JsonValue::make_string(event.policy));
        item->set("message", ahfl::json::JsonValue::make_string(event.message));
        item->set("diagnostic_code", ahfl::json::JsonValue::make_string(event.diagnostic_code));
        item->set("secret_free", ahfl::json::JsonValue::make_bool(event.secret_free));
        token_budget_array->push(std::move(item));
    }
    root->set("token_budget_events", std::move(token_budget_array));

    auto secret_array = ahfl::json::JsonValue::make_array();
    if (secret_audit_log != nullptr) {
        const auto &secret_events = secret_audit_log->events();
        for (std::size_t index = 0; index < secret_events.size(); ++index) {
            const auto &event = secret_events[index];
            auto item = ahfl::json::JsonValue::make_object();
            item->set("index", json_size(index));
            item->set(
                "kind",
                ahfl::json::JsonValue::make_string(secret_access_event_kind_name(event.kind)));
            item->set("provider_prefix",
                      ahfl::json::JsonValue::make_string(secret_handle_prefix(event.key)));
            item->set("key_fingerprint",
                      ahfl::json::JsonValue::make_string(hex_hash(fnv1a_hash(event.key))));
            item->set("accessor", ahfl::json::JsonValue::make_string(event.accessor));
            item->set("success", ahfl::json::JsonValue::make_bool(event.success));
            item->set("secret_free", ahfl::json::JsonValue::make_bool(true));
            secret_array->push(std::move(item));
        }
    }
    root->set("secret_lifecycle_events", std::move(secret_array));

    return root;
}

[[nodiscard]] bool
write_llm_provider_observability_json(const LLMCapabilityProvider &provider,
                                      const std::filesystem::path &path,
                                      const ahfl::secret::SecretAuditLog *secret_audit_log,
                                      std::ostream &err) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        err << "error: failed to open LLM observability output: " << ahfl::display_path(path)
            << '\n';
        return false;
    }

    auto root = build_llm_provider_observability_json(provider, secret_audit_log);
    file << ahfl::json::serialize_json(*root) << '\n';
    if (!file.good()) {
        err << "error: failed to write LLM observability output: " << ahfl::display_path(path)
            << '\n';
        return false;
    }
    return true;
}

void print_llm_provider_observability(const LLMCapabilityProvider &provider,
                                      const ahfl::secret::SecretAuditLog *secret_audit_log,
                                      std::ostream &out) {
    const auto &cache_events = provider.response_cache_audit_events();
    const auto &health_events = provider.provider_health_events();
    const auto &streaming_events = provider.streaming_events();
    const auto &token_usage_events = provider.token_usage_events();
    const auto &token_budget_events = provider.token_budget_events();
    const auto secret_event_count =
        secret_audit_log == nullptr ? std::size_t{0} : secret_audit_log->events().size();
    if (cache_events.empty() && health_events.empty() && streaming_events.empty() &&
        token_usage_events.empty() && token_budget_events.empty() && secret_event_count == 0) {
        return;
    }

    out << "\n--- LLM Provider Observability ---\n";
    out << "Cache Events: " << cache_events.size() << '\n';
    for (std::size_t index = 0; index < cache_events.size(); ++index) {
        const auto &event = cache_events[index];
        out << "    cache[" << index << "]: kind=" << cache_event_kind_name(event.kind)
            << " model=" << event.model << " key_version=" << event.cache_key_version
            << " key_fingerprint=" << event.key_fingerprint
            << " system_prompt_bytes=" << event.system_prompt_bytes
            << " user_prompt_bytes=" << event.user_prompt_bytes
            << " response_bytes=" << event.response_bytes
            << " cache_size_after=" << event.cache_size_after
            << " snapshot_entry_count=" << event.snapshot_entry_count
            << " persistent=" << (event.persistent ? "true" : "false")
            << " secret_free=" << (event.secret_free ? "true" : "false") << '\n';
    }

    out << "Provider Health Events: " << health_events.size() << '\n';
    for (std::size_t index = 0; index < health_events.size(); ++index) {
        const auto &event = health_events[index];
        out << "    health[" << index << "]: kind=" << provider_health_event_kind_name(event.kind)
            << " provider=" << event.provider_name << " selected=" << event.selected_provider_name
            << " status_code=" << event.status_code << " attempts_after=" << event.attempts_after
            << " secret_free=" << (event.secret_free ? "true" : "false") << '\n';
    }

    const auto degradation_summary = provider_degradation_summary(health_events);
    out << "Provider Degradation Summary: status=" << degradation_summary.status
        << " outcome=" << degradation_summary.outcome
        << " degraded_provider_count=" << degradation_summary.degraded_providers.size()
        << " selected_provider=" << degradation_summary.selected_provider
        << " total_attempts=" << degradation_summary.total_attempts
        << " secret_free=" << (degradation_summary.secret_free ? "true" : "false") << '\n';

    out << "Streaming Events: " << streaming_events.size() << '\n';
    for (std::size_t index = 0; index < streaming_events.size(); ++index) {
        const auto &event = streaming_events[index];
        out << "    stream[" << index << "]: kind=" << streaming_event_kind_name(event.kind)
            << " provider=" << event.provider_name << " chunk_index=" << event.chunk_index
            << " chunk_bytes=" << event.chunk_bytes
            << " total_content_bytes=" << event.total_content_bytes
            << " completed=" << (event.completed ? "true" : "false")
            << " secret_free=" << (event.secret_free ? "true" : "false") << '\n';
    }

    out << "Token Usage Events: " << token_usage_events.size() << '\n';
    for (std::size_t index = 0; index < token_usage_events.size(); ++index) {
        const auto &event = token_usage_events[index];
        out << "    usage[" << index << "]: provider=" << event.provider_name
            << " model=" << event.model << " workflow=" << event.workflow_name
            << " node=" << event.workflow_node_name << " agent=" << event.agent_name
            << " state=" << event.state_name
            << " node_index=" << event.workflow_node_execution_index
            << " prompt_tokens=" << event.prompt_tokens
            << " completion_tokens=" << event.completion_tokens
            << " total_tokens=" << event.total_tokens
            << " prompt_cost_usd=" << event.prompt_cost_usd
            << " completion_cost_usd=" << event.completion_cost_usd
            << " total_cost_usd=" << event.total_cost_usd
            << " cost_estimated=" << (event.cost_estimated ? "true" : "false")
            << " secret_free=" << (event.secret_free ? "true" : "false") << '\n';
    }

    out << "Token Budget Events: " << token_budget_events.size() << '\n';
    for (std::size_t index = 0; index < token_budget_events.size(); ++index) {
        const auto &event = token_budget_events[index];
        out << "    budget[" << index << "]: kind=" << token_budget_event_kind_name(event.kind)
            << " provider=" << event.provider_name << " model=" << event.model
            << " capability=" << event.capability_name << " workflow=" << event.workflow_name
            << " node=" << event.workflow_node_name << " agent=" << event.agent_name
            << " state=" << event.state_name
            << " node_index=" << event.workflow_node_execution_index
            << " max_total_tokens=" << event.max_total_tokens
            << " max_prompt_tokens=" << event.max_prompt_tokens
            << " max_response_tokens=" << event.max_response_tokens
            << " max_workflow_total_tokens=" << event.max_workflow_total_tokens
            << " max_node_total_tokens=" << event.max_node_total_tokens
            << " effective_prompt_tokens=" << event.effective_prompt_tokens
            << " system_prompt_tokens=" << event.system_prompt_tokens
            << " user_prompt_tokens_before=" << event.user_prompt_tokens_before
            << " user_prompt_tokens_after=" << event.user_prompt_tokens_after
            << " prompt_tokens=" << event.prompt_tokens
            << " completion_tokens=" << event.completion_tokens
            << " total_tokens=" << event.total_tokens
            << " cumulative_workflow_tokens=" << event.cumulative_workflow_tokens
            << " cumulative_node_tokens=" << event.cumulative_node_tokens
            << " max_total_cost_usd=" << event.max_total_cost_usd
            << " max_workflow_total_cost_usd=" << event.max_workflow_total_cost_usd
            << " max_node_total_cost_usd=" << event.max_node_total_cost_usd
            << " total_cost_usd=" << event.total_cost_usd
            << " cumulative_workflow_cost_usd=" << event.cumulative_workflow_cost_usd
            << " cumulative_node_cost_usd=" << event.cumulative_node_cost_usd
            << " truncated=" << (event.truncated ? "true" : "false") << " policy=" << event.policy
            << " diagnostic_code=" << event.diagnostic_code
            << " secret_free=" << (event.secret_free ? "true" : "false") << '\n';
    }

    out << "Secret Lifecycle Events: " << secret_event_count << '\n';
    if (secret_audit_log != nullptr) {
        const auto &secret_events = secret_audit_log->events();
        for (std::size_t index = 0; index < secret_events.size(); ++index) {
            const auto &event = secret_events[index];
            out << "    secret[" << index << "]: kind=" << secret_access_event_kind_name(event.kind)
                << " provider_prefix=" << secret_handle_prefix(event.key)
                << " key_fingerprint=" << hex_hash(fnv1a_hash(event.key))
                << " accessor=" << event.accessor
                << " success=" << (event.success ? "true" : "false") << " secret_free=true\n";
        }
    }
}

[[nodiscard]] std::string
event_capability_name(const ahfl::llm_provider::LLMTokenBudgetEvent &event) {
    return event.capability_name.empty() ? std::string{"<unknown>"} : event.capability_name;
}

[[nodiscard]] std::string event_budget_scope(const ahfl::llm_provider::LLMTokenBudgetEvent &event) {
    switch (event.kind) {
    case LLMTokenBudgetEventKind::WorkflowUsageWithinBudget:
    case LLMTokenBudgetEventKind::WorkflowUsageExceededBudget:
    case LLMTokenBudgetEventKind::WorkflowCostWithinBudget:
    case LLMTokenBudgetEventKind::WorkflowCostExceededBudget:
        return "workflow '" +
               (event.workflow_name.empty() ? std::string{"<direct>"} : event.workflow_name) + "'";
    case LLMTokenBudgetEventKind::NodeUsageWithinBudget:
    case LLMTokenBudgetEventKind::NodeUsageExceededBudget:
    case LLMTokenBudgetEventKind::NodeCostWithinBudget:
    case LLMTokenBudgetEventKind::NodeCostExceededBudget:
        return "workflow node '" + event.workflow_node_name + "'";
    default:
        return "capability '" + event_capability_name(event) + "'";
    }
}

[[nodiscard]] std::string event_context_note(const ahfl::llm_provider::LLMTokenBudgetEvent &event) {
    std::string note = "workflow=" + event.workflow_name + ", node=" + event.workflow_node_name +
                       ", agent=" + event.agent_name + ", state=" + event.state_name;
    if (event.has_workflow_node_context) {
        note += ", node_index=" + std::to_string(event.workflow_node_execution_index);
    }
    return note;
}

[[nodiscard]] bool emit_llm_token_budget_policy_diagnostics(const LLMCapabilityProvider &provider,
                                                            std::ostream &err) {
    ahfl::DiagnosticBag diagnostics;
    bool policy_failed = false;

    for (const auto &event : provider.token_budget_events()) {
        if (event.kind == LLMTokenBudgetEventKind::PromptRejected) {
            diagnostics.error()
                .code(ahfl::error_codes::runtime::LLMPromptBudgetRejected)
                .message("LLM prompt budget rejected for capability '" +
                         event_capability_name(event) + "'")
                .with_note("configured max_total_tokens=" + std::to_string(event.max_total_tokens) +
                           ", max_prompt_tokens=" + std::to_string(event.max_prompt_tokens) +
                           ", max_response_tokens=" + std::to_string(event.max_response_tokens))
                .with_note(
                    "estimated system_prompt_tokens=" + std::to_string(event.system_prompt_tokens) +
                    ", user_prompt_tokens_before=" +
                    std::to_string(event.user_prompt_tokens_before) +
                    ", effective_prompt_tokens=" + std::to_string(event.effective_prompt_tokens))
                .emit();
            continue;
        }

        if (event.kind == LLMTokenBudgetEventKind::UsageExceededBudget) {
            const bool warn_only = event.policy == "warn";
            policy_failed = policy_failed || !warn_only;
            if (warn_only) {
                diagnostics.warning()
                    .code(ahfl::error_codes::runtime::LLMTokenBudgetExceeded)
                    .message("LLM token budget policy warned for capability '" +
                             event_capability_name(event) + "'")
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reported total_tokens=" + std::to_string(event.total_tokens) +
                               " while max_total_tokens=" + std::to_string(event.max_total_tokens))
                    .with_note(
                        "token_budget_policy=" + event.policy +
                        "; reduce prompt/response usage or raise max_total_tokens in the LLM "
                        "config before rerunning")
                    .emit();
            } else {
                diagnostics.error()
                    .code(ahfl::error_codes::runtime::LLMTokenBudgetExceeded)
                    .message("LLM token budget policy failed for capability '" +
                             event_capability_name(event) + "'")
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reported total_tokens=" + std::to_string(event.total_tokens) +
                               " while max_total_tokens=" + std::to_string(event.max_total_tokens))
                    .with_note(
                        "token_budget_policy=" + event.policy +
                        "; reduce prompt/response usage or raise max_total_tokens in the LLM "
                        "config before rerunning")
                    .emit();
            }
        }

        if (event.kind == LLMTokenBudgetEventKind::CostExceededBudget) {
            const bool warn_only = event.policy == "warn";
            policy_failed = policy_failed || !warn_only;
            if (warn_only) {
                diagnostics.warning()
                    .code(ahfl::error_codes::runtime::LLMCostBudgetExceeded)
                    .message("LLM cost budget policy warned for capability '" +
                             event_capability_name(event) + "'")
                    .with_note(
                        "provider '" + event.provider_name + "' model '" + event.model +
                        "' estimated total_cost_usd=" + std::to_string(event.total_cost_usd) +
                        " while max_total_cost_usd=" + std::to_string(event.max_total_cost_usd))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce usage/cost rates or raise max_total_cost_usd in the LLM "
                               "config before rerunning")
                    .emit();
            } else {
                diagnostics.error()
                    .code(ahfl::error_codes::runtime::LLMCostBudgetExceeded)
                    .message("LLM cost budget policy failed for capability '" +
                             event_capability_name(event) + "'")
                    .with_note(
                        "provider '" + event.provider_name + "' model '" + event.model +
                        "' estimated total_cost_usd=" + std::to_string(event.total_cost_usd) +
                        " while max_total_cost_usd=" + std::to_string(event.max_total_cost_usd))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce usage/cost rates or raise max_total_cost_usd in the LLM "
                               "config before rerunning")
                    .emit();
            }
        }

        if (event.kind == LLMTokenBudgetEventKind::WorkflowUsageExceededBudget ||
            event.kind == LLMTokenBudgetEventKind::NodeUsageExceededBudget) {
            const bool warn_only = event.policy == "warn";
            policy_failed = policy_failed || !warn_only;
            const auto max_tokens =
                event.kind == LLMTokenBudgetEventKind::WorkflowUsageExceededBudget
                    ? event.max_workflow_total_tokens
                    : event.max_node_total_tokens;
            const auto cumulative_tokens =
                event.kind == LLMTokenBudgetEventKind::WorkflowUsageExceededBudget
                    ? event.cumulative_workflow_tokens
                    : event.cumulative_node_tokens;
            if (warn_only) {
                diagnostics.warning()
                    .code(ahfl::error_codes::runtime::LLMTokenBudgetExceeded)
                    .message("LLM cumulative token budget policy warned for " +
                             event_budget_scope(event))
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reached cumulative_tokens=" + std::to_string(cumulative_tokens) +
                               " while max_cumulative_tokens=" + std::to_string(max_tokens))
                    .with_note(event_context_note(event))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce workflow/node LLM usage or raise the cumulative token "
                               "budget before rerunning")
                    .emit();
            } else {
                diagnostics.error()
                    .code(ahfl::error_codes::runtime::LLMTokenBudgetExceeded)
                    .message("LLM cumulative token budget policy failed for " +
                             event_budget_scope(event))
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reached cumulative_tokens=" + std::to_string(cumulative_tokens) +
                               " while max_cumulative_tokens=" + std::to_string(max_tokens))
                    .with_note(event_context_note(event))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce workflow/node LLM usage or raise the cumulative token "
                               "budget before rerunning")
                    .emit();
            }
        }

        if (event.kind == LLMTokenBudgetEventKind::WorkflowCostExceededBudget ||
            event.kind == LLMTokenBudgetEventKind::NodeCostExceededBudget) {
            const bool warn_only = event.policy == "warn";
            policy_failed = policy_failed || !warn_only;
            const auto max_cost = event.kind == LLMTokenBudgetEventKind::WorkflowCostExceededBudget
                                      ? event.max_workflow_total_cost_usd
                                      : event.max_node_total_cost_usd;
            const auto cumulative_cost =
                event.kind == LLMTokenBudgetEventKind::WorkflowCostExceededBudget
                    ? event.cumulative_workflow_cost_usd
                    : event.cumulative_node_cost_usd;
            if (warn_only) {
                diagnostics.warning()
                    .code(ahfl::error_codes::runtime::LLMCostBudgetExceeded)
                    .message("LLM cumulative cost budget policy warned for " +
                             event_budget_scope(event))
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reached cumulative_cost_usd=" + std::to_string(cumulative_cost) +
                               " while max_cumulative_cost_usd=" + std::to_string(max_cost))
                    .with_note(event_context_note(event))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce workflow/node LLM usage or raise the cumulative cost "
                               "budget before rerunning")
                    .emit();
            } else {
                diagnostics.error()
                    .code(ahfl::error_codes::runtime::LLMCostBudgetExceeded)
                    .message("LLM cumulative cost budget policy failed for " +
                             event_budget_scope(event))
                    .with_note("provider '" + event.provider_name + "' model '" + event.model +
                               "' reached cumulative_cost_usd=" + std::to_string(cumulative_cost) +
                               " while max_cumulative_cost_usd=" + std::to_string(max_cost))
                    .with_note(event_context_note(event))
                    .with_note("token_budget_policy=" + event.policy +
                               "; reduce workflow/node LLM usage or raise the cumulative cost "
                               "budget before rerunning")
                    .emit();
            }
        }
    }

    if (diagnostics.has_error() || diagnostics.has_warning()) {
        diagnostics.render(err, std::nullopt, true);
    }
    return policy_failed;
}

void print_workflow_result(const WorkflowResult &result,
                           std::string_view workflow_name,
                           const std::filesystem::path &config_path,
                           std::string_view model_name,
                           std::ostream &out) {
    out << "\n=== AHFL Workflow Execution ===\n"
        << "Workflow: " << workflow_name << '\n'
        << "LLM Config: " << config_path << " (model: " << model_name << ")\n"
        << "\n--- Execution ---\n"
        << "Status: " << workflow_status_name(result.status) << '\n'
        << "Execution Order: ";

    for (std::size_t index = 0; index < result.execution_order.size(); ++index) {
        if (index > 0) {
            out << " -> ";
        }
        out << result.execution_order[index];
    }
    out << "\n\n--- Node Results ---\n";

    for (const auto &node_result : result.node_results) {
        out << "[" << node_result.execution_index << "] " << node_result.node_name;
        if (!node_result.target.empty()) {
            out << " (" << node_result.target << ")";
        }
        out << ": " << agent_status_name(node_result.status) << '\n';

        if (node_result.output.has_value()) {
            out << "    Output: ";
            print_value(*node_result.output, out);
            out << '\n';
        }
    }

    out << "\n--- Final Output ---\n";
    if (result.output.has_value()) {
        print_value(*result.output, out);
    } else {
        out << "(none)";
    }
    out << '\n';

    if (result.has_errors()) {
        out << "\n--- Errors ---\n";
        result.diagnostics.render(out);
    }

    out << '\n';
}

} // namespace

int run_workflow_with_llm(const ahfl::ir::Program &program,
                          const CommandLineOptions &options,
                          std::ostream &out,
                          std::ostream &err) {
    if (!options.workflow_name.has_value()) {
        err << "error: run requires --workflow\n";
        return 2;
    }
    if (!options.runtime_input_json.has_value()) {
        err << "error: run requires --input\n";
        return 2;
    }

    const auto config_path = llm_config_path_from_options(options);
    if (config_path.empty()) {
        err << "error: --llm-config is required when HOME is not set\n";
        return 2;
    }

    auto config_content = read_text_file(config_path, "LLM config", err);
    if (!config_content.has_value()) {
        return 1;
    }

    auto llm_config = load_config(*config_content);
    if (auto config_error = validate_config(llm_config); config_error.has_value()) {
        err << "error: " << *config_error << '\n';
        return 1;
    }
    auto secrets = build_llm_secret_manager(llm_config, err);
    if (secrets == nullptr) {
        return 1;
    }
    if (!resolve_llm_credentials(llm_config, *secrets, err)) {
        return 1;
    }

    std::shared_ptr<CapabilityRegistry> runtime_capability_bindings;
    if (options.capability_bindings_descriptor.has_value()) {
        runtime_capability_bindings =
            load_runtime_capability_bindings(program, options, secrets, err);
        if (runtime_capability_bindings == nullptr) {
            return 1;
        }
    }

    auto input_value = ahfl::evaluator::value_from_json(*options.runtime_input_json);
    if (!input_value.has_value()) {
        err << "error: failed to parse --input JSON\n";
        return 1;
    }

    const auto workflow_name = std::string(*options.workflow_name);
    const ir::ProgramIndex program_index{program};
    if (const auto *workflow = program_index.find_workflow(workflow_name); workflow != nullptr) {
        const auto input_validation = ahfl::runtime::validate_value_against_schema(
            *input_value, workflow->input_type_ref, program);
        if (!input_validation.valid) {
            err << "error: --input does not match workflow input schema for '" << workflow_name
                << "': " << input_validation.error << '\n';
            return 1;
        }
    }

    LLMCapabilityProvider llm_provider(program, llm_config);
    if (!configure_runtime_tools(llm_provider, options, err)) {
        return 1;
    }

    WorkflowRuntimeConfig runtime_config;
    auto llm_invoker = llm_provider.as_contextual_invoker();
    if (runtime_capability_bindings != nullptr) {
        auto binding_invoker = runtime_capability_bindings->as_contextual_invoker();
        runtime_config.contextual_capability_invoker =
            [runtime_capability_bindings,
             binding_invoker = std::move(binding_invoker),
             llm_invoker =
                 std::move(llm_invoker)](const ahfl::runtime::CapabilityInvocationContext &context,
                                         const std::string &name,
                                         const std::vector<ahfl::evaluator::Value> &args)
            -> ahfl::runtime::CapabilityCallResult {
            if (runtime_capability_bindings->has(name)) {
                return binding_invoker(context, name, args);
            }
            return llm_invoker(context, name, args);
        };
    } else {
        runtime_config.contextual_capability_invoker = std::move(llm_invoker);
    }

    WorkflowRuntime runtime(program, std::move(runtime_config));
    auto result = runtime.run(workflow_name, std::move(*input_value));

    print_workflow_result(result, workflow_name, config_path, llm_config.model, out);
    print_llm_provider_observability(llm_provider, &secrets->audit_log(), out);
    if (options.llm_observability_path.has_value() &&
        !write_llm_provider_observability_json(
            llm_provider,
            std::filesystem::path(std::string(*options.llm_observability_path)),
            &secrets->audit_log(),
            err)) {
        return 1;
    }
    const bool token_budget_policy_failed =
        emit_llm_token_budget_policy_diagnostics(llm_provider, err);
    return result.status == WorkflowStatus::Completed && !result.has_errors() &&
                   !token_budget_policy_failed
               ? 0
               : 1;
}

} // namespace ahfl::cli
