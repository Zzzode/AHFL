#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl {

[[nodiscard]] std::optional<std::string> suggest_name(std::string_view name,
                                                      const std::vector<std::string> &candidates);

} // namespace ahfl
