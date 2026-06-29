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
    std::string_view{"list_sort"},
    std::string_view{"list_dedup"},
    std::string_view{"set_raw_contains"},
    std::string_view{"set_raw_size"},
    std::string_view{"set_raw_empty"},
    std::string_view{"set_raw_singleton"},
    std::string_view{"set_from_array"},
    std::string_view{"set_union"},
    std::string_view{"set_intersection"},
    std::string_view{"set_difference"},
    std::string_view{"set_is_subset"},
    std::string_view{"map_raw_get"},
    std::string_view{"map_raw_contains_key"},
    std::string_view{"map_raw_size"},
    std::string_view{"map_raw_empty"},
    std::string_view{"map_raw_singleton"},
    std::string_view{"map_from_entries"},
    std::string_view{"string_raw_length"},
    std::string_view{"string_raw_concat"},
    std::string_view{"string_raw_substring"},
    std::string_view{"string_contains"},
    std::string_view{"string_starts_with"},
    std::string_view{"string_ends_with"},
    std::string_view{"string_trim"},
    std::string_view{"string_trim_start"},
    std::string_view{"string_trim_end"},
    std::string_view{"string_replace"},
    std::string_view{"string_split"},
    std::string_view{"string_parse_int"},
    std::string_view{"string_split_whitespace"},
    std::string_view{"string_parse_float"},
    std::string_view{"time_now"},
    std::string_view{"time_add"},
    std::string_view{"time_sub"},
    std::string_view{"time_duration_between"},
    std::string_view{"uuid_new_v4"},
    std::string_view{"uuid_parse"},
    std::string_view{"uuid_to_string"},
    std::string_view{"int_to_string"},
    std::string_view{"bool_to_string"},
    std::string_view{"float_to_string"},
    std::string_view{"float_trunc_to_int"},
    std::string_view{"int_to_float"},
    std::string_view{"json_parse_raw"},
    std::string_view{"json_emit_raw"},
    std::string_view{"decimal_raw_make"},
    std::string_view{"decimal_raw_add"},
    std::string_view{"decimal_raw_sub"},
    std::string_view{"decimal_raw_mul"},
    std::string_view{"decimal_raw_scale"},
    std::string_view{"decimal_raw_with_scale"},
    std::string_view{"decimal_raw_quantize"},
    std::string_view{"decimal_raw_eq"},
    std::string_view{"decimal_raw_cmp"},
    std::string_view{"decimal_raw_to_string"},
    std::string_view{"string_raw_compare"},
    std::string_view{"cmp_raw_compare"},
};

[[nodiscard]] inline constexpr const auto &known_builtin_hooks() noexcept {
    return kKnownBuiltinHooks;
}

[[nodiscard]] inline bool is_known_builtin_hook(std::string_view hook) noexcept {
    const auto &hooks = known_builtin_hooks();
    return std::find(hooks.begin(), hooks.end(), hook) != hooks.end();
}

} // namespace ahfl
