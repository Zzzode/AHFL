#pragma once

#include <cctype>
#include <string>
#include <string_view>

namespace ahfl::backends {

// DNS-1123 label charset: [a-z0-9-], lowercased. Any other byte is replaced
// with '-'; runs of separators are collapsed and leading/trailing separators
// trimmed.
[[nodiscard]] inline std::string dns_safe(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            out.push_back(static_cast<char>(ch));
        } else if (!out.empty() && out.back() != '-') {
            out.push_back('-');
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

[[nodiscard]] inline std::string capitalize(std::string_view s) {
    if (s.empty())
        return {};
    std::string result(s);
    result[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(result[0])));
    return result;
}

} // namespace ahfl::backends
