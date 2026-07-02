#pragma once

#include <string>
#include <string_view>

namespace ahfl::support {

[[nodiscard]] std::string sha256_hex(std::string_view bytes);

} // namespace ahfl::support
