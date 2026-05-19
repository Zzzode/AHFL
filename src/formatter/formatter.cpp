#include "ahfl/formatter/formatter.hpp"
#include <sstream>
#include <algorithm>

namespace ahfl::formatter {

namespace {

std::string trim_right(const std::string& s) {
    auto end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

std::string make_indent(int level, const FormatOptions& opts) {
    if (opts.use_tabs) {
        return std::string(static_cast<size_t>(level), '\t');
    }
    return std::string(static_cast<size_t>(level * opts.indent_width), ' ');
}

} // anonymous namespace

FormatResult format_source(const std::string& source, const FormatOptions& options) {
    FormatResult result;

    if (source.empty()) {
        result.success = true;
        result.formatted = options.trailing_newline ? "\n" : "";
        return result;
    }

    std::istringstream iss(source);
    std::ostringstream oss;
    std::string line;
    int indent_level = 0;
    int lines_changed = 0;
    std::vector<std::string> original_lines;
    std::vector<std::string> formatted_lines;

    while (std::getline(iss, line)) {
        original_lines.push_back(line);

        // Trim the line
        auto trimmed = trim_right(line);
        auto content_start = trimmed.find_first_not_of(" \t");
        std::string content = (content_start == std::string::npos) ? "" : trimmed.substr(content_start);

        // Decrease indent for closing braces
        if (!content.empty() && content[0] == '}') {
            indent_level = std::max(0, indent_level - 1);
        }

        // Format the line
        std::string formatted_line;
        if (content.empty()) {
            formatted_line = "";
        } else {
            formatted_line = make_indent(indent_level, options) + content;
        }
        formatted_lines.push_back(formatted_line);

        // Count opening and closing braces to track indent
        int opens = static_cast<int>(std::count(content.begin(), content.end(), '{'));
        int closes = static_cast<int>(std::count(content.begin(), content.end(), '}'));

        // We already decremented for leading }, so only count net opens
        if (!content.empty() && content[0] == '}') {
            // Already handled the first close above; count remaining
            indent_level += opens - (closes - 1);
        } else {
            indent_level += opens - closes;
        }
        indent_level = std::max(0, indent_level);
    }

    // Build output
    for (size_t i = 0; i < formatted_lines.size(); ++i) {
        oss << formatted_lines[i];
        if (i + 1 < formatted_lines.size()) {
            oss << "\n";
        }
    }

    // Count changes
    for (size_t i = 0; i < original_lines.size() && i < formatted_lines.size(); ++i) {
        if (original_lines[i] != formatted_lines[i]) {
            lines_changed++;
        }
    }

    result.formatted = oss.str();
    if (options.trailing_newline && !result.formatted.empty() && result.formatted.back() != '\n') {
        result.formatted += "\n";
    }
    result.lines_changed = lines_changed;
    result.success = true;
    return result;
}

bool check_formatting(const std::string& source, const FormatOptions& options) {
    auto result = format_source(source, options);
    if (!result.success) return false;
    return result.formatted == source;
}

std::vector<FormatDiff> compute_diff(const std::string& original, const std::string& formatted) {
    std::vector<FormatDiff> diffs;

    std::istringstream iss_orig(original);
    std::istringstream iss_fmt(formatted);
    std::string orig_line;
    std::string fmt_line;
    int line_num = 1;

    bool have_orig = true;
    bool have_fmt = true;

    while (have_orig || have_fmt) {
        have_orig = static_cast<bool>(std::getline(iss_orig, orig_line));
        have_fmt = static_cast<bool>(std::getline(iss_fmt, fmt_line));

        if (!have_orig && !have_fmt) break;

        std::string ol = have_orig ? orig_line : "";
        std::string fl = have_fmt ? fmt_line : "";

        if (ol != fl) {
            diffs.push_back({line_num, ol, fl});
        }

        orig_line.clear();
        fmt_line.clear();
        line_num++;
    }

    return diffs;
}

} // namespace ahfl::formatter
