#include "tooling/lsp/code_action.hpp"

#include "tooling/formatter/formatter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::lsp {

namespace {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

[[nodiscard]] Position end_of_document(const std::string &source) {
    if (source.empty()) {
        return Position{0, 0};
    }

    uint32_t line = 0;
    uint32_t last_line_start = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++line;
            last_line_start = static_cast<uint32_t>(i + 1);
        }
    }
    const uint32_t character = static_cast<uint32_t>(source.size() - last_line_start);
    return Position{line, character};
}

[[nodiscard]] std::size_t line_start_offset(const std::string &source, uint32_t line) {
    std::size_t offset = 0;
    for (uint32_t l = 0; l < line; ++l) {
        auto nl = source.find('\n', offset);
        if (nl == std::string::npos) {
            return source.size();
        }
        offset = nl + 1;
    }
    return offset;
}

[[nodiscard]] std::string_view line_text(const std::string &source, uint32_t line) {
    const auto start = line_start_offset(source, line);
    if (start >= source.size()) {
        return {};
    }
    const auto nl = source.find('\n', start);
    const auto len = (nl == std::string::npos) ? (source.size() - start) : (nl - start);
    return std::string_view(source).substr(start, len);
}

// Variant of line_text that takes a byte offset instead of a line number.
// Used by scan loops that walk the source by offset rather than line index.
[[nodiscard]] std::string_view line_text_from(const std::string &source, std::size_t offset) {
    if (offset >= source.size()) {
        return {};
    }
    // Find newline before offset → line start.
    const auto prev_nl = source.rfind('\n', offset);
    const auto start = (prev_nl == std::string::npos) ? 0U : (prev_nl + 1);
    const auto nl = source.find('\n', offset);
    const auto len = (nl == std::string::npos) ? (source.size() - start) : (nl - start);
    return std::string_view(source).substr(start, len);
}

[[nodiscard]] bool range_intersects_imports(const std::string &source, const Range &range) {
    (void)source;
    (void)range;
    return true;
}

[[nodiscard]] std::string_view ltrim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    return s;
}

// Extract the module name ("other definition in module M") from
// relatedInformation message.
[[nodiscard]] std::string extract_module_name_from_related_message(std::string_view message) {
    constexpr std::string_view kInModule = "definition in module ";
    auto pos = message.find(kInModule);
    if (pos != std::string_view::npos) {
        auto rest = message.substr(pos + kInModule.size());
        // trim trailing whitespace / punctuation
        while (!rest.empty() && (rest.back() == '.' || rest.back() == ' ' || rest.back() == '\n')) {
            rest.remove_suffix(1);
        }
        return std::string(rest);
    }
    constexpr std::string_view kOtherDecl = "other declaration in module ";
    pos = message.find(kOtherDecl);
    if (pos != std::string_view::npos) {
        auto rest = message.substr(pos + kOtherDecl.size());
        while (!rest.empty() && (rest.back() == '.' || rest.back() == ' ' || rest.back() == '\n')) {
            rest.remove_suffix(1);
        }
        return std::string(rest);
    }
    return {};
}

// ---------------------------------------------------------------------------
// QF-1: DUPLICATE_STRUCT_NAME -> navigate to other definition
// ---------------------------------------------------------------------------

constexpr std::string_view kCodeDuplicateStructName = "lint.DUPLICATE_STRUCT_NAME";
constexpr std::string_view kCodeUnusedImport = "lint.UNUSED_IMPORT";
constexpr std::string_view kCodeWrongArity = "typecheck.WRONG_ARITY";
constexpr std::string_view kCodeMatchMissingPatterns = "typecheck.MATCH_MISSING_PATTERNS";
constexpr std::string_view kCodeMatchUnreachableArm = "typecheck.MATCH_UNREACHABLE_ARM";
constexpr std::string_view kCodeMatchRedundantPattern = "typecheck.MATCH_REDUNDANT_PATTERN";
constexpr std::string_view kCodeUnreachableIfLetElse = "typecheck.UNREACHABLE_IF_LET_ELSE";
constexpr std::string_view kCodeInvalidRangePattern = "typecheck.INVALID_RANGE_PATTERN";
constexpr std::string_view kCodeMissingVariantField = "typecheck.MISSING_VARIANT_FIELD";
constexpr std::string_view kCodeUnexpectedVariantField = "typecheck.UNEXPECTED_VARIANT_FIELD";
constexpr std::string_view kCodeDuplicateVariantField = "typecheck.DUPLICATE_VARIANT_FIELD";
// Wave-21 A-2: QW-4 two new optional-agent-section warnings → insert TextEdit.
// Full code = "typecheck.AGENT_CONTEXT_OMITTED" / "...CAPABILITIES_OMITTED".
constexpr std::string_view kCodeAgentContextOmitted = "typecheck.AGENT_CONTEXT_OMITTED";
constexpr std::string_view kCodeAgentCapabilitiesOmitted = "typecheck.AGENT_CAPABILITIES_OMITTED";

[[nodiscard]] std::vector<CodeAction> qf_duplicate_struct(const LspDiagnostic &diag) {
    std::vector<CodeAction> actions;
    if (diag.related_information.empty()) {
        return actions;
    }

    for (const auto &related : diag.related_information) {
        const auto module_name = extract_module_name_from_related_message(related.message);
        if (module_name.empty()) {
            continue;
        }

        CodeAction action;
        action.title = "Go to other definition in module " + module_name;
        action.kind = CodeActionKind::QuickFix;
        action.diagnostics = {diag};
        // Command: custom ahfl.gotoSymbol with arguments [uri, range].
        action.command = "ahfl.gotoSymbol";

        // Serialize range into JSON-ish string form — arguments are
        // forwarded as individual string arguments. The client-side
        // command handler reads arguments in order: uri, startLine,
        // startCharacter, endLine, endCharacter.
        const auto &r = related.location.range;
        action.command_arguments = {
            related.location.uri,
            std::to_string(r.start.line),
            std::to_string(r.start.character),
            std::to_string(r.end.line),
            std::to_string(r.end.character),
        };
        actions.push_back(std::move(action));
    }
    return actions;
}

// ---------------------------------------------------------------------------
// QF-2: UNUSED_IMPORT -> delete the whole import statement
// ---------------------------------------------------------------------------

