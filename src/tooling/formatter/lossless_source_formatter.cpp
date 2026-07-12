#include "tooling/formatter/lossless_source_formatter.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <vector>

namespace ahfl::formatter {

namespace {

struct LexicalState final {
    bool in_block_comment{false};
};

struct LineShape final {
    int opening_braces{0};
    int closing_braces{0};
    int opening_delimiters{0};
    int closing_delimiters{0};
    int leading_closing_delimiters{0};
    bool starts_with_closing_brace{false};
};

[[nodiscard]] std::string_view trim_left(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    return line;
}

[[nodiscard]] std::string_view trim(std::string_view line) {
    line = trim_left(line);
    while (!line.empty() &&
           (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

[[nodiscard]] std::string indentation(int level, const FormatOptions &options) {
    if (level <= 0) {
        return {};
    }
    if (options.use_tabs) {
        return std::string(static_cast<std::size_t>(level), '\t');
    }
    return std::string(static_cast<std::size_t>(level * options.indent_width), ' ');
}

[[nodiscard]] LineShape scan_line(std::string_view line, LexicalState &state) {
    LineShape shape;
    bool in_string = false;
    bool escaped = false;
    bool saw_code = false;
    bool saw_non_closing_delimiter = false;

    for (std::size_t index = 0; index < line.size(); ++index) {
        const char current = line[index];
        const char next = index + 1 < line.size() ? line[index + 1] : '\0';

        if (state.in_block_comment) {
            if (current == '*' && next == '/') {
                state.in_block_comment = false;
                ++index;
            }
            continue;
        }

        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (current == '\\') {
                escaped = true;
            } else if (current == '"') {
                in_string = false;
            }
            continue;
        }

        if (current == '/' && next == '/') {
            break;
        }
        if (current == '/' && next == '*') {
            state.in_block_comment = true;
            ++index;
            continue;
        }
        if (current == '"') {
            in_string = true;
            saw_code = true;
            continue;
        }
        if (current == ' ' || current == '\t' || current == '\r') {
            continue;
        }

        if (!saw_code) {
            shape.starts_with_closing_brace = current == '}';
            saw_code = true;
        }
        if (current == '{') {
            ++shape.opening_braces;
        } else if (current == '}') {
            ++shape.closing_braces;
        } else if (current == '(' || current == '[') {
            ++shape.opening_delimiters;
            saw_non_closing_delimiter = true;
        } else if (current == ')' || current == ']') {
            ++shape.closing_delimiters;
            if (!saw_non_closing_delimiter) {
                ++shape.leading_closing_delimiters;
            }
        } else {
            saw_non_closing_delimiter = true;
        }
    }

    return shape;
}

[[nodiscard]] bool is_continuation_clause(std::string_view line) {
    const auto text = trim(line);
    return text.starts_with("-> ") || text.starts_with("decreases ") || text == "where" ||
           text.starts_with("where ");
}

[[nodiscard]] bool is_import_line(std::string_view line) {
    const auto text = trim(line);
    return text.starts_with("import ") && text.ends_with(';');
}

void sort_contiguous_imports(std::vector<std::string> &lines) {
    std::size_t begin = 0;
    while (begin < lines.size()) {
        if (!is_import_line(lines[begin])) {
            ++begin;
            continue;
        }

        std::size_t end = begin + 1;
        while (end < lines.size() && is_import_line(lines[end])) {
            ++end;
        }
        std::sort(lines.begin() + static_cast<std::ptrdiff_t>(begin),
                  lines.begin() + static_cast<std::ptrdiff_t>(end),
                  [](const std::string &lhs, const std::string &rhs) {
                      return trim(lhs) < trim(rhs);
                  });
        begin = end;
    }
}

[[nodiscard]] std::vector<std::string> split_lines(const std::string &source) {
    std::vector<std::string> lines;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace

FormatResult format_lossless_source(const std::string &source, const FormatOptions &options) {
    FormatResult result;
    if (source.empty()) {
        result.success = true;
        result.formatted = options.trailing_newline ? "\n" : "";
        return result;
    }

    auto lines = split_lines(source);
    if (options.sort_imports) {
        sort_contiguous_imports(lines);
    }

    std::ostringstream output;
    LexicalState lexical_state;
    int structural_depth = 0;
    int delimiter_depth = 0;

    for (const auto &line : lines) {
        const auto content = trim(line);
        if (content.empty()) {
            output << '\n';
            continue;
        }

        const auto shape = scan_line(content, lexical_state);
        const int brace_depth =
            shape.starts_with_closing_brace ? std::max(0, structural_depth - 1)
                                           : structural_depth;
        int continuation_depth =
            std::max(0, delimiter_depth - shape.leading_closing_delimiters);
        if (continuation_depth == 0 && is_continuation_clause(content)) {
            continuation_depth = 1;
        }
        const int line_depth = brace_depth + continuation_depth;
        output << indentation(line_depth, options) << content << '\n';
        structural_depth =
            std::max(0, structural_depth + shape.opening_braces - shape.closing_braces);
        delimiter_depth = std::max(
            0,
            delimiter_depth + shape.opening_delimiters - shape.closing_delimiters);
    }

    result.formatted = output.str();
    if (!options.trailing_newline && !result.formatted.empty() &&
        result.formatted.back() == '\n') {
        result.formatted.pop_back();
    }
    result.success = true;
    return result;
}

} // namespace ahfl::formatter
