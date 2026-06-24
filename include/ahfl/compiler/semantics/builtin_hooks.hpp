#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace ahfl {

inline constexpr std::array kKnownBuiltinHooks{
    std::string_view{"list_raw_get"},
    std::string_view{"list_raw_set"},
    std::string_view{"list_raw_length"},
    std::string_view{"list_raw_alloc"},
    std::string_view{"list_from_array"},
    std::string_view{"set_raw_contains"},
    std::string_view{"set_raw_size"},
    std::string_view{"set_raw_empty"},
    std::string_view{"set_raw_singleton"},
    std::string_view{"set_from_array"},
    std::string_view{"map_raw_get"},
    std::string_view{"map_raw_contains_key"},
    std::string_view{"map_raw_size"},
    std::string_view{"map_raw_empty"},
    std::string_view{"map_raw_singleton"},
    std::string_view{"map_from_entries"},
    std::string_view{"string_raw_length"},
    std::string_view{"string_raw_concat"},
    std::string_view{"string_contains"},
    std::string_view{"string_starts_with"},
    std::string_view{"string_ends_with"},
    std::string_view{"time_now"},
    std::string_view{"time_add"},
    std::string_view{"time_sub"},
    std::string_view{"time_duration_between"},
    std::string_view{"uuid_new_v4"},
    std::string_view{"uuid_parse"},
    std::string_view{"uuid_to_string"},
};

[[nodiscard]] inline constexpr const auto &known_builtin_hooks() noexcept {
    return kKnownBuiltinHooks;
}

[[nodiscard]] inline bool is_known_builtin_hook(std::string_view hook) noexcept {
    const auto &hooks = known_builtin_hooks();
    return std::find(hooks.begin(), hooks.end(), hook) != hooks.end();
}

} // namespace ahfl
