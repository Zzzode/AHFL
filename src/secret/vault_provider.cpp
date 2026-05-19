#include "ahfl/secret/vault_provider.hpp"

#include <string>

namespace ahfl::secret {

// ============================================================================
// VaultSecretProvider
// ============================================================================

VaultSecretProvider::VaultSecretProvider(VaultConfig config, VaultAuthConfig auth)
    : config_(std::move(config)), auth_config_(std::move(auth)) {}

std::optional<std::string> VaultSecretProvider::resolve(std::string_view key) {
    if (!authenticated_) {
        return std::nullopt;
    }

    std::string key_str(key);

    // Check cache first
    auto it = cache_.find(key_str);
    if (it != cache_.end()) {
        return it->second;
    }

    // Stub: simulate Vault API call
    std::string value = "vault_secret_" + key_str;
    cache_[key_str] = value;
    return value;
}

void VaultSecretProvider::refresh(std::string_view key) {
    std::string key_str(key);
    cache_.erase(key_str);

    // Re-resolve if authenticated
    if (authenticated_) {
        std::string value = "vault_secret_" + key_str;
        cache_[key_str] = value;
    }
}

bool VaultSecretProvider::is_authenticated() const { return authenticated_; }

bool VaultSecretProvider::is_connected() const { return authenticated_; }

void VaultSecretProvider::authenticate() {
    // Stub: always succeeds
    authenticated_ = true;
}

void VaultSecretProvider::revoke_token() {
    authenticated_ = false;
    cache_.clear();
}

std::vector<std::string> VaultSecretProvider::list_secrets(std::string_view /*path*/) const {
    // Stub: return empty vector
    return {};
}

const VaultConfig &VaultSecretProvider::config() const { return config_; }

} // namespace ahfl::secret
