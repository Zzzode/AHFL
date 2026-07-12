#include "tooling/cli/workflow_run.hpp"

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/program_view.hpp"
#include "base/json/json_value.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "ahfl/runtime/execution_renderer.hpp"
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

using ahfl::evaluator::value_from_json;
using ahfl::evaluator::value_to_json;
using ahfl::llm_provider::LLMCapabilityProvider;
using ahfl::llm_provider::LLMTokenBudgetEventKind;
using ahfl::llm_provider::load_config;
using ahfl::llm_provider::ToolCall;
using ahfl::llm_provider::ToolCallResult;
using ahfl::llm_provider::ToolDefinition;
using ahfl::llm_provider::validate_config;
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
using ahfl::runtime::WorkflowRuntime;
using ahfl::runtime::WorkflowRuntimeConfig;
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

[[nodiscard]] ahfl::runtime::ExecutionOutputFormat
execution_output_format_from_options(const CommandLineOptions &options) {
    const auto format = options.execution_output_format.value_or("human");
    if (format == "json") {
        return ahfl::runtime::ExecutionOutputFormat::Json;
    }
    if (format == "jsonl") {
        return ahfl::runtime::ExecutionOutputFormat::JsonLines;
    }
    if (format == "quiet") {
        return ahfl::runtime::ExecutionOutputFormat::Quiet;
    }
    return ahfl::runtime::ExecutionOutputFormat::Human;
}

[[nodiscard]] ahfl::runtime::ExecutionVerbosity
execution_verbosity_from_options(const CommandLineOptions &options) {
    const auto verbosity = options.execution_verbosity.value_or("normal");
    if (verbosity == "verbose") {
        return ahfl::runtime::ExecutionVerbosity::Verbose;
    }
    if (verbosity == "trace") {
        return ahfl::runtime::ExecutionVerbosity::Trace;
    }
    return ahfl::runtime::ExecutionVerbosity::Normal;
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


} // namespace

int run_workflow_with_llm(const ahfl::ir::Program &program,
                          const CommandLineOptions &options,
                          std::ostream &out,
                          std::ostream &err) {
    if (!options.workflow_name.has_value()) {
        err << "error: run requires --workflow or package workflow entry\n";
        return 2;
    }
    if (!options.runtime_input_json.has_value() && !options.runtime_input_file.has_value()) {
        err << "error: run requires --input, --input-file, or [run].input\n";
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

    std::string runtime_input;
    if (options.runtime_input_json.has_value()) {
        runtime_input = std::string(*options.runtime_input_json);
    } else {
        const auto input_path = std::filesystem::path(std::string(*options.runtime_input_file));
        auto content = read_text_file(input_path, "runtime input", err);
        if (!content.has_value()) {
            return 1;
        }
        runtime_input = std::move(*content);
    }
    auto input_value = ahfl::evaluator::value_from_json(runtime_input);
    if (!input_value.has_value()) {
        err << "error: failed to parse runtime input JSON\n";
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

    const auto render_result = ahfl::runtime::render_execution_result(
        result,
        ahfl::runtime::ExecutionOutputOptions{
            .format = execution_output_format_from_options(options),
            .verbosity = execution_verbosity_from_options(options),
        },
        out);
    if (!render_result.has_value()) {
        err << "error: " << render_result.error() << '\n';
        return 1;
    }
    if (result.diagnostics.has_error() || result.diagnostics.has_warning()) {
        result.diagnostics.render(err, std::nullopt, true);
    }
    return result.status() == ahfl::runtime::WorkflowStatus::Completed && !result.has_errors() &&
                   result.report.status == ahfl::runtime::RunTerminalStatus::Completed
               ? 0
               : 1;
}

} // namespace ahfl::cli
