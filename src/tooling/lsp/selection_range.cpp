#include "tooling/lsp/selection_range.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::lsp {

namespace {

// ---------- Position / offset helpers ----------

[[nodiscard]] std::size_t offset_of(const std::string &source, Position pos) {
    std::size_t offset = 0;
    std::uint32_t line = 0;
    while (line < pos.line && offset < source.size()) {
        if (source[offset] == '\n') {
            ++line;
        }
        ++offset;
    }
    return std::min(offset + static_cast<std::size_t>(pos.character), source.size());
}

[[nodiscard]] Position position_of(const std::string &source, std::size_t offset) {
    if (offset >= source.size()) {
        // Return position at end of file.
        std::uint32_t line = 0;
        for (char c : source) {
            if (c == '\n') {
                ++line;
            }
        }
        return Position{.line = line, .character = 0};
    }

    std::uint32_t line = 0;
    std::uint32_t column = 0;
    for (std::size_t i = 0; i < offset; ++i) {
        if (source[i] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    return Position{.line = line, .character = column};
}

[[nodiscard]] Range make_range(const std::string &source, std::size_t begin, std::size_t end) {
    return Range{
        .start = position_of(source, begin),
        .end = position_of(source, end),
    };
}

// ---------- Level 1: Identifier ----------

[[nodiscard]] bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

[[nodiscard]] std::optional<Range> identifier_range(const std::string &source, std::size_t offset) {
    if (offset >= source.size() || !is_ident_char(source[offset])) {
        // If the position is not on an identifier character, check if we are
        // just past the end of an identifier (common case: cursor at end of word).
        if (offset > 0 && offset <= source.size() && is_ident_char(source[offset - 1])) {
            --offset;
        } else {
            return std::nullopt;
        }
    }

    std::size_t begin = offset;
    while (begin > 0 && is_ident_char(source[begin - 1])) {
        --begin;
    }

    std::size_t end = offset;
    while (end < source.size() && is_ident_char(source[end])) {
        ++end;
    }

    if (begin == end) {
        return std::nullopt;
    }
    return make_range(source, begin, end);
}

// ---------- Level 2: Expression (smallest enclosing bracket pair) ----------

// Returns the range of the smallest enclosing bracket pair (one of ()[]{}).
[[nodiscard]] std::optional<Range> expression_range(const std::string &source, std::size_t offset) {
    struct BracketPair {
        char open;
        char close;
    };
    static constexpr BracketPair kPairs[] = {
        {'(', ')'},
        {'[', ']'},
        {'{', '}'},
    };

    std::size_t best_begin = 0;
    std::size_t best_end = 0;
    bool found = false;

    for (const auto &pair : kPairs) {
        // Scan backward to find the matching opening bracket.
        int depth = 0;
        std::size_t open_pos = std::string::npos;
        for (std::size_t i = offset; i > 0; --i) {
            const char c = source[i - 1];
            if (c == pair.close) {
                ++depth;
            } else if (c == pair.open) {
                if (depth == 0) {
                    open_pos = i - 1;
                    break;
                }
                --depth;
            }
        }
        if (open_pos == std::string::npos) {
            continue;
        }

        // Scan forward to find the matching closing bracket.
        depth = 0;
        std::size_t close_pos = std::string::npos;
        for (std::size_t i = open_pos + 1; i < source.size(); ++i) {
            const char c = source[i];
            if (c == pair.open) {
                ++depth;
            } else if (c == pair.close) {
                if (depth == 0) {
                    close_pos = i;
                    break;
                }
                --depth;
            }
        }
        if (close_pos == std::string::npos) {
            continue;
        }

        // We include the brackets themselves in the range.
        const std::size_t range_begin = open_pos;
        const std::size_t range_end = close_pos + 1;

        if (!found || (range_end - range_begin) < (best_end - best_begin)) {
            best_begin = range_begin;
            best_end = range_end;
            found = true;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return make_range(source, best_begin, best_end);
}

// ---------- Level 3: Line ----------

[[nodiscard]] Range line_range(const std::string &source, std::size_t offset) {
    std::size_t begin = offset;
    while (begin > 0 && source[begin - 1] != '\n') {
        --begin;
    }

    std::size_t end = offset;
    while (end < source.size() && source[end] != '\n') {
        ++end;
    }

    return make_range(source, begin, end);
}

// ---------- Level 4: Block (enclosing braced block) ----------

[[nodiscard]] std::optional<Range> block_range(const std::string &source, std::size_t offset) {
    // Find the enclosing { } pair.
    int depth = 0;
    std::size_t open_pos = std::string::npos;
    for (std::size_t i = offset; i > 0; --i) {
        const char c = source[i - 1];
        if (c == '}') {
            ++depth;
        } else if (c == '{') {
            if (depth == 0) {
                open_pos = i - 1;
                break;
            }
            --depth;
        }
    }
    if (open_pos == std::string::npos) {
        return std::nullopt;
    }

    // Find matching closing brace.
    depth = 0;
    std::size_t close_pos = std::string::npos;
    for (std::size_t i = open_pos + 1; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (depth == 0) {
                close_pos = i;
                break;
            }
            --depth;
        }
    }
    if (close_pos == std::string::npos) {
        return std::nullopt;
    }

    return make_range(source, open_pos, close_pos + 1);
}

// ---------- Level 5: Function / top-level declaration ----------

[[nodiscard]] bool is_declaration_keyword(std::string_view word) {
    static constexpr std::string_view kKeywords[] = {
        "capability",
        "predicate",
        "agent",
        "workflow",
        "struct",
        "enum",
        "type",
        "const",
        "import",
        "module",
    };
    for (const auto kw : kKeywords) {
        if (word == kw) {
            return true;
        }
    }
    return false;
}

// Returns the word at the start of the given line (skipping leading whitespace).
[[nodiscard]] std::string_view first_word_of_line(const std::string &source, std::size_t line_start) {
    std::size_t i = line_start;
    while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) {
        ++i;
    }
    std::size_t word_start = i;
    while (i < source.size() && is_ident_char(source[i])) {
        ++i;
    }
    if (i == word_start) {
        return {};
    }
    return std::string_view(source).substr(word_start, i - word_start);
}

// Find the start of the line containing `offset`.
[[nodiscard]] std::size_t line_start(const std::string &source, std::size_t offset) {
    std::size_t begin = offset;
    while (begin > 0 && source[begin - 1] != '\n') {
        --begin;
    }
    return begin;
}

// Find the end of the line containing `offset` (position of '\n' or end of file).
[[nodiscard]] std::size_t line_end(const std::string &source, std::size_t offset) {
    std::size_t end = offset;
    while (end < source.size() && source[end] != '\n') {
        ++end;
    }
    return end;
}

[[nodiscard]] std::optional<Range> declaration_range(const std::string &source, std::size_t offset) {
    // Walk backward through lines until we find a top-level (column 0 or near 0)
    // declaration keyword.
    std::size_t current = line_start(source, offset);
    std::size_t decl_start = std::string::npos;

    while (true) {
        const auto word = first_word_of_line(source, current);
        const std::size_t word_pos = current + (word.data() - source.data() - current);
        const std::size_t col = word_pos - current;

        // Consider it "top-level" if it starts at column < 4 (some indentation tolerance)
        // and matches a declaration keyword.
        if (!word.empty() && col < 4 && is_declaration_keyword(word)) {
            decl_start = current;
            break;
        }

        if (current == 0) {
            break;
        }
        // Move to start of previous line.
        current = line_start(source, current - 1);
    }

    if (decl_start == std::string::npos) {
        return std::nullopt;
    }

    // Walk forward to find the end of this declaration. For a simple heuristic,
    // we find the next top-level declaration keyword or end of file.
    // If declaration has a body (braces), extend to after the closing brace.
    std::size_t decl_end = source.size();

    // First, check if this declaration has a { } body.
    const auto brace_range = block_range(source, line_end(source, decl_start));
    if (brace_range.has_value()) {
        const auto block_end_offset = offset_of(source, brace_range->end);
        // Only use block_range if its opening brace comes after the declaration start.
        if (block_end_offset > decl_start) {
            // Extend past the block's closing brace to include trailing content on that line.
            decl_end = line_end(source, block_end_offset);
        }
    }

    // Fallback: find next top-level declaration.
    if (decl_end == source.size()) {
        std::size_t line_pos = line_end(source, decl_start) + 1;
        while (line_pos < source.size()) {
            const auto word = first_word_of_line(source, line_pos);
            const std::size_t col = (word.data() - source.data()) - line_pos;
            if (!word.empty() && col < 4 && is_declaration_keyword(word)) {
                decl_end = line_start(source, line_pos);
                break;
            }
            line_pos = line_end(source, line_pos);
            if (line_pos < source.size()) {
                ++line_pos; // skip '\n'
            }
        }
    }

    if (decl_start >= decl_end) {
        return std::nullopt;
    }
    return make_range(source, decl_start, decl_end);
}

// ---------- Level 6: File ----------

[[nodiscard]] Range file_range(const std::string &source) {
    return make_range(source, 0, source.size());
}

// ---------- Chain builder ----------

// Returns true if `outer` strictly contains `inner` (i.e., outer starts <= inner
// and ends >= inner, and they are not identical).
[[nodiscard]] bool range_contains(const Range &outer, const Range &inner) {
    const bool starts_before =
        (outer.start.line < inner.start.line) ||
        (outer.start.line == inner.start.line &&
         outer.start.character <= inner.start.character);
    const bool ends_after =
        (outer.end.line > inner.end.line) ||
        (outer.end.line == inner.end.line &&
         outer.end.character >= inner.end.character);

    const bool same =
        outer.start.line == inner.start.line &&
        outer.start.character == inner.start.character &&
        outer.end.line == inner.end.line &&
        outer.end.character == inner.end.character;

    return starts_before && ends_after && !same;
}

// Add a range to the end of the chain (as a new outermost parent) if it is
// strictly larger than the current outermost range.
void push_parent_if_larger(SelectionRange &root, const Range &range) {
    // Walk to the end of the chain (the outermost range).
    SelectionRange *tail = &root;
    while (tail->parent != nullptr) {
        tail = tail->parent.get();
    }

    if (!range_contains(range, tail->range)) {
        return;
    }

    auto next = std::make_unique<SelectionRange>();
    next->range = range;
    tail->parent = std::move(next);
}

[[nodiscard]] SelectionRange build_chain(const std::string &source, Position pos) {
    const std::size_t offset = offset_of(source, pos);

    // Collect candidate ranges from innermost to outermost.
    std::vector<Range> candidates;

    // Level 1: identifier
    if (const auto r = identifier_range(source, offset); r.has_value()) {
        candidates.push_back(*r);
    }

    // Level 2: expression (smallest bracket pair)
    if (const auto r = expression_range(source, offset); r.has_value()) {
        candidates.push_back(*r);
    }

    // Level 3: line
    candidates.push_back(line_range(source, offset));

    // Level 4: block (braced)
    if (const auto r = block_range(source, offset); r.has_value()) {
        candidates.push_back(*r);
    }

    // Level 5: declaration / function
    if (const auto r = declaration_range(source, offset); r.has_value()) {
        candidates.push_back(*r);
    }

    // Level 6: file
    candidates.push_back(file_range(source));

    // Build the chain: start with the first (innermost) candidate, then push
    // each subsequent candidate as a parent if it is strictly larger.
    // There is always at least one candidate (line level is always present).

    SelectionRange root;
    root.range = candidates.front();

    for (std::size_t i = 1; i < candidates.size(); ++i) {
        push_parent_if_larger(root, candidates[i]);
    }

    return root;
}

} // namespace

std::vector<SelectionRange> compute_selection_ranges(const std::string &source,
                                                     const std::vector<Position> &positions) {
    std::vector<SelectionRange> result;
    result.reserve(positions.size());

    for (const auto &pos : positions) {
        result.push_back(build_chain(source, pos));
    }

    return result;
}

} // namespace ahfl::lsp
