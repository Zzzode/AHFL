#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include "ahfl/support/ownership.hpp"

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

    [[nodiscard]] static TypePtr make(TypeKind kind) {
        return make_owned<Type>(Type{
            .kind = kind,
            .name = {},
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
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
        });
    }

    [[nodiscard]] static TypePtr struct_type(std::string canonical_name) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Struct,
            .name = std::move(canonical_name),
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
        });
    }

    [[nodiscard]] static TypePtr enum_type(std::string canonical_name) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Enum,
            .name = std::move(canonical_name),
            .string_bounds = std::nullopt,
            .decimal_scale = std::nullopt,
            .first = nullptr,
            .second = nullptr,
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

[[nodiscard]] inline bool are_types_equivalent(const Type &lhs, const Type &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }

    switch (lhs.kind) {
    case TypeKind::Any:
    case TypeKind::Never:
    case TypeKind::Unit:
    case TypeKind::Bool:
    case TypeKind::Int:
    case TypeKind::Float:
    case TypeKind::String:
    case TypeKind::UUID:
    case TypeKind::Timestamp:
    case TypeKind::Duration:
        return true;
    case TypeKind::BoundedString:
        return lhs.string_bounds == rhs.string_bounds;
    case TypeKind::Decimal:
        return lhs.decimal_scale == rhs.decimal_scale;
    case TypeKind::Struct:
    case TypeKind::Enum:
        return lhs.name == rhs.name;
    case TypeKind::Optional:
    case TypeKind::List:
    case TypeKind::Set:
        return lhs.first && rhs.first && are_types_equivalent(*lhs.first, *rhs.first);
    case TypeKind::Map:
        return lhs.first && rhs.first && lhs.second && rhs.second &&
               are_types_equivalent(*lhs.first, *rhs.first) &&
               are_types_equivalent(*lhs.second, *rhs.second);
    }

    return false;
}

[[nodiscard]] inline bool is_subtype_of(const Type &source, const Type &target) {
    if (are_types_equivalent(source, target)) {
        return true;
    }

    if (source.kind == TypeKind::BoundedString && target.kind == TypeKind::String) {
        return true;
    }

    if (source.kind == TypeKind::BoundedString && target.kind == TypeKind::BoundedString) {
        return source.string_bounds->first >= target.string_bounds->first &&
               source.string_bounds->second <= target.string_bounds->second;
    }

    if (source.kind == TypeKind::Optional && target.kind == TypeKind::Optional && source.first &&
        target.first) {
        return is_subtype_of(*source.first, *target.first);
    }

    if (source.kind == TypeKind::List && target.kind == TypeKind::List && source.first &&
        target.first) {
        return is_subtype_of(*source.first, *target.first);
    }

    if (source.kind == TypeKind::Set && target.kind == TypeKind::Set && source.first &&
        target.first) {
        return is_subtype_of(*source.first, *target.first);
    }

    if (source.kind == TypeKind::Map && target.kind == TypeKind::Map && source.first &&
        target.first && source.second && target.second) {
        return is_subtype_of(*source.first, *target.first) &&
               is_subtype_of(*source.second, *target.second);
    }

    return false;
}

[[nodiscard]] inline bool is_exact_schema_match(const Type &source, const Type &target) {
    return are_types_equivalent(source, target);
}

} // namespace ahfl
