#include "runtime/providers/secret/secret_provider.hpp"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace ahfl::secret;

bool test_env_provider_resolves_variable() {
    // Set an environment variable for testing
    ::setenv("AHFL_TEST_SECRET_KEY", "my_secret_value_42", 1);

    EnvSecretProvider provider;
    auto result = provider.resolve("AHFL_TEST_SECRET_KEY");
    if (!result.has_value()) return false;
    if (*result != "my_secret_value_42") return false;

    // Clean up
    ::unsetenv("AHFL_TEST_SECRET_KEY");
    return true;
}

bool test_env_provider_missing_key() {
    // Make sure this key doesn't exist
    ::unsetenv("AHFL_TEST_NONEXISTENT_KEY_XYZ");

    EnvSecretProvider provider;
    auto result = provider.resolve("AHFL_TEST_NONEXISTENT_KEY_XYZ");
    if (result.has_value()) return false;
    return true;
}

bool test_secret_manager_get_forwards() {
    ::setenv("AHFL_TEST_MGR_KEY", "manager_value", 1);

    auto provider = std::make_unique<EnvSecretProvider>();
    SecretManager manager(std::move(provider));

    auto result = manager.get("AHFL_TEST_MGR_KEY", "test_accessor");
    if (!result.has_value()) return false;
    if (*result != "manager_value") return false;

    // Missing key should return nullopt through manager
    auto missing = manager.get("AHFL_TEST_NONEXISTENT_MGR_XYZ", "test_accessor");
    if (missing.has_value()) return false;

    ::unsetenv("AHFL_TEST_MGR_KEY");
    return true;
}

bool test_secret_manager_audit_log() {
    ::setenv("AHFL_TEST_AUDIT_KEY", "audit_value", 1);
    ::unsetenv("AHFL_TEST_AUDIT_MISSING");

    auto provider = std::make_unique<EnvSecretProvider>();
    SecretManager manager(std::move(provider));

    // Initial audit log should be empty
    if (manager.audit_log().size() != 0) return false;

    // Successful access
    static_cast<void>(manager.get("AHFL_TEST_AUDIT_KEY", "accessor_a"));
    if (manager.audit_log().size() != 1) return false;

    auto &event1 = manager.audit_log().events()[0];
    if (event1.key != "AHFL_TEST_AUDIT_KEY") return false;
    if (event1.accessor != "accessor_a") return false;
    if (event1.success != true) return false;

    // Failed access (missing key)
    static_cast<void>(manager.get("AHFL_TEST_AUDIT_MISSING", "accessor_b"));
    if (manager.audit_log().size() != 2) return false;

    auto &event2 = manager.audit_log().events()[1];
    if (event2.key != "AHFL_TEST_AUDIT_MISSING") return false;
    if (event2.accessor != "accessor_b") return false;
    if (event2.success != false) return false;

    ::unsetenv("AHFL_TEST_AUDIT_KEY");
    return true;
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

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
