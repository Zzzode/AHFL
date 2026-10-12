#include "runtime/providers/llm/model_capacity.hpp"

#include "base/json/json_value.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace ahfl::llm_provider {
namespace {

// Exact origin and API path matching; URL credentials, queries, fragments and custom ports
// cannot inherit an official provider's capabilities.
[[nodiscard]] std::string normalized_official_endpoint(std::string_view endpoint) {
    auto value = std::string(endpoint);
    while (!value.empty() && value.back() == '/')
        value.pop_back();
    const auto separator = value.find("://");
    if (separator == std::string::npos)
        return {};
    const auto path = value.find('/', separator + 3);
    if (path == std::string::npos)
        return {};
    for (std::size_t index = 0; index < path; ++index) {
        value[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
    }
    return value;
}

[[nodiscard]] std::optional<int> positive_int(const ahfl::json::JsonValue *value) {
    if (!value)
        return std::nullopt;
    const auto number = value->as_int();
    if (!number || *number <= 0 || *number > std::numeric_limits<int>::max())
        return std::nullopt;
    return static_cast<int>(*number);
}

[[nodiscard]] std::string encode_model_path(std::string_view model) {
    constexpr std::string_view hex = "0123456789ABCDEF";
    std::string encoded;
    for (const unsigned char value : model) {
        if (std::isalnum(value) || value == '-' || value == '_' || value == '.' || value == '/' ||
            value == '~') {
            encoded.push_back(static_cast<char>(value));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[value >> 4]);
            encoded.push_back(hex[value & 15]);
        }
    }
    return encoded;
}

[[nodiscard]] ResolvedModelLimits
discover(const LLMProviderConfig &config, HttpClient &client, ProviderId provider_id) {
    ResolvedModelLimits limits{.provider_id = provider_id};
    const auto endpoint = normalized_official_endpoint(config.endpoint);
    if (endpoint == "https://api.openai.com/v1") {
        // Official compare catalog, verified 2026-10-12:
        // https://developers.openai.com/api/docs/models/compare
        constexpr std::array<std::string_view, 3> models{
            "gpt-6-astra", "gpt-6.1-sol", "gpt-6-luna"};
        for (std::size_t index = 0; index < models.size(); ++index) {
            if (config.model == models[index]) {
                limits.profile_id = static_cast<std::uint32_t>(index + 1);
                limits.source = ModelLimitSource::OfficialProfile;
                limits.context_window_tokens = 1050000;
                limits.max_output_tokens = 128000;
                limits.completion_limit_field = CompletionLimitField::MaxCompletionTokens;
                break;
            }
        }
    } else if (endpoint == "https://openrouter.ai/api/v1" && !config.model_context_window_tokens) {
        // https://openrouter.ai/docs/api/api-reference/models/get-model
        // One exact model, not a catalog scan or guessed model-name family.
        const auto response = client.get("/model/" + encode_model_path(config.model));
        if (response.success()) {
            const auto parsed = ahfl::json::parse_json(response.body);
            if (parsed && *parsed) {
                const auto *data = (*parsed)->get("data");
                const auto *id = data ? data->get("id") : nullptr;
                const auto model = id ? id->as_string() : std::nullopt;
                const auto window = positive_int(data ? data->get("context_length") : nullptr);
                const auto *top = data ? data->get("top_provider") : nullptr;
                const auto output = positive_int(top ? top->get("max_completion_tokens") : nullptr);
                const auto top_window = positive_int(top ? top->get("context_length") : nullptr);
                if (model && *model == config.model && window && output && *output <= *window &&
                    (!top || !top->get("context_length") || top_window) &&
                    (!top_window || *output <= *top_window)) {
                    limits.source = ModelLimitSource::ProviderMetadata;
                    limits.context_window_tokens =
                        top_window ? std::min(*window, *top_window) : *window;
                    limits.max_output_tokens = std::min(*output, *limits.context_window_tokens);
                    limits.completion_limit_field = CompletionLimitField::MaxCompletionTokens;
                }
            }
        }
    }
    if (config.model_context_window_tokens || config.model_max_output_tokens) {
        limits.source = ModelLimitSource::ExplicitOverride;
        if (config.model_context_window_tokens)
            limits.context_window_tokens = config.model_context_window_tokens;
        if (config.model_max_output_tokens)
            limits.max_output_tokens = config.model_max_output_tokens;
    }
    return limits;
}

} // namespace

ResolvedModelLimits ModelCapacityResolver::resolve(const LLMProviderConfig &config,
                                                   HttpClient &client,
                                                   ProviderId provider_id) {
    for (const auto &entry : entries_) {
        if (entry.provider_id == provider_id && entry.endpoint == config.endpoint &&
            entry.model == config.model &&
            entry.context_override == config.model_context_window_tokens &&
            entry.output_override == config.model_max_output_tokens) {
            return entry.limits;
        }
    }
    auto limits = discover(config, client, provider_id);
    entries_.push_back(Entry{provider_id,
                             config.endpoint,
                             config.model,
                             config.model_context_window_tokens,
                             config.model_max_output_tokens,
                             limits});
    return limits;
}

std::expected<EffectiveRequestBudget, std::string>
resolve_request_budget(const LLMProviderConfig &config, const ResolvedModelLimits &limits) {
    for (const auto value : {config.max_tokens,
                             config.max_prompt_tokens,
                             config.max_total_tokens,
                             limits.context_window_tokens,
                             limits.max_output_tokens}) {
        if (value && *value <= 0)
            return std::unexpected("LLM token limits must be positive");
    }
    const auto window = limits.context_window_tokens;
    if (window && limits.max_output_tokens && *limits.max_output_tokens > *window) {
        return std::unexpected("LLM model_max_output_tokens exceeds model_context_window_tokens");
    }
    const int physical_output =
        limits.max_output_tokens.value_or(window.value_or(std::numeric_limits<int>::max()));
    const int output = config.max_tokens.value_or(std::min(1024, physical_output));
    if (output > physical_output || (window && output >= *window)) {
        return std::unexpected("LLM max_tokens exceeds the resolved model output/context capacity");
    }
    const int total = window ? std::min(config.max_total_tokens.value_or(*window), *window)
                             : config.max_total_tokens.value_or(4096);
    if (output >= total)
        return std::unexpected(
            "LLM max_tokens must leave space in max_total_tokens for the prompt");
    const int reserve = window ? std::max(output, *window / 10 + (*window % 10 != 0)) : output;
    const int physical_prompt = window ? *window - reserve : total - output;
    const int prompt = std::min(
        {config.max_prompt_tokens.value_or(physical_prompt), physical_prompt, total - output});
    if (prompt <= 0)
        return std::unexpected("LLM resolved model capacity leaves no prompt budget");
    return EffectiveRequestBudget{
        .max_tokens = output,
        .max_prompt_tokens = prompt,
        .max_total_tokens = total,
        .safety_reserve_tokens = reserve,
        .compact_threshold_tokens = static_cast<int>(static_cast<std::int64_t>(prompt) * 4 / 5),
        .model_limits = limits,
    };
}

std::string_view completion_limit_field_name(CompletionLimitField field) {
    return field == CompletionLimitField::MaxCompletionTokens ? "max_completion_tokens"
                                                              : "max_tokens";
}

} // namespace ahfl::llm_provider
