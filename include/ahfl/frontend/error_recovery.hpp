#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::frontend {

struct RecoverySuggestion {
    std::string original_token;
    std::string suggested_token;
    double confidence{0.0};
};

struct PartialParseResult {
    bool has_partial_ast{false};
    std::size_t valid_declaration_count{0};
    std::size_t error_count{0};
    std::vector<RecoverySuggestion> suggestions;
    std::vector<std::string> error_messages;
};

class AhflErrorStrategy {
public:
    /// Compute "did you mean...?" suggestions using edit distance
    [[nodiscard]] std::vector<RecoverySuggestion>
    compute_suggestions(std::string_view token,
                        const std::vector<std::string> &candidates,
                        std::size_t max_results = 3) const;

    /// Find the best single match
    [[nodiscard]] RecoverySuggestion
    best_match(std::string_view token,
               const std::vector<std::string> &dictionary) const;

    /// Get AHFL keyword dictionary
    [[nodiscard]] static std::vector<std::string> keyword_dictionary();
};

/// Levenshtein edit distance
[[nodiscard]] std::size_t
edit_distance(std::string_view a, std::string_view b);

/// Parse with error recovery — collects partial results even on failure
[[nodiscard]] PartialParseResult
parse_with_recovery(std::string_view source, std::string_view filename = "");

} // namespace ahfl::frontend
