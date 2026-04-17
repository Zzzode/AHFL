#pragma once

#include <ostream>
#include <string_view>

namespace ahfl {

inline void write_escaped_json_string(std::ostream &out, std::string_view value) {
    constexpr char kHexDigits[] = "0123456789abcdef";

    out << '"';
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\b':
            out << "\\b";
            break;
        case '\f':
            out << "\\f";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                out << "\\u00" << kHexDigits[(byte >> 4U) & 0x0FU] << kHexDigits[byte & 0x0FU];
            } else {
                out << character;
            }
            break;
        }
    }
    out << '"';
}

} // namespace ahfl
