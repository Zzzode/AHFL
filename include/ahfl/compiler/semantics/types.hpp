#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

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

// ============================================================================
// std::variant payload alternatives (Task #2)
// ============================================================================
//
// Every type kind has a per-alternative struct so the payload of a Type is a
// std::variant<...>. New code SHOULD operate on the payload through Type::visit
// (Task #4) or Type::get_if<...>; existing code may continue to use the legacy
// kind/name/first/second mirror fields, which the factories keep in sync with
// the variant.

namespace types {

struct AnyT {};
struct NeverT {};
struct UnitT {};
struct BoolT {};
struct IntT {};
struct FloatT {};
struct StringT {};
struct BoundedStringT {
    std::int64_t minimum{0};
    std::int64_t maximum{0};
};
struct UUIDT {};
struct TimestampT {};
struct DurationT {};
struct DecimalT {
    std::int64_t scale{0};
};
struct StructT {
    std::string canonical_name;
    std::optional<SymbolId> symbol;
};
struct EnumT {
    std::string canonical_name;
    std::optional<SymbolId> symbol;
};
struct OptionalT {
    TypePtr inner{nullptr};
};
struct ListT {
    TypePtr element{nullptr};
};
struct SetT {
    TypePtr element{nullptr};
};
struct MapT {
    TypePtr key{nullptr};
    TypePtr value{nullptr};
};

using Payload = std::variant<AnyT,
                             NeverT,
                             UnitT,
                             BoolT,
                             IntT,
                             FloatT,
                             StringT,
                             BoundedStringT,
                             UUIDT,
                             TimestampT,
                             DurationT,
                             DecimalT,
                             StructT,
                             EnumT,
                             OptionalT,
                             ListT,
                             SetT,
                             MapT>;

// Helper for ad-hoc visitors using the overload pattern, e.g.
//
//   type->visit(types::Overloads{
//       [](const types::IntT &) { ... },
//       [](const types::ListT &list) { ... list.element ... },
//       [](const auto &) { /* fallback */ },
//   });
template <class... Fs> struct Overloads : Fs... {
    using Fs::operator()...;
};
template <class... Fs> Overloads(Fs...) -> Overloads<Fs...>;

} // namespace types

struct Type {
    // Authoritative payload introduced by Task #2. The legacy mirror fields
    // below are derived from this in the factories (see types.cpp) and exist
    // purely for backward compatibility with the ~270 call sites that still
    // read kind / name / first / second / etc. directly.
    types::Payload payload{types::AnyT{}};

    // Legacy mirror fields. Treat as read-only views into `payload`.
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

    // Variant-aware accessors (Task #4 visitor entry points).
    template <typename Visitor> decltype(auto) visit(Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), payload);
    }

    template <typename Alt>[[nodiscard]] const Alt *get_if() const noexcept {
        return std::get_if<Alt>(&payload);
    }

    template <typename Alt>[[nodiscard]] bool holds() const noexcept {
        return std::holds_alternative<Alt>(payload);
    }

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
