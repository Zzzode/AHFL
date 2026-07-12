#pragma once

#include "tooling/formatter/formatter.hpp"

#include <string>

namespace ahfl::formatter {

[[nodiscard]] FormatResult
format_lossless_source(const std::string &source, const FormatOptions &options);

} // namespace ahfl::formatter
