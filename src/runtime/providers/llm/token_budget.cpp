#include "runtime/providers/llm/token_budget.hpp"

namespace ahfl::llm_provider {

std::size_t TokenCounter::estimate(std::string_view text) {
    // Heuristic: ~4 UTF-8 bytes per token for typical English text.
    return text.size() / 4 + (text.size() % 4 != 0);
}

} // namespace ahfl::llm_provider
