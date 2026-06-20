#pragma once
#include "tooling/formatter/formatter.hpp"
#include <optional>
#include <string>

namespace ahfl::formatter {

[[nodiscard]] std::optional<FormatOptions> load_config(const std::string &path);

[[nodiscard]] std::string serialize_config(const FormatOptions &options);

[[nodiscard]] FormatOptions default_options();

} // namespace ahfl::formatter
