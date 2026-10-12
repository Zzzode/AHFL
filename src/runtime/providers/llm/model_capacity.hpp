#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/providers/llm/http_client.hpp"
#include "runtime/providers/llm/llm_provider_config.hpp"

namespace ahfl::llm_provider {

using ProviderId = std::uint32_t;

enum class ModelLimitSource {
    Unknown,
    ExplicitOverride,
    OfficialProfile,
    ProviderMetadata
};
enum class CompletionLimitField {
    MaxTokens,
    MaxCompletionTokens
};

struct ResolvedModelLimits {
    ProviderId provider_id{};
    std::uint32_t profile_id{};
    ModelLimitSource source{ModelLimitSource::Unknown};
    std::optional<int> context_window_tokens{};
    std::optional<int> max_output_tokens{};
    CompletionLimitField completion_limit_field{CompletionLimitField::MaxTokens};
};

struct EffectiveRequestBudget {
    int max_tokens{};
    int max_prompt_tokens{};
    int max_total_tokens{};
    int safety_reserve_tokens{};
    int compact_threshold_tokens{};
    ResolvedModelLimits model_limits{};
};

// Provider-local metadata is cached for this resolver's lifetime, including unavailable metadata.
class ModelCapacityResolver {
  public:
    [[nodiscard]] ResolvedModelLimits
    resolve(const LLMProviderConfig &config, HttpClient &client, ProviderId provider_id);

  private:
    struct Entry {
        ProviderId provider_id{};
        std::string endpoint;
        std::string model;
        std::optional<int> context_override;
        std::optional<int> output_override;
        ResolvedModelLimits limits;
    };
    std::vector<Entry> entries_;
};

[[nodiscard]] std::expected<EffectiveRequestBudget, std::string>
resolve_request_budget(const LLMProviderConfig &config, const ResolvedModelLimits &limits);

[[nodiscard]] std::string_view completion_limit_field_name(CompletionLimitField field);

} // namespace ahfl::llm_provider
