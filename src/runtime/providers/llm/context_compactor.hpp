#pragma once
#include "runtime/providers/llm/llm_session_store.hpp"
#include <expected>
#include <string>
#include <vector>
namespace ahfl::llm_provider {
/// Returns a closed oldest exchange prefix, retaining the latest full exchange.
[[nodiscard]] std::expected<std::size_t, std::string> compact_prefix_end(const LLMSession &);
[[nodiscard]] std::vector<SessionMessage> active_session_messages(const LLMSession &);
[[nodiscard]] std::string compact_summary_input(const LLMSession &, std::size_t prefix_end);
[[nodiscard]] std::expected<std::string, std::string> parse_compact_summary(std::string_view);
} // namespace ahfl::llm_provider
