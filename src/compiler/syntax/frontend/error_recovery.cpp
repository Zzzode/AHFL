#include "compiler/syntax/frontend/error_recovery.hpp"

#include "ahfl/compiler/frontend/frontend.hpp"

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
    std::vector<std::vector<std::size_t>> dp(m + 1, std::vector<std::size_t>(n + 1, 0));

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
                dp[i][j] = 1 + std::min({
                                   dp[i - 1][j],    // deletion
                                   dp[i][j - 1],    // insertion
                                   dp[i - 1][j - 1] // substitution
                               });
            }
        }
    }

    return dp[m][n];
}

std::vector<RecoverySuggestion>
AhflErrorStrategy::compute_suggestions(std::string_view token,
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
        double conf =
            (max_len == 0) ? 1.0 : 1.0 - (static_cast<double>(dist) / static_cast<double>(max_len));
        scored.push_back({candidate, dist, conf});
    }

    // Sort by distance ascending (lower distance = better match)
    std::sort(scored.begin(), scored.end(), [](const Scored &lhs, const Scored &rhs) {
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

RecoverySuggestion AhflErrorStrategy::best_match(std::string_view token,
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
    return {"module",   "import",     "as",         "struct",   "enum",     "type",
            "const",    "capability", "predicate",  "agent",    "contract", "flow",
            "workflow", "state",      "states",     "initial",  "final",    "input",
            "output",   "context",    "transition", "requires", "ensures",  "invariant",
            "forbid",   "always",     "eventually", "next",     "until",    "called",
            "in_state", "running",    "completed",  "let",      "if",       "else",
            "goto",     "return",     "assert",     "true",     "false",    "none"};
}

namespace {

/// Extract the offending token spelling from an ANTLR diagnostic message.
/// ANTLR phrases parse errors as e.g. "extraneous input 'xyz' expecting ..."
/// or "mismatched input 'foo' expecting ...". We pull the first single-quoted
/// lexeme so we can offer an edit-distance keyword suggestion for it. Returns
/// an empty string when no quoted token is present.
[[nodiscard]] std::string offending_token_from_message(std::string_view message) {
    const auto open = message.find('\'');
    if (open == std::string_view::npos) {
        return {};
    }
    const auto close = message.find('\'', open + 1);
    if (close == std::string_view::npos || close <= open + 1) {
        return {};
    }
    return std::string(message.substr(open + 1, close - open - 1));
}

} // anonymous namespace

PartialParseResult parse_with_recovery(std::string_view source, std::string_view filename) {
    PartialParseResult result;

    // KR5.6: drive the real ANTLR-backed front end instead of an ad-hoc
    // whitespace re-tokenizer. ANTLR's default error strategy already recovers
    // from syntax errors (extraneous / missing / mismatched tokens) and keeps
    // parsing, so a single call yields BOTH the surviving partial AST and the
    // full multi-error diagnostic set. We derive the PartialParseResult from
    // those real results and reuse the unified edit_distance for suggestions.
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(std::string(filename), std::string(source));

    if (parse_result.program != nullptr) {
        result.valid_declaration_count = parse_result.program->declarations.size();
    }
    result.has_partial_ast = result.valid_declaration_count > 0;

    const auto dictionary = AhflErrorStrategy::keyword_dictionary();
    const AhflErrorStrategy strategy;

    for (const auto &diagnostic : parse_result.diagnostics.entries()) {
        if (diagnostic.severity != DiagnosticSeverity::Error) {
            continue;
        }
        ++result.error_count;
        result.error_messages.push_back(diagnostic.message);

        // Offer a "did you mean <keyword>?" suggestion when the offending token
        // is a near-miss of an AHFL keyword (misspelled keyword recovery).
        const auto token = offending_token_from_message(diagnostic.message);
        if (token.empty()) {
            continue;
        }
        auto best = strategy.best_match(token, dictionary);
        if (best.confidence > 0.6 && best.confidence < 1.0) {
            result.suggestions.push_back(std::move(best));
        }
    }

    return result;
}

} // namespace ahfl::frontend
