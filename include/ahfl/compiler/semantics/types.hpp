#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
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

// Forward declarations of legacy composite sugar types (RFC §3.2 migration).
// Kept intentionally UNDEFINED: they exist *solely* so the SFINAE trait
// `detail::IsForbiddenSugar` can recognise the names by type identity even
// though the real definitions were removed. Any attempt to materialise one of
// these (sizeof, data members, allocation) is a separate hard stop. They must
// never appear as Payload alternatives (see architectural gates below).
struct OptionalT;
struct ListT;
struct SetT;
struct MapT;

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
                             FnT,
                             TypeVarT>;

// --- Architectural gates (P5.4 / R-01) ---------------------------------------
//
// The Payload variant has a fixed cardinality of 18. Adding, removing, or
// reordering alternatives is a cross-cutting change: every visitor switch,
// every typed HIR lowering branch, and every serialization round-trip must be
// kept in sync. Update the number below AND the architecture Python gate
// (scripts/check-architecture.py) together.
//
// Historical composite "sugar" types (OptionalT / ListT / SetT / MapT) are
// intentionally *not* Payload alternatives. Composites are now encoded through
// nominal generics on StructT/EnumT (RFC §3.2) rather than ad-hoc union
// branches. The two compile-time gates below + the Python script gate keep
// this invariant from regressing.

namespace detail {

// True iff T is one of the forbidden legacy sugar payloads. The trait matches
// by type identity against the forward declarations above (which intentionally
// have no body anywhere in the project).
template <typename T> struct IsForbiddenSugar : std::false_type {};
template <> struct IsForbiddenSugar<OptionalT> : std::true_type {};
template <> struct IsForbiddenSugar<ListT> : std::true_type {};
template <> struct IsForbiddenSugar<SetT> : std::true_type {};
template <> struct IsForbiddenSugar<MapT> : std::true_type {};

// Fold a pack: any alternative is a forbidden sugar?
template <typename... Ts>
constexpr bool AnyForbiddenSugarV = (IsForbiddenSugar<Ts>::value || ...);

} // namespace detail

// 1) Cardinality gate. Spelled `== 18` so a +1/-1 change breaks compilation
//    immediately. Hand-test: change to 17 and confirm a hard error.
static_assert(std::variant_size_v<Payload> == 18,
              "ahfl::types::Payload cardinality drift. Update this static_assert "
              "along with every visitor/lowering/serialization site, and keep "
              "scripts/check-architecture.py aligned.");

// 2) Forbidden-sugar gate. OptionalT / ListT / SetT / MapT must never appear
//    as a Payload alternative. Composites live on nominal generics only.
//    Implementation note: we enumerate every slot via std::variant_alternative
//    because C++ does not (yet) expose the pack of alternatives directly. If
//    arity grows, the cardinality static_assert fires first and this list is
//    updated alongside it.
static_assert(!detail::AnyForbiddenSugarV<
                  std::variant_alternative_t<0, Payload>,
                  std::variant_alternative_t<1, Payload>,
                  std::variant_alternative_t<2, Payload>,
                  std::variant_alternative_t<3, Payload>,
                  std::variant_alternative_t<4, Payload>,
                  std::variant_alternative_t<5, Payload>,
                  std::variant_alternative_t<6, Payload>,
                  std::variant_alternative_t<7, Payload>,
                  std::variant_alternative_t<8, Payload>,
                  std::variant_alternative_t<9, Payload>,
                  std::variant_alternative_t<10, Payload>,
                  std::variant_alternative_t<11, Payload>,
                  std::variant_alternative_t<12, Payload>,
                  std::variant_alternative_t<13, Payload>,
                  std::variant_alternative_t<14, Payload>,
                  std::variant_alternative_t<15, Payload>,
                  std::variant_alternative_t<16, Payload>,
                  std::variant_alternative_t<17, Payload>>,
              "OptionalT / ListT / SetT / MapT are forbidden as ahfl::types::Payload "
              "alternatives. Composites are encoded via nominal generics on "
              "StructT/EnumT. See scripts/check-architecture.py.");

// 3) SFINAE helper for factory sites (e.g. TypeContext::build_payload and any
//    future Payload materializers). Using it on a sugar type produces a
//    substitution failure rather than a late linker/visitor error:
//
//      template <typename T, typename = PayloadAllowed<T>>
//      void make_payload(T &&) { ... }
template <typename T>
using PayloadAllowed = std::enable_if_t<!detail::IsForbiddenSugar<std::decay_t<T>>::value, int>;

// Helper for ad-hoc visitors using the overload pattern, e.g.
//
//   type->visit(types::Overloads{
//       [](const types::IntT &) { ... },
//       [](const types::StructT &structure) { ... structure.type_args ... },
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

[[nodiscard]] inline bool is_schema_type_kind(TypeKind kind) noexcept {
    return kind == TypeKind::Struct;
}

} // namespace ahfl
