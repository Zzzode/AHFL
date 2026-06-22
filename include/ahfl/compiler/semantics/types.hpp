#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/semantics/effect_judgement.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"

namespace ahfl {

enum class TypeKind {
    // Checker-internal helper kinds. These are not directly written by users
    // but are produced by the type system during inference / instantiation.
    Any,
    Never,
    Error,
    // P2 (RFC §3.2.2 / §5): a type variable standing in for a concrete type
    // inside a generic declaration. Resolved to a concrete type during
    // monomorphization (type substitution).
    TypeVar,

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
    EnumVariant,

    // Source-level composite kinds.
    Optional,
    List,
    Set,
    Map,
    Fn,
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
    // Concrete type arguments of an instantiated generic struct.
    // Empty for monomorphic structs or the generic definition itself.
    // Index matches the declaration's type parameter position.
    std::vector<TypePtr> type_args;
};
struct EnumT {
    std::string canonical_name;
    std::optional<SymbolId> symbol;
    // Concrete type arguments of an instantiated generic enum.
    // Empty for monomorphic enums or the generic definition itself.
    std::vector<TypePtr> type_args;
};
struct EnumVariantT {
    std::string canonical_name;
    std::optional<SymbolId> symbol;
    std::string variant_name;
    // Concrete type arguments of the parent enum's instantiation.
    std::vector<TypePtr> type_args;
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
struct FnT {
    std::vector<TypePtr> params;
    TypePtr return_type{nullptr};
    EffectJudgement effect;
};
// P2 (RFC §5): a named type variable. Used as a placeholder inside generic
// P2 (RFC §3.2.2 / §5): a type variable placeholder. Used inside generic
// fn/trait signatures and their bodies; replaced with a concrete type during
// monomorphization.
//
// `index` is the zero-based position of this type parameter in the enclosing
// generic declaration's parameter list. This is the *canonical identity* of
// the variable — substitution uses index-based O(1) lookup (industry standard
// practice: Rust Substs, Swift GenericTypeParamKey, Clang TemplateParmIndex),
// not string hashing. The `name` is preserved only for diagnostics and
// human-readable rendering.
struct TypeVarT {
    std::uint32_t index{0};
    std::string name;
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
                             EnumVariantT,
                             OptionalT,
                             ListT,
                             SetT,
                             MapT,
                             FnT,
                             TypeVarT>;

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

    template <typename Alt> [[nodiscard]] const Alt *get_if() const noexcept {
        return std::get_if<Alt>(&payload);
    }

    template <typename Alt> [[nodiscard]] bool holds() const noexcept {
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
            [](const types::StructT &value) {
                std::string result = value.canonical_name;
                if (!value.type_args.empty()) {
                    result += '<';
                    for (std::size_t i = 0; i < value.type_args.size(); ++i) {
                        if (i > 0) {
                            result += ", ";
                        }
                        result += value.type_args[i] ? value.type_args[i]->describe()
                                                      : std::string{"Any"};
                    }
                    result += '>';
                }
                return result;
            },
            [](const types::EnumT &value) {
                std::string result = value.canonical_name;
                if (!value.type_args.empty()) {
                    result += '<';
                    for (std::size_t i = 0; i < value.type_args.size(); ++i) {
                        if (i > 0) {
                            result += ", ";
                        }
                        result += value.type_args[i] ? value.type_args[i]->describe()
                                                      : std::string{"Any"};
                    }
                    result += '>';
                }
                return result;
            },
            [](const types::EnumVariantT &value) {
                std::string result = value.canonical_name;
                if (!value.type_args.empty()) {
                    result += '<';
                    for (std::size_t i = 0; i < value.type_args.size(); ++i) {
                        if (i > 0) {
                            result += ", ";
                        }
                        result += value.type_args[i] ? value.type_args[i]->describe()
                                                      : std::string{"Any"};
                    }
                    result += '>';
                }
                result += "::" + value.variant_name;
                return result;
            },
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
            [](const types::FnT &value) {
                std::ostringstream builder;
                builder << "Fn(";
                for (std::size_t i = 0; i < value.params.size(); ++i) {
                    if (i > 0) {
                        builder << ", ";
                    }
                    builder << (value.params[i] ? value.params[i]->describe() : std::string{"Any"});
                }
                builder << ") -> " << (value.return_type ? value.return_type->describe()
                                                          : std::string{"Any"});
                if (value.effect.kind != EffectJudgement::Kind::Pure) {
                    builder << " effect " << to_string(value.effect);
                }
                return builder.str();
            },
            [](const types::TypeVarT &value) { return value.name; },
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
