#pragma once

#include <cstddef>
#include <string_view>

namespace ahfl::llm_provider {

/// Heuristic token counter (UTF-8 bytes/4 approximation; not a model tokenizer).
class TokenCounter {
  public:
    [[nodiscard]] static std::size_t estimate(std::string_view text);
};

} // namespace ahfl::llm_provider