// Find the Range of the full import statement that contains `line`.
// Import lines may be continued with a "from" clause on the same or
// following line, so we scan forward until we hit a terminating ';' or
// a non-import top-level item.
[[nodiscard]] std::optional<Range> find_import_statement_range(const std::string &source,
                                                               const LspDiagnostic &diag) {
    // Start from the diagnostic's own range start line; if the range
    // starts before the import keyword on its line, step forward to the
    // first line whose trimmed text starts with the "import" keyword
    // within [range.start.line .. range.end.line].
    uint32_t start_line = diag.range.start.line;
    uint32_t end_line_limit = diag.range.end.line;

    // Walk forward/backward a couple of lines to find the line starting
    // with 'import' around the diagnostic.
    {
        uint32_t probe = (start_line > 0U) ? (start_line - 1U) : 0U;
        uint32_t upper_bound = std::min(static_cast<uint32_t>(source.size()), end_line_limit + 1U);
        (void)upper_bound; // kept for future extension; current scan is bounded inline
        // probe for "import" keyword up to 3 lines back
        bool found = false;
        for (uint32_t l = 0; l < 3U; ++l) {
            if (probe + l >= (1U << 28))
                break;
            uint32_t line_no = probe + l;
            auto text = line_text(source, line_no);
            if (text.empty() && line_no > end_line_limit) {
                break;
            }
            auto trimmed = ltrim(text);
            if (trimmed.substr(0, 6) == "import") {
                start_line = line_no;
                found = true;
                break;
            }
            if (line_no >= end_line_limit + 2U) {
                break;
            }
        }
        if (!found) {
            // fallback: use diag.range.start.line directly
            auto text = ltrim(line_text(source, start_line));
            if (text.substr(0, 6) != "import") {
                // Bail out — we can't reliably find the statement.
                return std::nullopt;
            }
        }
    }

    // Now walk forward from start_line, looking for the end of the
    // import statement. An import statement ends with a semicolon.
    // Multiline imports are supported (e.g. import X from Y;).
    const auto start_offset = line_start_offset(source, start_line);

    std::size_t cursor = start_offset;
    std::size_t semicolon_pos = std::string::npos;
    // Also accept end-of-line if no ';' is found within 8 lines
    // (defensive fallback).
    std::size_t last_line_end = start_offset;
    uint32_t lines_seen = 0;
    while (cursor < source.size() && lines_seen < 8) {
        if (source[cursor] == ';') {
            semicolon_pos = cursor + 1; // include the ';'
            break;
        }
        if (source[cursor] == '\n') {
            ++lines_seen;
            last_line_end = cursor + 1; // include the newline
        }
        ++cursor;
    }
    (void)last_line_end; // currently unused; retained for future end-of-line fallback path

    Range range;
    range.start = Position{start_line, 0};
    if (semicolon_pos != std::string::npos) {
        // Compute line/character for semicolon_pos + 1 to include ';'
        // plus any trailing newline.
        std::size_t end = semicolon_pos;
        if (end < source.size() && source[end] == '\n') {
            ++end;
        }
        uint32_t line = start_line;
        std::size_t line_start = line_start_offset(source, start_line);
        for (std::size_t i = line_start; i < end; ++i) {
            if (source[i] == '\n') {
                ++line;
            }
        }
        std::size_t prev_nl = source.rfind('\n', end == 0 ? 0 : end - 1);
        if (prev_nl == std::string::npos) {
            prev_nl = 0;
        } else {
            ++prev_nl;
        }
        std::uint32_t ch = static_cast<std::uint32_t>(end - prev_nl);
        if (end == 0)
            ch = 0;
        range.end = Position{line, ch};
    } else {
        // Fallback: extend to end of the start line
        auto line_view = line_text(source, start_line);
        range.end = Position{start_line, static_cast<uint32_t>(line_view.size())};
    }

    // If the statement includes a trailing newline (end character == 0
    // on next line), that's fine — the edit will delete that newline.
    return range;
}

