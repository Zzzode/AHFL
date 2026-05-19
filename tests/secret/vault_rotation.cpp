#include "ahfl/secret/key_rotation.hpp"
#include "ahfl/secret/vault_provider.hpp"

#include <cassert>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::secret;

bool test_vault_authenticate_then_resolve() {
    VaultConfig config;
    config.address = "http://127.0.0.1:8200";
    config.mount_path = "secret";

    VaultSecretProvider provider(config);

    // Before authentication, resolve should fail
    if (provider.is_authenticated()) return false;

    // Authenticate
    provider.authenticate();
    if (!provider.is_authenticated()) return false;
    if (!provider.is_connected()) return false;

    // Resolve should now succeed
    auto result = provider.resolve("database_password");
    if (!result.has_value()) return false;
    if (*result != "vault_secret_database_password") return false;

    // Resolve again should return from cache
    auto result2 = provider.resolve("database_password");
    if (!result2.has_value()) return false;
    if (*result2 != "vault_secret_database_password") return false;

    return true;
}

bool test_vault_resolve_without_auth_returns_nullopt() {
    VaultConfig config;
    VaultSecretProvider provider(config);

    // Without authentication, resolve should return nullopt
    auto result = provider.resolve("some_key");
    if (result.has_value()) return false;

    // list_secrets should return empty
    auto secrets = provider.list_secrets("path/");
    if (!secrets.empty()) return false;

    return true;
}

bool test_key_rotation_add_policy_and_rotate() {
    VaultConfig config;
    VaultSecretProvider vault(config);
    vault.authenticate();

    KeyRotationManager manager(vault);

    // Initially no policies
    if (manager.policy_count() != 0) return false;

    // Add a policy
    RotationPolicy policy;
    policy.key = "api_key";
    policy.interval = std::chrono::seconds{3600};
    policy.max_retries = 3;
    manager.add_policy(policy);

    if (manager.policy_count() != 1) return false;

    // Query policy
    auto retrieved = manager.get_policy("api_key");
    if (!retrieved.has_value()) return false;
    if (retrieved->key != "api_key") return false;
    if (retrieved->interval != std::chrono::seconds{3600}) return false;

    // Rotate key manually
    auto event = manager.rotate_key("api_key");
    if (event.key != "api_key") return false;
    if (event.status != RotationStatus::Completed) return false;
    if (event.new_version != "vault_secret_api_key") return false;

    // Check history
    auto hist = manager.history();
    if (hist.size() != 1) return false;
    if (hist[0].key != "api_key") return false;

    auto key_hist = manager.history_for("api_key");
    if (key_hist.size() != 1) return false;

    // Remove policy
    manager.remove_policy("api_key");
    if (manager.policy_count() != 0) return false;

    auto removed = manager.get_policy("api_key");
    if (removed.has_value()) return false;

    return true;
}

bool test_key_rotation_callback_fires() {
    VaultConfig config;
    VaultSecretProvider vault(config);
    vault.authenticate();

    KeyRotationManager manager(vault);

    // Track callback invocations
    int callback_count = 0;
    std::string last_key;
    RotationStatus last_status{RotationStatus::Idle};

    manager.on_rotation([&](const RotationEvent &event) {
        ++callback_count;
        last_key = event.key;
        last_status = event.status;
    });

    // Add policy and rotate
    RotationPolicy policy;
    policy.key = "secret_token";
    policy.interval = std::chrono::seconds{60};
    manager.add_policy(policy);

    manager.rotate_key("secret_token");

    if (callback_count != 1) return false;
    if (last_key != "secret_token") return false;
    if (last_status != RotationStatus::Completed) return false;

    // check_and_rotate should also fire callback (for first rotation on a new policy)
    RotationPolicy policy2;
    policy2.key = "another_key";
    policy2.interval = std::chrono::seconds{0}; // always rotate
    manager.add_policy(policy2);

    auto events = manager.check_and_rotate();
    // Should have rotated "another_key" (since it's never been rotated)
    // "secret_token" may or may not rotate depending on timing — it was just rotated
    bool found_another = false;
    for (const auto &e : events) {
        if (e.key == "another_key") {
            found_another = true;
        }
    }
    if (!found_another) return false;

    // Callback should have fired again
    if (callback_count < 2) return false;

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

    run(test_vault_authenticate_then_resolve, "test_vault_authenticate_then_resolve");
    run(test_vault_resolve_without_auth_returns_nullopt,
        "test_vault_resolve_without_auth_returns_nullopt");
    run(test_key_rotation_add_policy_and_rotate, "test_key_rotation_add_policy_and_rotate");
    run(test_key_rotation_callback_fires, "test_key_rotation_callback_fires");

    if (failures > 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cerr << "All tests passed\n";
    return 0;
}
