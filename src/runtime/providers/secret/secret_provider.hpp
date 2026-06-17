#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::secret {

/// Abstract interface for resolving secrets by key.
class SecretProvider {
  public:
    virtual ~SecretProvider() = default;
    [[nodiscard]] virtual std::optional<std::string> resolve(std::string_view key) = 0;
    virtual void refresh(std::string_view key) = 0;
};

/// Resolves secrets from environment variables.
class EnvSecretProvider final : public SecretProvider {
  public:
    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override;
    void refresh(std::string_view key) override;
};

/// Ordered provider chain with optional prefix routing.
///
/// Handles of the form "prefix:key" are routed to the matching provider after
/// stripping the prefix. Unqualified handles are resolved against providers
/// marked as default_for_unqualified, in insertion order.
class SecretProviderChain final : public SecretProvider {
  public:
    void add_provider(std::string prefix,
                      std::unique_ptr<SecretProvider> provider,
                      bool default_for_unqualified = false);

    [[nodiscard]] std::optional<std::string> resolve(std::string_view key) override;
    void refresh(std::string_view key) override;

    [[nodiscard]] std::size_t provider_count() const;

  private:
    struct Entry {
        std::string prefix;
        std::unique_ptr<SecretProvider> provider;
        bool default_for_unqualified{false};
    };

    [[nodiscard]] std::optional<std::pair<std::size_t, std::string_view>>
    provider_for_prefixed_key(std::string_view key) const;

    std::vector<Entry> entries_;
};

enum class SecretAccessEventKind {
    Resolve,
    Refresh,
};

/// A single audit event for secret access or lifecycle management.
struct SecretAccessEvent {
    SecretAccessEventKind kind{SecretAccessEventKind::Resolve};
    std::string key;
    std::string accessor;
    bool success{false};
    std::chrono::system_clock::time_point timestamp;
};

/// Append-only audit log for secret accesses.
class SecretAuditLog {
  public:
    void record(SecretAccessEvent event);
    [[nodiscard]] const std::vector<SecretAccessEvent> &events() const {
        return events_;
    }
    [[nodiscard]] std::size_t size() const {
        return events_.size();
    }

  private:
    std::vector<SecretAccessEvent> events_;
    mutable std::mutex mutex_;
};

/// Composite secret manager: provider + audit log.
class SecretManager {
  public:
    explicit SecretManager(std::unique_ptr<SecretProvider> provider);

    /// Resolve a secret, logging the access.
    [[nodiscard]] std::optional<std::string> get(std::string_view key,
                                                 std::string_view accessor = "");

    /// Force refresh of a secret in the underlying provider and log the lifecycle event.
    void refresh(std::string_view key, std::string_view accessor = "");

    /// Access the audit log for inspection.
    [[nodiscard]] const SecretAuditLog &audit_log() const {
        return audit_log_;
    }

  private:
    std::unique_ptr<SecretProvider> provider_;
    SecretAuditLog audit_log_;
};

} // namespace ahfl::secret
