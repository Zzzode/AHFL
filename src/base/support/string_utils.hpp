#pragma once

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl {

/// Join a vector of strings with a delimiter.
[[nodiscard]] inline std::string join(const std::vector<std::string> &parts,
                                      std::string_view delimiter) {
    std::string joined;

    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index != 0) {
            joined += delimiter;
        }
        joined += parts[index];
    }

    return joined;
}

/// Sanitize a name into a valid identifier (alphanumeric + underscore, no double underscores).
/// If the result starts with a digit, prepends "n_".
[[nodiscard]] inline std::string sanitize_identifier(std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size() + 2);

    for (const auto character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            sanitized.push_back(character);
        } else {
            sanitized.push_back('_');
        }
    }

    while (sanitized.find("__") != std::string::npos) {
        sanitized.replace(sanitized.find("__"), 2, "_");
    }

    if (sanitized.empty()) {
        return "id";
    }

    if (std::isdigit(static_cast<unsigned char>(sanitized.front())) != 0) {
        sanitized.insert(sanitized.begin(), 'n');
        sanitized.insert(sanitized.begin() + 1, '_');
    }

    return sanitized;
}

} // namespace ahfl
