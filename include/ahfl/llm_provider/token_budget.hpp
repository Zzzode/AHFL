#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ahfl::llm_provider {

/// Heuristic token counter (chars/4 approximation).
class TokenCounter {
  public:
    [[nodiscard]] static std::size_t estimate(std::string_view text);
};

/// Budget constraints for prompt construction.
struct TokenBudget {
    std::size_t max_total{4096};
    std::size_t max_prompt{3072};
    std::size_t max_response{1024};
};

/// Truncates prompts to fit within budget.
class PromptTruncator {
  public:
    explicit PromptTruncator(TokenBudget budget) : budget_(budget) {}

    /// Truncate system and user prompts to fit within max_prompt budget.
    /// Returns the truncated user prompt (system prompt has priority).
    [[nodiscard]] std::string truncate(std::string_view system, std::string_view user) const;

    [[nodiscard]] const TokenBudget &budget() const { return budget_; }

  private:
    TokenBudget budget_;
};

} // namespace ahfl::llm_provider
