#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::llm_provider {

/// Status of an LLM provider.
enum class ProviderStatus { Available, Degraded, Unavailable };

/// An entry in the provider registry.
struct ProviderEntry {
    std::string name;
    std::string endpoint;
    std::string api_key_secret;
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

    /// Mark a provider as unavailable (e.g., after repeated failures).
    void mark_unavailable(std::string_view name);

    /// Mark a provider as available again.
    void mark_available(std::string_view name);

    /// Get all registered providers.
    [[nodiscard]] const std::vector<ProviderEntry> &providers() const { return providers_; }

  private:
    std::vector<ProviderEntry> providers_;
};

} // namespace ahfl::llm_provider
