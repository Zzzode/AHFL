#pragma once

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl::backend_printer {

inline void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

} // namespace ahfl::backend_printer
