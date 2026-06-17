#include "runtime/providers/secret/secret_provider.hpp"

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
// SecretProviderChain
// ============================================================================

void SecretProviderChain::add_provider(std::string prefix,
                                       std::unique_ptr<SecretProvider> provider,
                                       bool default_for_unqualified) {
    if (prefix.empty() || provider == nullptr) {
        return;
    }

    entries_.push_back(Entry{
        .prefix = std::move(prefix),
        .provider = std::move(provider),
        .default_for_unqualified = default_for_unqualified,
    });
}

std::optional<std::pair<std::size_t, std::string_view>>
SecretProviderChain::provider_for_prefixed_key(std::string_view key) const {
    const auto separator = key.find(':');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= key.size()) {
        return std::nullopt;
    }

    const auto prefix = key.substr(0, separator);
    const auto stripped_key = key.substr(separator + 1);
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].prefix == prefix) {
            return std::make_pair(index, stripped_key);
        }
    }
    return std::make_pair(entries_.size(), stripped_key);
}

std::optional<std::string> SecretProviderChain::resolve(std::string_view key) {
    if (auto prefixed = provider_for_prefixed_key(key); prefixed.has_value()) {
        const auto &[index, stripped_key] = *prefixed;
        if (index >= entries_.size()) {
            return std::nullopt;
        }
        return entries_[index].provider->resolve(stripped_key);
    }

    for (auto &entry : entries_) {
        if (!entry.default_for_unqualified) {
            continue;
        }
        if (auto resolved = entry.provider->resolve(key); resolved.has_value()) {
            return resolved;
        }
    }
    return std::nullopt;
}

void SecretProviderChain::refresh(std::string_view key) {
    if (auto prefixed = provider_for_prefixed_key(key); prefixed.has_value()) {
        const auto &[index, stripped_key] = *prefixed;
        if (index < entries_.size()) {
            entries_[index].provider->refresh(stripped_key);
        }
        return;
    }

    for (auto &entry : entries_) {
        if (entry.default_for_unqualified) {
            entry.provider->refresh(key);
        }
    }
}

std::size_t SecretProviderChain::provider_count() const {
    return entries_.size();
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
    event.kind = SecretAccessEventKind::Resolve;
    event.key = std::string(key);
    event.accessor = std::string(accessor);
    event.success = result.has_value();
    event.timestamp = std::chrono::system_clock::now();
    audit_log_.record(std::move(event));

    return result;
}

void SecretManager::refresh(std::string_view key, std::string_view accessor) {
    provider_->refresh(key);

    SecretAccessEvent event;
    event.kind = SecretAccessEventKind::Refresh;
    event.key = std::string(key);
    event.accessor = std::string(accessor);
    event.success = true;
    event.timestamp = std::chrono::system_clock::now();
    audit_log_.record(std::move(event));
}

} // namespace ahfl::secret
