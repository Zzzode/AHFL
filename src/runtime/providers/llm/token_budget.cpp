#include "runtime/providers/llm/token_budget.hpp"

#include <algorithm>
#include <string>

namespace ahfl::llm_provider {

std::size_t TokenCounter::estimate(std::string_view text) {
    // Heuristic: ~4 characters per token on average for English text
    return (text.size() + 3) / 4;
}

std::string PromptTruncator::truncate(std::string_view system, std::string_view user) const {
    std::size_t system_tokens = TokenCounter::estimate(system);
    std::size_t available = budget_.max_prompt;

    if (system_tokens >= available) {
        // System prompt alone exceeds budget — return empty user prompt
        return {};
    }

    std::size_t user_budget = available - system_tokens;
    std::size_t user_tokens = TokenCounter::estimate(user);

    if (user_tokens <= user_budget) {
        return std::string(user);
    }

    // Truncate user prompt to fit (approximate by characters)
    std::size_t max_chars = user_budget * 4;
    if (max_chars >= user.size()) {
        return std::string(user);
    }
    return std::string(user.substr(0, max_chars));
}

} // namespace ahfl::llm_provider
