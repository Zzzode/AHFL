#include "secret/secret_provider.hpp"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>

namespace ahfl::secret {

// ============================================================================
// EnvSecretProvider
// ============================================================================

std::optional<std::string> EnvSecretProvider::resolve(std::string_view key) {
    // std::getenv requires a null-terminated string
    std::string key_str(key);
    const char *val = std::getenv(key_str.c_str());
    if (val == nullptr) {
        return std::nullopt;
    }
    return std::string(val);
}

void EnvSecretProvider::refresh(std::string_view /*key*/) {
    // Environment variables don't support refresh — no-op
}

// ============================================================================
// SecretAuditLog
// ============================================================================

void SecretAuditLog::record(SecretAccessEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.push_back(std::move(event));
}

// ============================================================================
// SecretManager
// ============================================================================

SecretManager::SecretManager(std::unique_ptr<SecretProvider> provider)
    : provider_(std::move(provider)) {}

std::optional<std::string> SecretManager::get(std::string_view key, std::string_view accessor) {
    auto result = provider_->resolve(key);

    SecretAccessEvent event;
    event.key = std::string(key);
    event.accessor = std::string(accessor);
    event.success = result.has_value();
    event.timestamp = std::chrono::system_clock::now();
    audit_log_.record(std::move(event));

    return result;
}

void SecretManager::refresh(std::string_view key) { provider_->refresh(key); }

} // namespace ahfl::secret
