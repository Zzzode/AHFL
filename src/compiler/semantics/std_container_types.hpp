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

[[nodiscard]] inline std::optional<StdContainerTypeView>
std_container_type_view(const Type &type) noexcept {
    if (const auto *option = type.get_if<types::EnumT>();
        option != nullptr && std::string_view{option->canonical_name} == kOptionType &&
        option->type_args.size() == 1 && option->type_args.front() != nullptr) {
        return StdContainerTypeView{
            .kind = StdContainerKind::Option,
            .first = option->type_args.front(),
            .second = nullptr,
            .nominal = true,
        };
    }
    if (const auto *structure = type.get_if<types::StructT>(); structure != nullptr) {
        const std::string_view canonical_name{structure->canonical_name};
        if (canonical_name == kListType && structure->type_args.size() == 1 &&
            structure->type_args.front() != nullptr) {
            return StdContainerTypeView{
                .kind = StdContainerKind::List,
                .first = structure->type_args.front(),
                .second = nullptr,
                .nominal = true,
            };
        }
        if (canonical_name == kSetType && structure->type_args.size() == 1 &&
            structure->type_args.front() != nullptr) {
            return StdContainerTypeView{
                .kind = StdContainerKind::Set,
                .first = structure->type_args.front(),
                .second = nullptr,
                .nominal = true,
            };
        }
        if (canonical_name == kMapType && structure->type_args.size() == 2 &&
            structure->type_args[0] != nullptr && structure->type_args[1] != nullptr) {
            return StdContainerTypeView{
                .kind = StdContainerKind::Map,
                .first = structure->type_args[0],
                .second = structure->type_args[1],
                .nominal = true,
            };
        }
    }

    return std::nullopt;
}

} // namespace ahfl::stdlib_bridge
