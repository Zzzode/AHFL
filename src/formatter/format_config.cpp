#include "ahfl/formatter/format_config.hpp"
#include <sstream>
#include <fstream>

namespace ahfl::formatter {

std::optional<FormatOptions> load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    FormatOptions opts = default_options();
    std::string line;

    while (std::getline(file, line)) {
        // Simple key=value parsing
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        auto key = line.substr(0, eq);
        auto value = line.substr(eq + 1);

        // Trim
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value = value.substr(1);

        if (key == "indent_width") {
            opts.indent_width = std::stoi(value);
        } else if (key == "use_tabs") {
            opts.use_tabs = (value == "true" || value == "1");
        } else if (key == "max_line_length") {
            opts.max_line_length = std::stoi(value);
        } else if (key == "trailing_newline") {
            opts.trailing_newline = (value == "true" || value == "1");
        } else if (key == "align_fields") {
            opts.align_fields = (value == "true" || value == "1");
        } else if (key == "sort_imports") {
            opts.sort_imports = (value == "true" || value == "1");
        }
    }

    return opts;
}

std::string serialize_config(const FormatOptions& options) {
    std::ostringstream oss;
    oss << "indent_width = " << options.indent_width << "\n";
    oss << "use_tabs = " << (options.use_tabs ? "true" : "false") << "\n";
    oss << "max_line_length = " << options.max_line_length << "\n";
    oss << "trailing_newline = " << (options.trailing_newline ? "true" : "false") << "\n";
    oss << "align_fields = " << (options.align_fields ? "true" : "false") << "\n";
    oss << "sort_imports = " << (options.sort_imports ? "true" : "false") << "\n";
    return oss.str();
}

FormatOptions default_options() {
    return FormatOptions{};
}

} // namespace ahfl::formatter