[[nodiscard]] std::optional<CodeAction> qf_unused_import(const std::string &source,
                                                         const LspDiagnostic &diag) {
    auto stmt_range = find_import_statement_range(source, diag);
    if (!stmt_range) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = *stmt_range;
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove unused import";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

// ---------------------------------------------------------------------------
// QF-3: WRONG_ARITY -> insert placeholders inside empty / underfull
//       keyword-call parens for assert/requires/unreachable/unwrap(expr).
// ---------------------------------------------------------------------------

constexpr std::array<std::string_view, 4> kWrongArityKeywords = {
    "assert",
    "requires",
    "unreachable",
    "unwrap",
};

// Placeholder text per keyword (used when the parens are empty).
[[nodiscard]] std::string_view placeholder_for_keyword(std::string_view keyword) {
    if (keyword == "assert")
        return "<cond>";
    if (keyword == "requires")
        return "<cond>";
    if (keyword == "unreachable")
        return "<TODO_message>";
    if (keyword == "unwrap")
        return "<TODO>";
    return "_TODO_";
}

// Find the keyword call containing the diagnostic position.
// Returns {keyword, open_paren_offset, close_paren_offset} if found.
struct KeywordCall {
    std::string_view keyword;
    std::size_t open_paren;  // position of '('
    std::size_t close_paren; // position of ')'
};

[[nodiscard]] std::optional<KeywordCall> find_kw_call_at(const std::string &source,
                                                         const Range &diag_range) {
    // Compute the offset of the diagnostic's start position.
    std::size_t diag_offset =
        line_start_offset(source, diag_range.start.line) + diag_range.start.character;

    // Walk backward from diag_offset to find the nearest keyword whose
    // '(' comes before/at diag_offset and whose ')' is ahead.
    // Strategy: scan backward looking for each kWrongArityKeywords
    // followed immediately by '(' (ignoring whitespace? AHFL has no
    // space between keyword and '(' for these forms).
    std::size_t cursor = (diag_offset < source.size()) ? diag_offset : source.size();
    if (cursor > 0 && !source.empty()) {
        // Start just before the diagnostic range to cover cases where
        // the range starts exactly at the keyword name.
        std::size_t search_end = std::min(cursor + diag_range.end.character, source.size());
        std::size_t search_start = (cursor > 40) ? (cursor - 40) : 0;

        for (const auto kw : kWrongArityKeywords) {
            std::size_t pos = source.rfind(kw, search_end);
            while (pos != std::string::npos && pos >= search_start) {
                // Ensure full word match (prev char is not identifier)
                bool prev_ok =
                    (pos == 0) || (!std::isalpha(static_cast<unsigned char>(source[pos - 1])) &&
                                   source[pos - 1] != '_');
                std::size_t after = pos + kw.size();
                bool next_ok =
                    (after < source.size()) &&
                    (source[after] == '(' || source[after] == ' ' || source[after] == '\t');
                if (prev_ok && next_ok) {
                    // Find the open paren (possibly with whitespace)
                    std::size_t op = after;
                    while (op < source.size() && (source[op] == ' ' || source[op] == '\t'))
                        ++op;
                    if (op < source.size() && source[op] == '(') {
                        // Find matching close paren
                        int depth = 1;
                        std::size_t cp = op + 1;
                        while (cp < source.size() && depth > 0) {
                            if (source[cp] == '(')
                                ++depth;
                            else if (source[cp] == ')') {
                                --depth;
                                if (depth == 0)
                                    break;
                            }
                            // Skip string literals (very rough)
                            else if (source[cp] == '"') {
                                ++cp;
                                while (cp < source.size() && source[cp] != '"') {
                                    if (source[cp] == '\\' && cp + 1 < source.size())
                                        cp += 2;
                                    else
                                        ++cp;
                                }
                            }
                            ++cp;
                        }
                        if (cp < source.size() && source[cp] == ')') {
                            return KeywordCall{kw, op, cp};
                        }
                    }
                }
                if (pos == 0)
                    break;
                pos = source.rfind(kw, pos - 1);
            }
        }
    }
    return std::nullopt;
}

// Convert a byte offset in source → LSP Position.
[[nodiscard]] Position offset_to_position(const std::string &source, std::size_t offset) {
    if (offset >= source.size()) {
        return end_of_document(source);
    }
    uint32_t line = 0;
    uint32_t last_nl = 0;
    for (std::size_t i = 0; i < offset; ++i) {
        if (source[i] == '\n') {
            ++line;
            last_nl = static_cast<uint32_t>(i + 1);
        }
    }
    return Position{line, static_cast<uint32_t>(offset - last_nl)};
}

[[nodiscard]] std::size_t position_to_offset(const std::string &source, Position position) {
    const auto start = line_start_offset(source, position.line);
    return std::min(source.size(), start + position.character);
}

[[nodiscard]] std::optional<CodeAction> qf_wrong_arity(const std::string &source,
                                                       const LspDiagnostic &diag) {
    // Only apply for the assert/requires/unreachable/unwrap family.
    auto kw_call = find_kw_call_at(source, diag.range);
    if (!kw_call) {
        return std::nullopt;
    }

    // Content between '(' and ')'.
    const std::string_view view(source);
    auto inside =
        view.substr(kw_call->open_paren + 1, kw_call->close_paren - (kw_call->open_paren + 1));
    auto trimmed_inside = ltrim(inside);
    bool empty = trimmed_inside.empty();
    // Trim trailing whitespace for comparison too.
    while (!trimmed_inside.empty() &&
           (trimmed_inside.back() == ' ' || trimmed_inside.back() == '\t' ||
            trimmed_inside.back() == '\n')) {
        trimmed_inside.remove_suffix(1);
    }
    empty = trimmed_inside.empty();

    TextEdit edit;
    edit.range.start = offset_to_position(source, kw_call->open_paren + 1);
    edit.range.end = offset_to_position(source, kw_call->close_paren);
    if (empty) {
        edit.new_text = std::string(placeholder_for_keyword(kw_call->keyword));
    } else {
        // Keep existing content and append ", <TODO_next>" — but only
        // when we can clearly see there's one argument and WRONG_ARITY
        // asks for more. For safety we simply replace with a single
        // placeholder (users can type more themselves).
        edit.new_text = std::string(placeholder_for_keyword(kw_call->keyword));
    }

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Insert placeholder for missing argument(s)";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

// ---------------------------------------------------------------------------
// RFC0011-QF: MATCH_MISSING_PATTERNS -> insert structured witness arm(s)
// ---------------------------------------------------------------------------

[[nodiscard]] bool is_identifier_char(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

[[nodiscard]] bool
is_keyword_at(const std::string &source, std::size_t offset, std::string_view keyword) {
    if (offset + keyword.size() > source.size()) {
        return false;
    }
    if (std::string_view(source).substr(offset, keyword.size()) != keyword) {
        return false;
    }
    const bool before_ok = offset == 0 || !is_identifier_char(source[offset - 1]);
    const auto after = offset + keyword.size();
    const bool after_ok = after >= source.size() || !is_identifier_char(source[after]);
    return before_ok && after_ok;
}

[[nodiscard]] std::size_t skip_trivia_forward(const std::string &source, std::size_t cursor) {
    while (cursor < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[cursor]))) {
            ++cursor;
            continue;
        }
        if (cursor + 1 < source.size() && source[cursor] == '/' && source[cursor + 1] == '/') {
            const auto nl = source.find('\n', cursor + 2);
            if (nl == std::string::npos) {
                return source.size();
            }
            cursor = nl + 1;
            continue;
        }
        if (cursor + 1 < source.size() && source[cursor] == '/' && source[cursor + 1] == '*') {
            const auto close = source.find("*/", cursor + 2);
            if (close == std::string::npos) {
                return source.size();
            }
            cursor = close + 2;
            continue;
        }
        break;
    }
    return cursor;
}

