#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::llm_provider {

/// Status of an LLM provider.
enum class ProviderStatus {
    Available,
    Degraded,
    Unavailable
};

/// An entry in the provider registry.
struct ProviderEntry {
    std::string name;
    std::string endpoint;
    std::string api_key;
    std::string api_key_secret;
    std::string oauth2_token_secret;
    std::string mtls_client_cert_path;
    std::string mtls_client_key_path;
    std::string mtls_ca_cert_path;
    std::string mtls_client_cert_secret;
    std::string mtls_client_key_secret;
    std::string mtls_ca_cert_secret;
    bool mtls_verify_tls{true};
    bool mtls_verify_tls_set{false};
    std::string auth_scheme;
    std::string auth_header;
    std::string model;
    int priority{0};
    ProviderStatus status{ProviderStatus::Available};
};

/// Registry for multiple LLM providers with priority-based selection and fallback.
class ProviderRegistry {
  public:
    void add_provider(ProviderEntry entry);

    /// Select the best available provider (highest priority among Available/Degraded).
    [[nodiscard]] std::optional<std::reference_wrapper<const ProviderEntry>> select() const;

    /// Return all candidate providers ordered by fallback priority.
    [[nodiscard]] std::vector<ProviderEntry> selection_order() const;

    /// Mark a provider as unavailable (e.g., after repeated failures).
    void mark_unavailable(std::string_view name);

    /// Mark a provider as available again.
    void mark_available(std::string_view name);

    /// Get all registered providers.
    [[nodiscard]] const std::vector<ProviderEntry> &providers() const {
        return providers_;
    }

  private:
    std::vector<ProviderEntry> providers_;
};

} // namespace ahfl::llm_provider
