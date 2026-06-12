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
    Error,

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
struct ErrorT {};
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
                             ErrorT,
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
    // Authoritative semantic type representation. All payload-specific data
    // must be accessed through visit(), get_if<T>(), or holds<T>(); this keeps
    // Type as a single-source-of-truth tagged union rather than a kind plus
    // nullable mirror fields.
    types::Payload payload{types::AnyT{}};

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
        return visit(types::Overloads{
            [](const types::AnyT &) { return std::string{"Any"}; },
            [](const types::NeverT &) { return std::string{"Never"}; },
            [](const types::ErrorT &) { return std::string{"<error>"}; },
            [](const types::UnitT &) { return std::string{"Unit"}; },
            [](const types::BoolT &) { return std::string{"Bool"}; },
            [](const types::IntT &) { return std::string{"Int"}; },
            [](const types::FloatT &) { return std::string{"Float"}; },
            [](const types::StringT &) { return std::string{"String"}; },
            [](const types::UUIDT &) { return std::string{"UUID"}; },
            [](const types::TimestampT &) { return std::string{"Timestamp"}; },
            [](const types::DurationT &) { return std::string{"Duration"}; },
            [](const types::BoundedStringT &value) {
                std::ostringstream builder;
                builder << "String(" << value.minimum << ", " << value.maximum << ")";
                return builder.str();
            },
            [](const types::DecimalT &value) {
                std::ostringstream builder;
                builder << "Decimal(" << value.scale << ")";
                return builder.str();
            },
            [](const types::StructT &value) { return value.canonical_name; },
            [](const types::EnumT &value) { return value.canonical_name; },
            [](const types::OptionalT &value) {
                return "Optional<" + (value.inner ? value.inner->describe() : std::string{"Any"}) +
                       ">";
            },
            [](const types::ListT &value) {
                return "List<" + (value.element ? value.element->describe() : std::string{"Any"}) +
                       ">";
            },
            [](const types::SetT &value) {
                return "Set<" + (value.element ? value.element->describe() : std::string{"Any"}) +
                       ">";
            },
            [](const types::MapT &value) {
                return "Map<" + (value.key ? value.key->describe() : std::string{"Any"}) + ", " +
                       (value.value ? value.value->describe() : std::string{"Any"}) + ">";
            },
        });
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