[[nodiscard]] std::optional<std::size_t> find_matching_close_brace(const std::string &source,
                                                                   std::size_t open_brace) {
    if (open_brace >= source.size() || source[open_brace] != '{') {
        return std::nullopt;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t cursor = open_brace; cursor < source.size(); ++cursor) {
        const char c = source[cursor];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (cursor + 1 < source.size() && c == '/' && source[cursor + 1] == '/') {
            const auto nl = source.find('\n', cursor + 2);
            if (nl == std::string::npos) {
                return std::nullopt;
            }
            cursor = nl;
            continue;
        }
        if (cursor + 1 < source.size() && c == '/' && source[cursor + 1] == '*') {
            const auto close = source.find("*/", cursor + 2);
            if (close == std::string::npos) {
                return std::nullopt;
            }
            cursor = close + 1;
            continue;
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0) {
                return cursor;
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> find_match_close_brace(const std::string &source,
                                                                const Range &range) {
    const auto range_start = position_to_offset(source, range.start);
    auto range_end = position_to_offset(source, range.end);
    if (range_end <= range_start || range_end > source.size()) {
        range_end = source.size();
    }

    std::size_t match_pos = source.find("match", range_start);
    while (match_pos != std::string::npos && match_pos < range_end) {
        if (!is_keyword_at(source, match_pos, "match")) {
            match_pos = source.find("match", match_pos + 1);
            continue;
        }
        const auto open = source.find('{', match_pos + std::string_view("match").size());
        if (open == std::string::npos || open >= source.size()) {
            return std::nullopt;
        }

        if (const auto close = find_matching_close_brace(source, open)) {
            return close;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::string line_indent_at_offset(const std::string &source, std::size_t offset) {
    const auto line_start = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    std::size_t cursor = line_start == std::string::npos ? 0 : line_start + 1;
    const auto indent_start = cursor;
    while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t')) {
        ++cursor;
    }
    return source.substr(indent_start, cursor - indent_start);
}

[[nodiscard]] std::string trim_copy(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

[[nodiscard]] bool source_safe_inserted_pattern_fragment(std::string_view pattern) {
    const auto trimmed = trim_copy(pattern);
    if (trimmed.empty()) {
        return false;
    }
    if (pattern.find("=>") != std::string_view::npos) {
        return false;
    }
    if (pattern.find("//") != std::string_view::npos ||
        pattern.find("/*") != std::string_view::npos) {
        return false;
    }
    return std::none_of(pattern.begin(), pattern.end(), [](char c) { return c == ';'; });
}

[[nodiscard]] bool source_safe_multiline_pattern_fragment(std::string_view pattern) {
    const auto trimmed = trim_copy(pattern);
    if (trimmed.empty()) {
        return false;
    }
    if (pattern.find("=>") != std::string_view::npos ||
        pattern.find(';') != std::string_view::npos) {
        return false;
    }
    return true;
}

[[nodiscard]] std::vector<std::string>
structured_missing_pattern_witnesses(const LspDiagnostic &diag) {
    constexpr std::size_t kMaxExactWitnessArms = 16;
    const auto found = diag.data.find("missing_witnesses");
    if (found == diag.data.end()) {
        return {};
    }
    if (found->second.size() > kMaxExactWitnessArms) {
        return {};
    }
    std::vector<std::string> witnesses;
    witnesses.reserve(found->second.size());
    for (const auto &candidate : found->second) {
        const auto witness = trim_copy(candidate);
        if (!source_safe_inserted_pattern_fragment(witness)) {
            return {};
        }
        witnesses.push_back(witness);
    }
    return witnesses;
}

[[nodiscard]] std::optional<std::string> single_diagnostic_data(const LspDiagnostic &diag,
                                                                std::string_view key) {
    const auto found = diag.data.find(std::string(key));
    if (found == diag.data.end() || found->second.size() != 1) {
        return std::nullopt;
    }
    return found->second.front();
}

[[nodiscard]] bool source_safe_identifier(std::string_view text) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(text.front());
    if (!(std::isalpha(first) || text.front() == '_')) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), is_identifier_char);
}

[[nodiscard]] std::string indent_pattern_fragment_for_arm(std::string_view pattern,
                                                          std::string_view arm_indent) {
    std::string text;
    std::size_t cursor = 0;
    bool first_line = true;
    while (cursor <= pattern.size()) {
        const auto next_newline = pattern.find('\n', cursor);
        const auto line_end =
            next_newline == std::string_view::npos ? pattern.size() : next_newline;
        auto line = pattern.substr(cursor, line_end - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!first_line) {
            text += "\n";
        }
        text += arm_indent;
        text += line;
        if (next_newline == std::string_view::npos) {
            break;
        }
        first_line = false;
        cursor = next_newline + 1;
    }
    return text;
}

[[nodiscard]] std::string missing_match_arms_text(const std::vector<std::string> &patterns,
                                                  std::string_view arm_indent,
                                                  std::string_view close_indent) {
    std::string text;
    for (const auto &pattern : patterns) {
        text += indent_pattern_fragment_for_arm(pattern, arm_indent);
        text += " => <TODO>,\n";
    }
    text += close_indent;
    return text;
}

[[nodiscard]] std::optional<CodeAction> qf_match_missing_patterns(const std::string &source,
                                                                  const LspDiagnostic &diag) {
    const auto close = find_match_close_brace(source, diag.range);
    if (!close.has_value()) {
        return std::nullopt;
    }

    const auto close_position = offset_to_position(source, *close);
    const auto close_line_start = line_start_offset(source, close_position.line);
    const auto line_prefix =
        std::string_view(source).substr(close_line_start, *close - close_line_start);
    const bool close_line_prefix_is_indent = std::all_of(
        line_prefix.begin(), line_prefix.end(), [](char c) { return c == ' ' || c == '\t'; });
    const auto close_indent = close_line_prefix_is_indent ? std::string(line_prefix)
                                                          : line_indent_at_offset(source, *close);
    const auto arm_indent = close_indent + "    ";
    const auto patterns = structured_missing_pattern_witnesses(diag);
    if (patterns.empty()) {
        return std::nullopt;
    }

    TextEdit edit;
    if (close_line_prefix_is_indent) {
        edit.range.start = offset_to_position(source, close_line_start);
        edit.range.end = close_position;
        edit.new_text = missing_match_arms_text(patterns, arm_indent, close_indent);
    } else {
        edit.range.start = close_position;
        edit.range.end = close_position;
        edit.new_text = "\n" + missing_match_arms_text(patterns, arm_indent, close_indent);
    }

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    if (patterns.size() == 1) {
        action.title = "Insert missing match arm";
    } else {
        action.title = "Insert missing match arms";
    }
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> find_struct_variant_pattern_braces(
    const std::string &source, const LspDiagnostic &diag, std::string_view variant_name) {
    const auto range_start = position_to_offset(source, diag.range.start);
    const auto range_end = position_to_offset(source, diag.range.end);
    if (range_start >= range_end || range_end > source.size() || variant_name.empty()) {
        return std::nullopt;
    }

    std::size_t variant_pos = source.find(variant_name, range_start);
    while (variant_pos != std::string::npos && variant_pos < range_end) {
        if (is_keyword_at(source, variant_pos, variant_name)) {
            break;
        }
        variant_pos = source.find(variant_name, variant_pos + 1);
    }
    if (variant_pos == std::string::npos || variant_pos >= range_end) {
        return std::nullopt;
    }

    const auto open_brace = source.find('{', variant_pos + variant_name.size());
    if (open_brace == std::string::npos || open_brace >= range_end) {
        return std::nullopt;
    }
    const auto close_brace = find_matching_close_brace(source, open_brace);
    if (!close_brace.has_value() || *close_brace > range_end) {
        return std::nullopt;
    }
    return std::pair{open_brace, *close_brace};
}

[[nodiscard]] std::optional<CodeAction> qf_missing_variant_field(const std::string &source,
                                                                 const LspDiagnostic &diag) {
    const auto variant_name = single_diagnostic_data(diag, "variant_name");
    const auto missing_field = single_diagnostic_data(diag, "missing_field");
    if (!variant_name.has_value() || !missing_field.has_value() ||
        !source_safe_identifier(*variant_name) || !source_safe_identifier(*missing_field)) {
        return std::nullopt;
    }

    const auto braces = find_struct_variant_pattern_braces(source, diag, *variant_name);
    if (!braces.has_value()) {
        return std::nullopt;
    }
    const auto [open_brace, close_brace] = *braces;
    const std::string_view content(source.data() + open_brace + 1, close_brace - open_brace - 1);

    TextEdit edit;
    if (content.find('\n') == std::string_view::npos) {
        std::size_t content_end = close_brace;
        while (content_end > open_brace + 1 &&
               (source[content_end - 1] == ' ' || source[content_end - 1] == '\t')) {
            --content_end;
        }
        const bool has_existing_field = content_end > open_brace + 1;
        edit.range.start =
            offset_to_position(source, has_existing_field ? content_end : open_brace + 1);
        edit.range.end = offset_to_position(source, close_brace);
        edit.new_text =
            has_existing_field ? ", " + *missing_field + " " : " " + *missing_field + " ";
    } else {
        const auto close_line_start_offset =
            line_start_offset(source, offset_to_position(source, close_brace).line);
        const auto close_prefix = std::string_view(source).substr(
            close_line_start_offset, close_brace - close_line_start_offset);
        if (!std::all_of(close_prefix.begin(), close_prefix.end(), [](char c) {
                return c == ' ' || c == '\t';
            })) {
            return std::nullopt;
        }

        std::size_t last_non_space = close_brace;
        while (last_non_space > open_brace + 1 &&
               std::isspace(static_cast<unsigned char>(source[last_non_space - 1]))) {
            --last_non_space;
        }
        if (last_non_space == open_brace + 1 || source[last_non_space - 1] == ',') {
            edit.range.start = offset_to_position(source, close_line_start_offset);
            edit.range.end = edit.range.start;
            edit.new_text = std::string(close_prefix) + "    " + *missing_field + ",\n";
        } else if (source[last_non_space - 1] == '{') {
            edit.range.start = offset_to_position(source, close_line_start_offset);
            edit.range.end = edit.range.start;
            edit.new_text = std::string(close_prefix) + "    " + *missing_field + ",\n";
        } else {
            const auto gap = std::string_view(source).substr(
                last_non_space, close_line_start_offset - last_non_space);
            if (!std::all_of(gap.begin(), gap.end(), [](char c) {
                    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
                })) {
                return std::nullopt;
            }
            edit.range.start = offset_to_position(source, last_non_space);
            edit.range.end = offset_to_position(source, close_line_start_offset);
            edit.new_text = ",\n" + std::string(close_prefix) + "    " + *missing_field + ",\n";
        }
    }

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Insert missing variant field `" + *missing_field + "`";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] bool field_horizontal_space(char c) noexcept {
    return c == ' ' || c == '\t';
}

[[nodiscard]] bool only_field_horizontal_space(std::string_view text) noexcept {
    return std::all_of(text.begin(), text.end(), field_horizontal_space);
}

[[nodiscard]] bool source_safe_field_tail(std::string_view text) noexcept {
    std::size_t cursor = 0;
    while (cursor < text.size() && field_horizontal_space(text[cursor])) {
        ++cursor;
    }
    if (cursor == text.size()) {
        return true;
    }
    if (text[cursor] != ',') {
        return false;
    }
    ++cursor;
    while (cursor < text.size() && field_horizontal_space(text[cursor])) {
        ++cursor;
    }
    return cursor == text.size();
}

[[nodiscard]] std::optional<Range>
unexpected_variant_field_delete_range(const std::string &source, const LspDiagnostic &diag) {
    const auto start = position_to_offset(source, diag.range.start);
    const auto end = position_to_offset(source, diag.range.end);
    if (start >= end || end > source.size()) {
        return std::nullopt;
    }
    if (!source_safe_multiline_pattern_fragment(
            std::string_view(source).substr(start, end - start))) {
        return std::nullopt;
    }

    const auto line_start = source.rfind('\n', start == 0 ? 0 : start - 1);
    const auto delete_line_start = line_start == std::string::npos ? 0 : line_start + 1;
    const auto line_end = source.find('\n', end);
    const auto delete_line_end = line_end == std::string::npos ? source.size() : line_end + 1;
    const auto suffix_end = line_end == std::string::npos ? source.size() : line_end;
    const auto prefix =
        std::string_view(source).substr(delete_line_start, start - delete_line_start);
    const auto suffix = std::string_view(source).substr(end, suffix_end - end);
    if (only_field_horizontal_space(prefix) && source_safe_field_tail(suffix)) {
        return Range{offset_to_position(source, delete_line_start),
                     offset_to_position(source, delete_line_end)};
    }

    std::size_t left = start;
    while (left > 0 && field_horizontal_space(source[left - 1])) {
        --left;
    }
    if (left > 0 && source[left - 1] == ',') {
        std::size_t delete_end = end;
        while (delete_end < source.size() && field_horizontal_space(source[delete_end])) {
            ++delete_end;
        }
        return Range{offset_to_position(source, left - 1), offset_to_position(source, delete_end)};
    }

    std::size_t right = end;
    while (right < source.size() && field_horizontal_space(source[right])) {
        ++right;
    }
    if (right < source.size() && source[right] == ',') {
        ++right;
        while (right < source.size() && field_horizontal_space(source[right])) {
            ++right;
        }
        return Range{offset_to_position(source, start), offset_to_position(source, right)};
    }

    while (right < source.size() && field_horizontal_space(source[right])) {
        ++right;
    }
    return Range{offset_to_position(source, left), offset_to_position(source, right)};
}

[[nodiscard]] std::optional<CodeAction> qf_unexpected_variant_field(const std::string &source,
                                                                    const LspDiagnostic &diag) {
    const auto context = single_diagnostic_data(diag, "variant_field_context");
    const auto unexpected_field = single_diagnostic_data(diag, "unexpected_field");
    if (!context.has_value() || *context != "pattern" || !unexpected_field.has_value() ||
        !source_safe_identifier(*unexpected_field)) {
        return std::nullopt;
    }

    const auto delete_range = unexpected_variant_field_delete_range(source, diag);
    if (!delete_range.has_value()) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = *delete_range;
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove unexpected variant field `" + *unexpected_field + "`";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] std::optional<CodeAction> qf_duplicate_variant_field(const std::string &source,
                                                                   const LspDiagnostic &diag) {
    const auto context = single_diagnostic_data(diag, "variant_field_context");
    const auto duplicate_field = single_diagnostic_data(diag, "duplicate_field");
    if (!context.has_value() || *context != "pattern" || !duplicate_field.has_value() ||
        !source_safe_identifier(*duplicate_field)) {
        return std::nullopt;
    }

    const auto delete_range = unexpected_variant_field_delete_range(source, diag);
    if (!delete_range.has_value()) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = *delete_range;
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove duplicate variant field `" + *duplicate_field + "`";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] std::optional<CodeAction> qf_match_unreachable_arm(const std::string &source,
                                                                 const LspDiagnostic &diag) {
    const auto arm_line = diag.range.start.line;
    const auto line = line_text(source, arm_line);
    if (line.empty()) {
        return std::nullopt;
    }

    const auto trimmed = ltrim(line);
    if (trimmed.empty() || trimmed.front() == '}') {
        return std::nullopt;
    }

    const auto start = line_start_offset(source, arm_line);
    const std::string_view view(source);
    std::size_t cursor = start;
    std::optional<std::size_t> arrow;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (; cursor < source.size(); ++cursor) {
        const char c = source[cursor];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '(') {
            ++paren_depth;
        } else if (c == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}' && brace_depth > 0) {
            --brace_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']' && bracket_depth > 0) {
            --bracket_depth;
        }

        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0 &&
            cursor + 1 < source.size() && view.substr(cursor, 2) == "=>") {
            arrow = cursor;
            cursor += 2;
            break;
        }
    }
    if (!arrow.has_value()) {
        return std::nullopt;
    }

    std::size_t end = std::string::npos;
    paren_depth = 0;
    brace_depth = 0;
    bracket_depth = 0;
    in_string = false;
    escaped = false;
    for (; cursor < source.size(); ++cursor) {
        const char c = source[cursor];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
            continue;
        }
        if (c == '(') {
            ++paren_depth;
        } else if (c == ')' && paren_depth > 0) {
            --paren_depth;
        } else if (c == '{') {
            ++brace_depth;
        } else if (c == '}' && brace_depth > 0) {
            --brace_depth;
        } else if (c == '[') {
            ++bracket_depth;
        } else if (c == ']' && bracket_depth > 0) {
            --bracket_depth;
        }

        if (paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            if (c == ',') {
                end = cursor + 1;
                if (end < source.size() && source[end] == '\n') {
                    ++end;
                }
                break;
            }
            if (c == '}') {
                end = cursor;
                break;
            }
        }
    }
    if (end == std::string::npos || end <= start) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range.start = offset_to_position(source, start);
    edit.range.end = offset_to_position(source, end);
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove unreachable match arm";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] bool horizontal_space(char c) noexcept {
    return c == ' ' || c == '\t';
}

