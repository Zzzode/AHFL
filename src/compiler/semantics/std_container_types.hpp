#pragma once

#include "ahfl/compiler/semantics/types.hpp"

#include <optional>
#include <string_view>

namespace ahfl::stdlib_bridge {

inline constexpr std::string_view kOptionType = "std::option::Option";
inline constexpr std::string_view kListType = "std::collections::List";
inline constexpr std::string_view kSetType = "std::collections::Set";
inline constexpr std::string_view kMapType = "std::collections::Map";

enum class StdContainerKind {
    Option,
    List,
    Set,
    Map,
};

struct StdContainerTypeView {
    StdContainerKind kind{StdContainerKind::List};
    TypePtr first{nullptr};
    TypePtr second{nullptr};
    bool nominal{false};
};

// Identify a Type as one of the four nominal stdlib containers (Option /
// List / Set / Map). When the type has a complete nominal shape (canonical
// name + the expected type_args), `first` / `second` carry the element /
// key-value type arguments. When the user references a bare unspecialised
// nominal (e.g. `Option::None` at a qualified-value owner site, where the
// type parameters are not yet applied), the helper still recognises it via
// its canonical name so callers can short-circuit container-specific
// behaviour before type arguments are re-introduced through the outer
// context. In that case `first` and `second` are nullptr.
[[nodiscard]] inline std::optional<StdContainerTypeView>
std_container_type_view(const Type &type) noexcept {
    // Helper: read up to `nargs` type arguments; tolerate zero args when the
    // container is referenced as a bare nominal owner.
    const auto read_args =
        [](const std::vector<TypePtr> &args,
           std::size_t nargs,
           TypePtr &out_first,
           TypePtr &out_second) noexcept -> bool {
        if (args.empty()) {
            out_first = nullptr;
            out_second = nullptr;
            return true;
        }
        if (args.size() != nargs) {
            return false;
        }
        if (nargs >= 1) {
            if (args[0] == nullptr) return false;
            out_first = args[0];
        }
        if (nargs >= 2) {
            if (args[1] == nullptr) return false;
            out_second = args[1];
        }
        return true;
    };

    if (const auto *option = type.get_if<types::EnumT>();
        option != nullptr && std::string_view{option->canonical_name} == kOptionType) {
        StdContainerTypeView view{
            .kind = StdContainerKind::Option,
            .first = nullptr,
            .second = nullptr,
            .nominal = true,
        };
        if (!read_args(option->type_args, 1, view.first, view.second)) {
            return std::nullopt;
        }
        return view;
    }
    if (const auto *structure = type.get_if<types::StructT>(); structure != nullptr) {
        const std::string_view canonical_name{structure->canonical_name};
        if (canonical_name == kListType) {
            StdContainerTypeView view{
                .kind = StdContainerKind::List,
                .first = nullptr,
                .second = nullptr,
                .nominal = true,
            };
            if (!read_args(structure->type_args, 1, view.first, view.second)) {
                return std::nullopt;
            }
            return view;
        }
        if (canonical_name == kSetType) {
            StdContainerTypeView view{
                .kind = StdContainerKind::Set,
                .first = nullptr,
                .second = nullptr,
                .nominal = true,
            };
            if (!read_args(structure->type_args, 1, view.first, view.second)) {
                return std::nullopt;
            }
            return view;
        }
        if (canonical_name == kMapType) {
            StdContainerTypeView view{
                .kind = StdContainerKind::Map,
                .first = nullptr,
                .second = nullptr,
                .nominal = true,
            };
            if (!read_args(structure->type_args, 2, view.first, view.second)) {
                return std::nullopt;
            }
            return view;
        }
    }

    // An EnumVariantT whose canonical name matches a nominal stdlib enum is
    // also a valid "view" of that container, used by narrowing / subtyping /
    // Option-none diagnostics where a scrutinee has been narrowed to a
    // specific variant (e.g. Option::None) before being compared against the
    // unspecialised enum sentinel.
    if (const auto *variant = type.get_if<types::EnumVariantT>(); variant != nullptr) {
        const std::string_view canonical_name{variant->canonical_name};
        if (canonical_name == kOptionType) {
            StdContainerTypeView view{
                .kind = StdContainerKind::Option,
                .first = nullptr,
                .second = nullptr,
                .nominal = true,
            };
            if (!read_args(variant->type_args, 1, view.first, view.second)) {
                return std::nullopt;
            }
            return view;
        }
    }

    return std::nullopt;
}

} // namespace ahfl::stdlib_bridge
