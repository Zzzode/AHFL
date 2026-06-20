#include "tooling/lsp/document_highlight.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::lsp {

namespace {

[[nodiscard]] bool is_word_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/// Convert a byte offset in the source to a (line, character) position.
/// Lines and characters are 0-based.
[[nodiscard]] Position offset_to_position(std::string_view source, std::size_t offset) {
    Position pos;
    std::size_t line_start = 0;
    for (std::size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++pos.line;
            line_start = i + 1;
        }
    }
    pos.character = static_cast<uint32_t>(offset - line_start);
    return pos;
}

/// Convert a (line, character) position to a byte offset in the source.
/// If the position is past the end of the document, returns source.size().
[[nodiscard]] std::size_t position_to_offset(std::string_view source, const Position &position) {
    std::size_t offset = 0;
    std::size_t line = 0;
    while (line < position.line && offset < source.size()) {
        if (source[offset] == '\n') {
            ++line;
        }
        ++offset;
    }
    if (line < position.line) {
        return source.size();
    }
    std::size_t line_end = offset;
    while (line_end < source.size() && source[line_end] != '\n') {
        ++line_end;
    }
    std::size_t char_offset = offset + position.character;
    if (char_offset > line_end) {
        char_offset = line_end;
    }
    return char_offset;
}

/// Find the word (identifier) that contains the given byte offset.
/// Returns the start and end offsets of the word, or nullopt if the
/// offset is not inside a word.
[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>>
find_word_at_offset(std::string_view source, std::size_t offset) {
    if (offset >= source.size() || !is_word_char(source[offset])) {
        return std::nullopt;
    }
    std::size_t start = offset;
    while (start > 0 && is_word_char(source[start - 1])) {
        --start;
    }
    std::size_t end = offset;
    while (end < source.size() && is_word_char(source[end])) {
        ++end;
    }
    return std::make_pair(start, end);
}

} // namespace

std::vector<DocumentHighlight> compute_document_highlights(const std::string &source,
                                                           const Position &position) {
    std::vector<DocumentHighlight> result;

    const std::size_t cursor_offset = position_to_offset(source, position);
    auto word_bounds = find_word_at_offset(source, cursor_offset);
    if (!word_bounds.has_value()) {
        return result;
    }

    const std::size_t word_start = word_bounds->first;
    const std::size_t word_end = word_bounds->second;
    const std::string_view word(source.data() + word_start, word_end - word_start);

    if (word.empty()) {
        return result;
    }

    // Search for all occurrences of the word in the source.
    std::size_t search_pos = 0;
    while ((search_pos = source.find(word, search_pos)) != std::string::npos) {
        const std::size_t match_start = search_pos;
        const std::size_t match_end = search_pos + word.size();

        // Check that the match is a whole word (not part of a longer identifier).
        const bool at_word_start = match_start == 0 || !is_word_char(source[match_start - 1]);
        const bool at_word_end = match_end == source.size() || !is_word_char(source[match_end]);

        if (at_word_start && at_word_end) {
            const Position start_pos = offset_to_position(source, match_start);
            const Position end_pos = offset_to_position(source, match_end);
            result.push_back(DocumentHighlight{
                .range = Range{.start = start_pos, .end = end_pos},
                .kind = DocumentHighlightKind::Text,
            });
        }

        // Move past this match to avoid infinite loops on zero-length matches.
        if (match_end == search_pos) {
            ++search_pos;
        } else {
            search_pos = match_end;
        }
    }

    return result;
}

} // namespace ahfl::lsp
