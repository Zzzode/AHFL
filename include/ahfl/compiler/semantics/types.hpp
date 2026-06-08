#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>

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

// `TypePtr` was historically `Owned<Type>`. As of the type-interning refactor
// it is a non-owning `const Type*`: every Type is hash-consed by the global
// TypeContext so identical types share a single canonical instance and no
// caller needs to deep-copy them. `nullptr` is still a valid sentinel for
// "missing/optional".
using TypePtr = const Type *;

struct Type {
    TypeKind kind{TypeKind::Any};
    std::string name;
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
    std::optional<std::int64_t> decimal_scale;
    TypePtr first{nullptr};
    TypePtr second{nullptr};
    std::optional<SymbolId> nominal_symbol;

    // Factories below intern through the process-wide TypeContext (see
    // types.cpp). They return canonical `const Type*` pointers; identical
    // arguments always yield the same pointer.
    [[nodiscard]] static TypePtr make(TypeKind kind);
    [[nodiscard]] static TypePtr string();
    [[nodiscard]] static TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum);
    [[nodiscard]] static TypePtr decimal(std::int64_t scale);

    [[nodiscard]] static TypePtr struct_type(std::string canonical_name);
    [[nodiscard]] static TypePtr struct_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] static TypePtr struct_type(std::string canonical_name,
                                             std::optional<SymbolId> symbol);

    [[nodiscard]] static TypePtr enum_type(std::string canonical_name);
    [[nodiscard]] static TypePtr enum_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] static TypePtr enum_type(std::string canonical_name,
                                           std::optional<SymbolId> symbol);

    [[nodiscard]] static TypePtr optional(TypePtr value_type);
    [[nodiscard]] static TypePtr list(TypePtr element_type);
    [[nodiscard]] static TypePtr set(TypePtr element_type);
    [[nodiscard]] static TypePtr map(TypePtr key_type, TypePtr value_type);

    // After interning, cloning is a no-op identity. The signature is kept so
    // existing call sites that wrote `type->clone()` keep compiling without
    // dereferencing changes; semantically these now share the canonical node.
    [[nodiscard]] TypePtr clone() const noexcept {
        return this;
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
