#include "frontend/error_recovery.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::frontend {

std::size_t edit_distance(std::string_view a, std::string_view b) {
    const auto m = a.size();
    const auto n = b.size();

    // 2D DP matrix
    std::vector<std::vector<std::size_t>> dp(m + 1,
                                             std::vector<std::size_t>(n + 1, 0));

    for (std::size_t i = 0; i <= m; ++i) {
        dp[i][0] = i;
    }
    for (std::size_t j = 0; j <= n; ++j) {
        dp[0][j] = j;
    }

    for (std::size_t i = 1; i <= m; ++i) {
        for (std::size_t j = 1; j <= n; ++j) {
            if (a[i - 1] == b[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j],      // deletion
                                          dp[i][j - 1],      // insertion
                                          dp[i - 1][j - 1]   // substitution
                });
            }
        }
    }

    return dp[m][n];
}

std::vector<RecoverySuggestion>
AhflErrorStrategy::compute_suggestions(
    std::string_view token,
    const std::vector<std::string> &candidates,
    std::size_t max_results) const {

    struct Scored {
        std::string candidate;
        std::size_t distance{0};
        double confidence{0.0};
    };

    std::vector<Scored> scored;
    scored.reserve(candidates.size());

    for (const auto &candidate : candidates) {
        auto dist = edit_distance(token, candidate);
        auto max_len = std::max(token.size(), candidate.size());
        double conf = (max_len == 0)
                          ? 1.0
                          : 1.0 - (static_cast<double>(dist) /
                                   static_cast<double>(max_len));
        scored.push_back({candidate, dist, conf});
    }

    // Sort by distance ascending (lower distance = better match)
    std::sort(scored.begin(), scored.end(),
              [](const Scored &lhs, const Scored &rhs) {
                  return lhs.distance < rhs.distance;
              });

    std::vector<RecoverySuggestion> results;
    auto count = std::min(max_results, scored.size());
    results.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        RecoverySuggestion suggestion;
        suggestion.original_token = std::string(token);
        suggestion.suggested_token = scored[i].candidate;
        suggestion.confidence = scored[i].confidence;
        results.push_back(std::move(suggestion));
    }

    return results;
}

RecoverySuggestion
AhflErrorStrategy::best_match(std::string_view token,
                              const std::vector<std::string> &dictionary) const {
    auto suggestions = compute_suggestions(token, dictionary, 1);
    if (suggestions.empty()) {
        RecoverySuggestion empty;
        empty.original_token = std::string(token);
        return empty;
    }
    return suggestions[0];
}

std::vector<std::string> AhflErrorStrategy::keyword_dictionary() {
    return {
        "module",     "import",    "as",         "struct",     "enum",
        "type",       "const",     "capability", "predicate",  "agent",
        "contract",   "flow",      "workflow",   "state",      "states",
        "initial",    "final",     "input",      "output",     "context",
        "transition", "requires",  "ensures",    "invariant",  "forbid",
        "always",     "eventually","next",       "until",      "called",
        "in_state",   "running",   "completed",  "let",        "if",
        "else",       "goto",      "return",     "assert",     "true",
        "false",      "none"
    };
}

namespace {

/// Check if a token matches a top-level declaration keyword
bool is_declaration_keyword(std::string_view token) {
    static const std::vector<std::string_view> decl_keywords = {
        "struct", "enum", "agent", "workflow", "contract", "flow",
        "capability", "predicate", "type", "module", "const"
    };
    for (auto kw : decl_keywords) {
        if (token == kw) {
            return true;
        }
    }
    return false;
}

/// Check if a token is a known keyword
bool is_keyword(std::string_view token) {
    auto dict = AhflErrorStrategy::keyword_dictionary();
    for (const auto &kw : dict) {
        if (token == kw) {
            return true;
        }
    }
    return false;
}

/// Simple tokenizer: split source into whitespace-separated tokens
std::vector<std::string> tokenize(std::string_view source) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        // Skip whitespace and punctuation that isn't part of identifiers
        while (i < source.size() &&
               (source[i] == ' ' || source[i] == '\t' ||
                source[i] == '\n' || source[i] == '\r' ||
                source[i] == '{' || source[i] == '}' ||
                source[i] == '(' || source[i] == ')' ||
                source[i] == ';' || source[i] == ':' ||
                source[i] == ',' || source[i] == '.')) {
            ++i;
        }
        if (i >= source.size()) {
            break;
        }
        // Collect token characters (identifiers and underscores)
        std::size_t start = i;
        while (i < source.size() &&
               source[i] != ' ' && source[i] != '\t' &&
               source[i] != '\n' && source[i] != '\r' &&
               source[i] != '{' && source[i] != '}' &&
               source[i] != '(' && source[i] != ')' &&
               source[i] != ';' && source[i] != ':' &&
               source[i] != ',' && source[i] != '.') {
            ++i;
        }
        if (i > start) {
            tokens.emplace_back(source.substr(start, i - start));
        }
    }
    return tokens;
}

} // anonymous namespace

PartialParseResult
parse_with_recovery(std::string_view source, std::string_view filename) {
    PartialParseResult result;

    auto tokens = tokenize(source);
    auto dict = AhflErrorStrategy::keyword_dictionary();
    AhflErrorStrategy strategy;

    // Scan for top-level declaration keywords to count valid declarations
    for (const auto &tok : tokens) {
        if (is_declaration_keyword(tok)) {
            ++result.valid_declaration_count;
        }
    }

    result.has_partial_ast = (result.valid_declaration_count > 0);

    // Identify tokens that look like they could be misspelled keywords
    // Heuristic: if a token is not a keyword, not a common identifier pattern
    // (starts with uppercase = type name, or contains digits), check if it's
    // close to a keyword
    for (const auto &tok : tokens) {
        if (is_keyword(tok)) {
            continue;
        }
        // Skip tokens that look like identifiers (capitalized names, numbers)
        if (!tok.empty() && (tok[0] >= 'A' && tok[0] <= 'Z')) {
            continue;
        }
        // Skip string literals or numeric tokens
        if (!tok.empty() && (tok[0] >= '0' && tok[0] <= '9')) {
            continue;
        }
        // Check edit distance to keywords
        auto best = strategy.best_match(tok, dict);
        if (best.confidence > 0.6 && best.confidence < 1.0) {
            // This token is close to a keyword but not exact — likely an error
            ++result.error_count;
            result.suggestions.push_back(best);
            std::string msg = "unknown token '" + tok + "'";
            if (!filename.empty()) {
                msg = std::string(filename) + ": " + msg;
            }
            if (!best.suggested_token.empty()) {
                msg += "; did you mean '" + best.suggested_token + "'?";
            }
            result.error_messages.push_back(std::move(msg));
        }
    }

    return result;
}

} // namespace ahfl::frontend
