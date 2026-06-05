#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"

namespace ahfl {

enum class TypeKind {
    // Checker-internal helper kinds. These are not source-level AHFL types.
    Any,
    Never,

    // Source-level primitive kinds.
    Unit,
    Bool,
    Int,
    Float,
    String,
    BoundedString,
    UUID,
    Timestamp,
    Duration,
    Decimal,

    // Source-level nominal kinds.
    Struct,
    Enum,

    // Source-level composite kinds.
    Optional,
    List,
    Set,
    Map,
};

struct Type;
using TypePtr = Owned<Type>;

struct Type {
    TypeKind kind{TypeKind::Any};
    std::string name;
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
    std::optional<std::int64_t> decimal_scale;
    TypePtr first;
    TypePtr second;
    std::optional<SymbolId> nominal_symbol;

    [[nodiscard]] static TypePtr make(TypeKind kind) {
        return make_owned<Type>(Type{
            .kind = kind,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr string() {
        return make(TypeKind::String);
    }

    [[nodiscard]] static TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum) {
        return make_owned<Type>(Type{
            .kind = TypeKind::BoundedString,
            .name = {},
            .string_bounds = std::make_pair(minimum, maximum),
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr decimal(std::int64_t scale) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Decimal,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = scale,
            .first = nullptr,
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr struct_type(std::string canonical_name) {
        return struct_type(std::move(canonical_name), std::nullopt);
    }

    [[nodiscard]] static TypePtr struct_type(std::string canonical_name, SymbolId symbol) {
        return struct_type(std::move(canonical_name), std::optional<SymbolId>{symbol});
    }

    [[nodiscard]] static TypePtr struct_type(std::string canonical_name,
                                             std::optional<SymbolId> symbol) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Struct,
            .name = std::move(canonical_name),
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
            .nominal_symbol = symbol,
        });
    }

    [[nodiscard]] static TypePtr enum_type(std::string canonical_name) {
        return enum_type(std::move(canonical_name), std::nullopt);
    }

    [[nodiscard]] static TypePtr enum_type(std::string canonical_name, SymbolId symbol) {
        return enum_type(std::move(canonical_name), std::optional<SymbolId>{symbol});
    }

    [[nodiscard]] static TypePtr enum_type(std::string canonical_name,
                                           std::optional<SymbolId> symbol) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Enum,
            .name = std::move(canonical_name),
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
            .nominal_symbol = symbol,
        });
    }

    [[nodiscard]] static TypePtr optional(TypePtr value_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Optional,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = std::move(value_type),
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr list(TypePtr element_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::List,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = std::move(element_type),
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr set(TypePtr element_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Set,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = std::move(element_type),
            .second = nullptr,
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] static TypePtr map(TypePtr key_type, TypePtr value_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Map,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = std::move(key_type),
            .second = std::move(value_type),
            .nominal_symbol = std::nullopt,
        });
    }

    [[nodiscard]] TypePtr clone() const {
        auto copy = make_owned<Type>();
        copy->kind = kind;
        copy->name = name;
        copy->string_bounds = string_bounds;
        copy->decimal_scale = decimal_scale;
        copy->first = first ? first->clone() : nullptr;
        copy->second = second ? second->clone() : nullptr;
        copy->nominal_symbol = nominal_symbol;
        return copy;
    }

    [[nodiscard]] std::string describe() const {
        switch (kind) {
        case TypeKind::Any:
            return "Any";
        case TypeKind::Never:
            return "Never";
        case TypeKind::Unit:
            return "Unit";
        case TypeKind::Bool:
            return "Bool";
        case TypeKind::Int:
            return "Int";
        case TypeKind::Float:
            return "Float";
        case TypeKind::String:
            return "String";
        case TypeKind::BoundedString: {
            std::ostringstream builder;
            builder << "String(" << string_bounds->first << ", " << string_bounds->second << ")";
            return builder.str();
        }
        case TypeKind::UUID:
            return "UUID";
        case TypeKind::Timestamp:
            return "Timestamp";
        case TypeKind::Duration:
            return "Duration";
        case TypeKind::Decimal: {
            std::ostringstream builder;
            builder << "Decimal(" << *decimal_scale << ")";
            return builder.str();
        }
        case TypeKind::Struct:
            return name;
        case TypeKind::Enum:
            return name;
        case TypeKind::Optional:
            return "Optional<" + first->describe() + ">";
        case TypeKind::List:
            return "List<" + first->describe() + ">";
        case TypeKind::Set:
            return "Set<" + first->describe() + ">";
        case TypeKind::Map:
            return "Map<" + first->describe() + ", " + second->describe() + ">";
        }

        return "<invalid-type>";
    }
};

[[nodiscard]] inline bool is_internal_type_kind(TypeKind kind) noexcept {
    return kind == TypeKind::Any || kind == TypeKind::Never;
}

[[nodiscard]] inline bool is_source_type_kind(TypeKind kind) noexcept {
    return !is_internal_type_kind(kind);
}

[[nodiscard]] inline bool is_collection(TypeKind kind) noexcept {
    return kind == TypeKind::List || kind == TypeKind::Set || kind == TypeKind::Map;
}

[[nodiscard]] inline bool is_schema_type_kind(TypeKind kind) noexcept {
    return kind == TypeKind::Struct;
}

} // namespace ahfl
