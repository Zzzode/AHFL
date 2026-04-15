#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

#include "ahfl/ownership.hpp"

namespace ahfl {

enum class TypeKind {
    Any,
    Never,
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
    Named,
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
        return make_owned<Type>(Type{.kind = kind});
    }

    [[nodiscard]] static TypePtr named(std::string name) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Named,
            .name = std::move(name),
        });
    }

    [[nodiscard]] static TypePtr string() {
        return make(TypeKind::String);
    }

    [[nodiscard]] static TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum) {
        return make_owned<Type>(Type{
            .kind = TypeKind::BoundedString,
            .string_bounds = std::make_pair(minimum, maximum),
        });
    }

    [[nodiscard]] static TypePtr decimal(std::int64_t scale) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Decimal,
            .decimal_scale = scale,
        });
    }

    [[nodiscard]] static TypePtr optional(TypePtr value_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Optional,
            .first = std::move(value_type),
        });
    }

    [[nodiscard]] static TypePtr list(TypePtr element_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::List,
            .first = std::move(element_type),
        });
    }

    [[nodiscard]] static TypePtr set(TypePtr element_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Set,
            .first = std::move(element_type),
        });
    }

    [[nodiscard]] static TypePtr map(TypePtr key_type, TypePtr value_type) {
        return make_owned<Type>(Type{
            .kind = TypeKind::Map,
            .first = std::move(key_type),
            .second = std::move(value_type),
        });
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
        case TypeKind::Named:
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

[[nodiscard]] inline bool is_collection(TypeKind kind) noexcept {
    return kind == TypeKind::List || kind == TypeKind::Set || kind == TypeKind::Map;
}

} // namespace ahfl
