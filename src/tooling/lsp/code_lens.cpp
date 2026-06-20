#include "tooling/lsp/code_lens.hpp"

#include <cctype>
#include <string_view>

namespace ahfl::lsp {

namespace {

/// Skip leading whitespace and return the remaining view.
[[nodiscard]] std::string_view skip_whitespace(std::string_view line) {
    std::size_t i = 0;
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    return line.substr(i);
}

/// Check if line starts with a given keyword followed by whitespace or punctuation
/// that would end an identifier.
[[nodiscard]] bool starts_with_keyword(std::string_view line, std::string_view keyword) {
    if (line.size() < keyword.size()) {
        return false;
    }
    if (line.substr(0, keyword.size()) != keyword) {
        return false;
    }
    if (line.size() == keyword.size()) {
        return true;
    }
    const char next = line[keyword.size()];
    return std::isspace(static_cast<unsigned char>(next)) || next == '(';
}

/// Extract the first identifier after a keyword on the same line.
/// Returns empty if none found.
[[nodiscard]] std::string_view extract_name_after(std::string_view line,
                                                  std::string_view keyword) {
    auto after = line.substr(keyword.size());
    std::size_t i = 0;
    while (i < after.size() && std::isspace(static_cast<unsigned char>(after[i]))) {
        ++i;
    }
    if (i >= after.size()) {
        return {};
    }
    std::size_t start = i;
    while (i < after.size() &&
           (std::isalnum(static_cast<unsigned char>(after[i])) || after[i] == '_')) {
        ++i;
    }
    if (i == start) {
        return {};
    }
    return after.substr(start, i - start);
}

/// Update brace depth and return whether the line contains a top-level opening brace.
void update_brace_depth(std::string_view line, int &depth, bool &in_block_comment) {
    std::size_t i = 0;
    while (i < line.size()) {
        if (in_block_comment) {
            if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                in_block_comment = false;
                i += 2;
            } else {
                ++i;
            }
            continue;
        }

        if (line[i] == '/' && i + 1 < line.size()) {
            if (line[i + 1] == '/') {
                // Rest of line is a comment
                break;
            }
            if (line[i + 1] == '*') {
                in_block_comment = true;
                i += 2;
                continue;
            }
        }

        if (line[i] == '{') {
            ++depth;
        } else if (line[i] == '}') {
            --depth;
        }
        ++i;
    }
}

} // namespace

std::vector<CodeLens> compute_code_lens(const std::string &source) {
    std::vector<CodeLens> result;

    // Keywords that introduce top-level declarations we care about.
    // Each entry: (keyword, display_label)
    constexpr std::pair<std::string_view, std::string_view> kKeywords[] = {
        {"struct", "struct"},
        {"enum", "enum"},
        {"capability", "capability"},
        {"agent", "agent"},
        {"workflow", "workflow"},
        {"predicate", "predicate"},
        {"const", "const"},
        {"type", "type"},
    };

    int brace_depth = 0;
    bool in_block_comment = false;
    std::size_t line_start = 0;
    uint32_t line_number = 0;

    const std::string_view src{source};

    while (line_start <= src.size()) {
        std::size_t line_end = src.find('\n', line_start);
        if (line_end == std::string_view::npos) {
            line_end = src.size();
        }

        std::string_view full_line = src.substr(line_start, line_end - line_start);
        // Strip trailing \r if present
        std::string_view line = full_line;
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }

        const auto stripped = skip_whitespace(line);

        // Track comment state and brace depth.
        // We first check if this line starts with a declaration keyword at top-level,
        // then update brace depth for subsequent lines.
        const bool at_top_level = brace_depth == 0 && !in_block_comment;
        const bool is_line_comment = stripped.substr(0, 2) == "//";
        const bool starts_block_comment = stripped.substr(0, 2) == "/*";

        bool found_decl = false;
        if (at_top_level && !is_line_comment && !starts_block_comment && !stripped.empty()) {
            for (const auto &[keyword, label] : kKeywords) {
                if (starts_with_keyword(stripped, keyword)) {
                    const auto name = extract_name_after(stripped, keyword);

                    CodeLens lens;
                    lens.range.start.line = line_number;
                    lens.range.start.character = static_cast<uint32_t>(
                        stripped.data() - line.data());
                    lens.range.end.line = line_number;
                    lens.range.end.character = static_cast<uint32_t>(line.size());

                    std::string title;
                    title.reserve(label.size() + 1 + name.size());
                    title.append(label);
                    if (!name.empty()) {
                        title.push_back(' ');
                        title.append(name);
                    }
                    lens.command_title = std::move(title);
                    lens.command = "ahfl.showReferences";
                    if (!name.empty()) {
                        lens.command_arguments.push_back(std::string{name});
                    }

                    result.push_back(std::move(lens));
                    found_decl = true;
                    break;
                }
            }
        }

        (void)found_decl;

        // Update brace depth and comment state for the next line.
        update_brace_depth(line, brace_depth, in_block_comment);

        if (line_end == src.size()) {
            break;
        }
        line_start = line_end + 1;
        ++line_number;
    }

    return result;
}

} // namespace ahfl::lsp