[[nodiscard]] bool pattern_separator_space(char c) noexcept {
    return horizontal_space(c) || c == '\n' || c == '\r';
}

[[nodiscard]] bool prefix_since_line_start_is_indent(const std::string &source,
                                                     std::size_t offset) {
    const auto previous_newline = source.rfind('\n', offset == 0 ? 0 : offset - 1);
    const auto line_start = previous_newline == std::string::npos ? 0 : previous_newline + 1;
    return std::all_of(source.begin() + static_cast<std::ptrdiff_t>(line_start),
                       source.begin() + static_cast<std::ptrdiff_t>(offset),
                       horizontal_space);
}

[[nodiscard]] std::size_t maybe_include_deleted_line_newline(const std::string &source,
                                                             std::size_t delete_start,
                                                             std::size_t delete_end) {
    if (delete_end < source.size() && source[delete_end] == '\n' &&
        prefix_since_line_start_is_indent(source, delete_start)) {
        return delete_end + 1;
    }
    return delete_end;
}

[[nodiscard]] std::optional<Range> find_redundant_or_branch_range(const std::string &source,
                                                                  const LspDiagnostic &diag) {
    const auto start = position_to_offset(source, diag.range.start);
    const auto end = position_to_offset(source, diag.range.end);
    if (start >= end || end > source.size()) {
        return std::nullopt;
    }
    if (!source_safe_multiline_pattern_fragment(
            std::string_view(source).substr(start, end - start))) {
        return std::nullopt;
    }

    std::size_t left = start;
    while (left > 0 && pattern_separator_space(source[left - 1])) {
        --left;
    }
    if (left > 0 && source[left - 1] == '|') {
        std::size_t delete_start = left - 1;
        const auto delete_line_start =
            line_start_offset(source, offset_to_position(source, delete_start).line);
        while (delete_start > delete_line_start && horizontal_space(source[delete_start - 1])) {
            --delete_start;
        }
        const auto delete_end = maybe_include_deleted_line_newline(source, delete_start, end);
        return Range{offset_to_position(source, delete_start),
                     offset_to_position(source, delete_end)};
    }

    std::size_t right = end;
    while (right < source.size() && pattern_separator_space(source[right])) {
        ++right;
    }
    if (right < source.size() && source[right] == '|') {
        std::size_t delete_end = right + 1;
        while (delete_end < source.size() && pattern_separator_space(source[delete_end])) {
            ++delete_end;
        }
        return Range{offset_to_position(source, start), offset_to_position(source, delete_end)};
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<CodeAction> qf_match_redundant_pattern(const std::string &source,
                                                                   const LspDiagnostic &diag) {
    auto branch_range = find_redundant_or_branch_range(source, diag);
    if (!branch_range.has_value()) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = *branch_range;
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove redundant pattern branch";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] std::optional<Range> find_if_let_else_branch_range(const std::string &source,
                                                                 const LspDiagnostic &diag) {
    const auto diag_start = position_to_offset(source, diag.range.start);
    const auto diag_end = position_to_offset(source, diag.range.end);
    if (diag_start >= source.size() || diag_end < diag_start) {
        return std::nullopt;
    }

    std::size_t open_brace = skip_trivia_forward(source, diag_start);
    if (open_brace >= source.size() || source[open_brace] != '{') {
        const auto forward_open = source.find('{', diag_start);
        if (forward_open != std::string::npos && forward_open <= diag_end) {
            open_brace = forward_open;
        } else {
            const auto backward_open =
                diag_start == 0 ? std::string::npos : source.rfind('{', diag_start - 1);
            if (backward_open == std::string::npos) {
                return std::nullopt;
            }
            const auto close = find_matching_close_brace(source, backward_open);
            if (!close.has_value() || *close < diag_end) {
                return std::nullopt;
            }
            open_brace = backward_open;
        }
    }

    std::optional<std::size_t> else_keyword;
    std::size_t search = open_brace;
    while (search > 0) {
        const auto candidate = source.rfind("else", search - 1);
        if (candidate == std::string::npos) {
            break;
        }
        if (is_keyword_at(source, candidate, "else") &&
            skip_trivia_forward(source, candidate + std::string_view("else").size()) ==
                open_brace) {
            else_keyword = candidate;
            break;
        }
        search = candidate;
    }
    if (!else_keyword.has_value()) {
        return std::nullopt;
    }

    const auto close_brace = find_matching_close_brace(source, open_brace);
    if (!close_brace.has_value()) {
        return std::nullopt;
    }

    const auto line_start = source.rfind('\n', *else_keyword == 0 ? 0 : *else_keyword - 1);
    const auto else_line_start = line_start == std::string::npos ? 0 : line_start + 1;
    std::size_t delete_start = *else_keyword;
    const auto prefix =
        std::string_view(source).substr(else_line_start, *else_keyword - else_line_start);
    const bool else_starts_line =
        std::all_of(prefix.begin(), prefix.end(), [](char c) { return c == ' ' || c == '\t'; });
    if (else_starts_line) {
        delete_start = else_line_start;
    } else {
        while (delete_start > else_line_start &&
               (source[delete_start - 1] == ' ' || source[delete_start - 1] == '\t')) {
            --delete_start;
        }
    }

    std::size_t delete_end = *close_brace + 1;
    if (delete_end < source.size() && source[delete_end] == '\n') {
        ++delete_end;
    }
    if (delete_end <= delete_start) {
        return std::nullopt;
    }
    return Range{offset_to_position(source, delete_start), offset_to_position(source, delete_end)};
}

[[nodiscard]] std::optional<CodeAction> qf_unreachable_if_let_else(const std::string &source,
                                                                   const LspDiagnostic &diag) {
    auto else_range = find_if_let_else_branch_range(source, diag);
    if (!else_range.has_value()) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = *else_range;
    edit.new_text = "";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Remove unreachable if-let else branch";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

[[nodiscard]] bool source_safe_signed_int_bound(std::string_view bound) noexcept {
    if (bound.empty()) {
        return false;
    }
    std::size_t cursor = 0;
    if (bound.front() == '-') {
        cursor = 1;
        if (cursor == bound.size()) {
            return false;
        }
    }
    return std::all_of(bound.begin() + static_cast<std::ptrdiff_t>(cursor),
                       bound.end(),
                       [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
}

[[nodiscard]] std::optional<CodeAction> qf_invalid_range_pattern(const std::string &source,
                                                                 const LspDiagnostic &diag) {
    const auto start_bound = single_diagnostic_data(diag, "range_start");
    const auto end_bound = single_diagnostic_data(diag, "range_end");
    if (!start_bound.has_value() || !end_bound.has_value() ||
        !source_safe_signed_int_bound(*start_bound) || !source_safe_signed_int_bound(*end_bound)) {
        return std::nullopt;
    }

    const auto range_start = position_to_offset(source, diag.range.start);
    const auto range_end = position_to_offset(source, diag.range.end);
    if (range_start >= range_end || range_end > source.size()) {
        return std::nullopt;
    }

    const std::string original = *start_bound + ".." + *end_bound;
    if (trim_copy(std::string_view(source).substr(range_start, range_end - range_start)) !=
        original) {
        return std::nullopt;
    }

    TextEdit edit;
    edit.range = diag.range;
    edit.new_text = *end_bound + ".." + *start_bound;

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Swap range pattern bounds";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

// ---------------------------------------------------------------------------
// Organize Imports (existing, preserved)
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<CodeAction> make_organize_imports_action(const std::string &source) {
    formatter::FormatOptions options;
    options.sort_imports = true;

    auto result = formatter::format_source(source, options);
    if (!result.success || result.formatted == source) {
        return std::nullopt;
    }

    TextEdit full_edit;
    full_edit.range.start = Position{0, 0};
    full_edit.range.end = end_of_document(source);
    full_edit.new_text = result.formatted;

    WorkspaceEdit edit;
    edit.changes.emplace("", std::vector<TextEdit>{std::move(full_edit)});

    CodeAction action;
    action.title = "Organize Imports";
    action.kind = CodeActionKind::SourceOrganizeImports;
    action.is_preferred = false;
    action.edit = std::move(edit);

    return action;
}

// ---------------------------------------------------------------------------
// QF-4: AGENT_CONTEXT_OMITTED → insert "context: struct { };" after input.
// ---------------------------------------------------------------------------

// Grammar (AHFL.g4 agentDecl):
//   'agent' IDENT '{' inputDecl contextDecl? outputDecl statesDecl initialDecl
//       finalDecl capabilitiesDecl? quotaDecl? transitionDecl* '}'
//
// Diagnostic range covers the whole agent block. We scan inside this range
// for the `;` that closes inputDecl, then walk forward to find `output:` and
// insert a `context: struct { };` line between the two, preserving indent.
[[nodiscard]] std::optional<CodeAction> qf_agent_context_omitted(const std::string &source,
                                                                 const LspDiagnostic &diag) {
    const std::size_t block_start =
        line_start_offset(source, diag.range.start.line) + diag.range.start.character;
    const std::size_t block_end =
        std::min(source.size(),
                 line_start_offset(source, diag.range.end.line) + diag.range.end.character + 1);
    if (block_start >= block_end || block_end > source.size()) {
        return std::nullopt;
    }

    // 1. Find the `input:` keyword inside this block.
    const auto input_pos = source.find("input:", block_start);
    if (input_pos == std::string::npos || input_pos >= block_end) {
        return std::nullopt;
    }
    // 2. Find the first `;` after `input:` — that closes the input declaration.
    const auto input_semi = source.find(';', input_pos);
    if (input_semi == std::string::npos || input_semi >= block_end) {
        return std::nullopt;
    }
    // 3. Find the next newline after `;` so we can insert a whole new line.
    const auto after_nl = source.find('\n', input_semi);
    if (after_nl == std::string::npos || after_nl + 1 >= block_end) {
        return std::nullopt;
    }
    // 4. Determine indentation by peeking at the next non-empty line
    //    (which should be `output:` or the next schema keyword).
    std::size_t indent_start = after_nl + 1;
    while (indent_start < block_end &&
           (source[indent_start] == ' ' || source[indent_start] == '\t')) {
        ++indent_start;
    }
    const std::size_t indent_len = indent_start - (after_nl + 1);
    const std::string indent(source, after_nl + 1, indent_len);

    // Insert position = right after the newline after input semicolon.
    const auto insert_pos = after_nl + 1;
    TextEdit edit;
    edit.range.start = offset_to_position(source, insert_pos);
    edit.range.end = edit.range.start; // pure insert
    edit.new_text = indent + "context: struct { };\n";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Insert `context: struct { };` clause";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

// ---------------------------------------------------------------------------
// QF-5: AGENT_CAPABILITIES_OMITTED → insert "capabilities: [];" before first
//       transition or closing `}`.
// ---------------------------------------------------------------------------

[[nodiscard]] std::optional<CodeAction> qf_agent_capabilities_omitted(const std::string &source,
                                                                      const LspDiagnostic &diag) {
    const std::size_t block_start =
        line_start_offset(source, diag.range.start.line) + diag.range.start.character;
    const std::size_t block_end =
        std::min(source.size(),
                 line_start_offset(source, diag.range.end.line) + diag.range.end.character + 1);
    if (block_start >= block_end || block_end > source.size()) {
        return std::nullopt;
    }

    // 1. Find end of `final:` clause (first `;` after "final:" within block).
    const auto final_pos = source.find("final:", block_start);
    std::size_t anchor = block_end;
    if (final_pos != std::string::npos && final_pos < block_end) {
        const auto semi = source.find(';', final_pos);
        if (semi != std::string::npos && semi < block_end) {
            anchor = semi;
        }
    }
    if (anchor >= block_end) {
        // Fallback: find `states:` keyword — must always exist (grammar REQUIRED).
        const auto states_pos = source.find("states:", block_start);
        if (states_pos == std::string::npos || states_pos >= block_end) {
            return std::nullopt;
        }
        anchor = states_pos;
    }

    // 2. Find insertion point: line-break AFTER anchor's end-of-line, BEFORE any
    //    `transition` keyword, `quota` keyword, or closing `}`.
    const auto anchor_nl = source.find('\n', anchor);
    if (anchor_nl == std::string::npos || anchor_nl + 1 >= block_end) {
        return std::nullopt;
    }

    // Now find the next line that is not the `quota:` keyword (optional, would
    // be a sibling of capabilities in grammar). We want to land before either
    // `quota:` / `transition` / `}`.
    std::size_t scan = anchor_nl + 1;
    while (scan < block_end) {
        const auto line_start = scan;
        auto trimmed = ltrim(line_text_from(source, line_start));
        if (trimmed.empty()) {
            // Skip blank lines, keep scanning.
            const auto nl = source.find('\n', scan);
            if (nl == std::string::npos || nl >= block_end)
                break;
            scan = nl + 1;
            continue;
        }
        if (trimmed.substr(0, 9) == "quota:" || trimmed.substr(0, 11) == "transition" ||
            trimmed[0] == '}') {
            break; // insert BEFORE this line
        }
        // Otherwise step past.
        const auto nl = source.find('\n', scan);
        if (nl == std::string::npos || nl >= block_end)
            break;
        scan = nl + 1;
    }

    // Determine indentation of the line we are about to land before.
    std::size_t indent_check = scan;
    while (indent_check < block_end &&
           (source[indent_check] == ' ' || source[indent_check] == '\t')) {
        ++indent_check;
    }
    std::size_t indent_len = indent_check - scan;
    if (indent_len == 0) {
        // Fallback: 4 spaces (the project default formatter indent).
        indent_len = 4;
    }
    const std::string indent(source, scan, indent_len);

    TextEdit edit;
    edit.range.start = offset_to_position(source, scan);
    edit.range.end = edit.range.start; // pure insert
    edit.new_text = indent + "capabilities: [];\n";

    WorkspaceEdit ws_edit;
    ws_edit.changes.emplace("", std::vector<TextEdit>{std::move(edit)});

    CodeAction action;
    action.title = "Insert empty `capabilities: [];` clause";
    action.kind = CodeActionKind::QuickFix;
    action.is_preferred = true;
    action.diagnostics = {diag};
    action.edit = std::move(ws_edit);
    return action;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::vector<CodeAction> compute_code_actions(const std::string &source,
                                             const Range &range,
                                             const std::vector<LspDiagnostic> &diagnostics) {
    std::vector<CodeAction> actions;

    // Source-level actions first.
    if (range_intersects_imports(source, range)) {
        if (auto organize = make_organize_imports_action(source)) {
            actions.push_back(std::move(*organize));
        }
    }

    // Diagnostic-driven quick fixes.
    for (const auto &diag : diagnostics) {
        if (diag.code == kCodeDuplicateStructName) {
            auto more = qf_duplicate_struct(diag);
            for (auto &a : more)
                actions.push_back(std::move(a));
        } else if (diag.code == kCodeUnusedImport) {
            if (auto a = qf_unused_import(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeWrongArity) {
            if (auto a = qf_wrong_arity(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeMatchMissingPatterns) {
            if (auto a = qf_match_missing_patterns(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeMatchUnreachableArm) {
            if (auto a = qf_match_unreachable_arm(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeMatchRedundantPattern) {
            if (auto a = qf_match_redundant_pattern(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeUnreachableIfLetElse) {
            if (auto a = qf_unreachable_if_let_else(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeInvalidRangePattern) {
            if (auto a = qf_invalid_range_pattern(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeMissingVariantField) {
            if (auto a = qf_missing_variant_field(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeUnexpectedVariantField) {
            if (auto a = qf_unexpected_variant_field(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeDuplicateVariantField) {
            if (auto a = qf_duplicate_variant_field(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeAgentContextOmitted) {
            if (auto a = qf_agent_context_omitted(source, diag)) {
                actions.push_back(std::move(*a));
            }
        } else if (diag.code == kCodeAgentCapabilitiesOmitted) {
            if (auto a = qf_agent_capabilities_omitted(source, diag)) {
                actions.push_back(std::move(*a));
            }
        }
    }

    return actions;
}

} // namespace ahfl::lsp
