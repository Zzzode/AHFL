#include "runtime/providers/secret/secret_provider.hpp"
#include "runtime/providers/secret/auth_provider.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

using namespace ahfl::secret;

class StaticSecretProvider final : public SecretProvider {
  public:
    explicit StaticSecretProvider(std::unordered_map<std::string, std::string> values)
        : values_(std::move(values)) {}

    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override {
        auto it = values_.find(std::string(key));
        if (it == values_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void refresh(std::string_view key) override {
        last_refreshed_key = std::string(key);
    }

    std::string last_refreshed_key;

  private:
    std::unordered_map<std::string, std::string> values_;
};

bool test_env_provider_resolves_variable() {
    // Set an environment variable for testing
    ::setenv("AHFL_TEST_SECRET_KEY", "my_secret_value_42", 1);

    EnvSecretProvider provider;
    auto result = provider.resolve("AHFL_TEST_SECRET_KEY");
    if (!result.has_value())
        return false;
    if (*result != "my_secret_value_42")
        return false;

    // Clean up
    ::unsetenv("AHFL_TEST_SECRET_KEY");
    return true;
}

bool test_secret_provider_chain_routes_prefixed_handles() {
    SecretProviderChain chain;
    chain.add_provider("env",
                       std::make_unique<StaticSecretProvider>(
                           std::unordered_map<std::string, std::string>{{"AHFL_KEY", "env-key"}}),
                       true);
    chain.add_provider("vault",
                       std::make_unique<StaticSecretProvider>(
                           std::unordered_map<std::string, std::string>{{"llm/api", "vault-key"}}),
                       false);

    auto bare = chain.resolve("AHFL_KEY");
    if (!bare.has_value() || *bare != "env-key")
        return false;

    auto env = chain.resolve("env:AHFL_KEY");
    if (!env.has_value() || *env != "env-key")
        return false;

    auto vault = chain.resolve("vault:llm/api");
    if (!vault.has_value() || *vault != "vault-key")
        return false;

    auto unknown_prefix = chain.resolve("secret-manager:llm/api");
    if (unknown_prefix.has_value())
        return false;

    auto missing = chain.resolve("vault:missing");
    if (missing.has_value())
        return false;

    return chain.provider_count() == 2;
}

bool test_env_provider_missing_key() {
    // Make sure this key doesn't exist
    ::unsetenv("AHFL_TEST_NONEXISTENT_KEY_XYZ");

    EnvSecretProvider provider;
    auto result = provider.resolve("AHFL_TEST_NONEXISTENT_KEY_XYZ");
    if (result.has_value())
        return false;
    return true;
}

bool test_secret_manager_get_forwards() {
    ::setenv("AHFL_TEST_MGR_KEY", "manager_value", 1);

    auto provider = std::make_unique<EnvSecretProvider>();
    SecretManager manager(std::move(provider));

    auto result = manager.get("AHFL_TEST_MGR_KEY", "test_accessor");
    if (!result.has_value())
        return false;
    if (*result != "manager_value")
        return false;

    // Missing key should return nullopt through manager
    auto missing = manager.get("AHFL_TEST_NONEXISTENT_MGR_XYZ", "test_accessor");
    if (missing.has_value())
        return false;

    ::unsetenv("AHFL_TEST_MGR_KEY");
    return true;
}

bool test_secret_manager_audit_log() {
    ::setenv("AHFL_TEST_AUDIT_KEY", "audit_value", 1);
    ::unsetenv("AHFL_TEST_AUDIT_MISSING");

    auto provider = std::make_unique<EnvSecretProvider>();
    SecretManager manager(std::move(provider));

    // Initial audit log should be empty
    if (manager.audit_log().size() != 0)
        return false;

    // Successful access
    static_cast<void>(manager.get("AHFL_TEST_AUDIT_KEY", "accessor_a"));
    if (manager.audit_log().size() != 1)
        return false;

    auto &event1 = manager.audit_log().events()[0];
    if (event1.key != "AHFL_TEST_AUDIT_KEY")
        return false;
    if (event1.accessor != "accessor_a")
        return false;
    if (event1.success != true)
        return false;

    // Failed access (missing key)
    static_cast<void>(manager.get("AHFL_TEST_AUDIT_MISSING", "accessor_b"));
    if (manager.audit_log().size() != 2)
        return false;

    auto &event2 = manager.audit_log().events()[1];
    if (event2.key != "AHFL_TEST_AUDIT_MISSING")
        return false;
    if (event2.accessor != "accessor_b")
        return false;
    if (event2.success != false)
        return false;

    ::unsetenv("AHFL_TEST_AUDIT_KEY");
    return true;
}

bool test_secret_manager_refresh_records_lifecycle_event() {
    auto provider = std::make_unique<StaticSecretProvider>(
        std::unordered_map<std::string, std::string>{{"ROTATING_KEY", "rotated_value"}});
    auto *raw_provider = provider.get();
    SecretManager manager(std::move(provider));

    manager.refresh("ROTATING_KEY", "rotation_test");
    if (raw_provider->last_refreshed_key != "ROTATING_KEY")
        return false;
    if (manager.audit_log().size() != 1)
        return false;

    const auto &event = manager.audit_log().events()[0];
    if (event.kind != SecretAccessEventKind::Refresh)
        return false;
    if (event.key != "ROTATING_KEY")
        return false;
    if (event.accessor != "rotation_test")
        return false;
    if (!event.success)
        return false;

    return true;
}

bool test_auth_provider_oauth2_missing_secret_fails_closed() {
    auto provider =
        std::make_unique<StaticSecretProvider>(std::unordered_map<std::string, std::string>{});
    SecretManager manager(std::move(provider));

    AuthConfig config;
    config.scheme = AuthScheme::OAuth2ClientCredentials;
    config.token_key = "OAUTH_TOKEN";

    const auto result = resolve_auth(config, manager);
    if (result.success)
        return false;
    return result.error_message == "OAuth2 token secret not found: OAUTH_TOKEN";
}

bool test_auth_provider_mtls_missing_private_key_fails_closed() {
    auto provider = std::make_unique<StaticSecretProvider>(
        std::unordered_map<std::string, std::string>{{"CERT_PATH", "/tmp/client.pem"}});
    SecretManager manager(std::move(provider));

    AuthConfig config;
    config.scheme = AuthScheme::MTLS;
    config.cert_path_key = "CERT_PATH";
    config.key_path_key = "KEY_PATH";

    const auto result = resolve_auth(config, manager);
    if (result.success)
        return false;
    return result.error_message == "mTLS private key path secret not found: KEY_PATH";
}

} // namespace

int main() {
    int failures = 0;
    auto run = [&](bool (*fn)(), const char *name) {
        if (!fn()) {
            std::cerr << "FAIL: " << name << "\n";
            ++failures;
        }
    };

    run(test_env_provider_resolves_variable, "test_env_provider_resolves_variable");
    run(test_env_provider_missing_key, "test_env_provider_missing_key");
    run(test_secret_manager_get_forwards, "test_secret_manager_get_forwards");
    run(test_secret_manager_audit_log, "test_secret_manager_audit_log");
    run(test_secret_manager_refresh_records_lifecycle_event,
        "test_secret_manager_refresh_records_lifecycle_event");
    run(test_secret_provider_chain_routes_prefixed_handles,
        "test_secret_provider_chain_routes_prefixed_handles");
    run(test_auth_provider_oauth2_missing_secret_fails_closed,
        "test_auth_provider_oauth2_missing_secret_fails_closed");
    run(test_auth_provider_mtls_missing_private_key_fails_closed,
        "test_auth_provider_mtls_missing_private_key_fails_closed");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
