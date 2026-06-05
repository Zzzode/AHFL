#pragma once

#include <string_view>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

[[nodiscard]] bool has_resolved_symbol(const SymbolRef &ref) noexcept;
[[nodiscard]] bool has_resolved_type(const TypeRef &ref) noexcept;

[[nodiscard]] std::string_view symbol_canonical_name(const SymbolRef &ref,
                                                     std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view symbol_display_name(const SymbolRef &ref,
                                                   std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view type_canonical_name(const TypeRef &ref,
                                                   std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view type_display_name(const TypeRef &ref,
                                                 std::string_view fallback = {}) noexcept;

} // namespace ahfl::ir
