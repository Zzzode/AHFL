#pragma once

// Canonical decoding of an AHFL source STRING LITERAL spelling into the exact
// UTF-8 bytes the runtime value carries (without the surrounding quotes).
//
// This is the ONE authority shared by the tree-walking evaluator
// (eval_string_literal) and the Core-Wasm backend's rodata literal pool
// (frame-bridge v2 D1), so a String a computed module returns and the String
// the evaluator returns are byte-identical by construction — a differential
// fixture never has to guess which escape table a lane applied.
//
// Accepted escapes (the source spelling is front-end validated): \n \r \t
// \\ \". Any other backslash sequence keeps the escaped character verbatim
// (the evaluator's permissive historical rule). A spelling that is not wrapped
// in quotes is returned unchanged.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::support {

[[nodiscard]] inline std::string
decode_string_literal_bytes(std::string_view spelling) {
    if (spelling.size() < 2 || spelling.front() != '"' ||
        spelling.back() != '"') {
        return std::string{spelling};
    }
    std::string decoded;
    decoded.reserve(spelling.size() - 2);
    for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
        const char ch = spelling[index];
        if (ch != '\\' || index + 2 >= spelling.size()) {
            decoded.push_back(ch);
            continue;
        }
        const char escaped = spelling[++index];
        switch (escaped) {
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        case '\\':
            decoded.push_back('\\');
            break;
        case '"':
            decoded.push_back('"');
            break;
        default:
            decoded.push_back(escaped);
            break;
        }
    }
    return decoded;
}

} // namespace ahfl::support
