// Type interning / hash-consing implementation.
//
// The TypeContext owns every Type instance ever produced. Equal types (same
// kind + payload + child pointers) collapse to a single canonical instance,
// so downstream code can rely on raw pointer identity for fast equivalence
// checks and avoid deep cloning entirely.
//
// Concurrency model: AHFL's compilation pipeline drives every TypeChecker pass
// from a single thread. The context is therefore protected by a mutex out of
// caution but is otherwise expected to see negligible contention.

#include "ahfl/compiler/semantics/type_context.hpp"

#include <cstddef>
#include <tuple>
#include <utility>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t hash_mix(std::size_t seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

} // namespace

std::size_t TypeContext::TypeKeyHash::operator()(const TypeKey &key) const noexcept {
    auto seed = std::hash<int>{}(static_cast<int>(key.kind));
    seed = hash_mix(seed, std::hash<std::string>{}(key.name));
    seed = hash_mix(seed, std::hash<std::string>{}(key.variant_name));
    seed = hash_mix(seed, std::hash<bool>{}(key.string_bounds.has_value()));
    if (key.string_bounds.has_value()) {
        seed = hash_mix(seed, std::hash<std::int64_t>{}(key.string_bounds->first));
        seed = hash_mix(seed, std::hash<std::int64_t>{}(key.string_bounds->second));
    }
    seed = hash_mix(seed, std::hash<bool>{}(key.decimal_scale.has_value()));
    if (key.decimal_scale.has_value()) {
        seed = hash_mix(seed, std::hash<std::int64_t>{}(*key.decimal_scale));
    }
    seed = hash_mix(seed, std::hash<bool>{}(key.nominal_symbol.has_value()));
    if (key.nominal_symbol.has_value()) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(key.nominal_symbol->value));
    }
    seed = hash_mix(seed, std::hash<bool>{}(key.type_var_index.has_value()));
    if (key.type_var_index.has_value()) {
        seed = hash_mix(seed, std::hash<std::uint32_t>{}(*key.type_var_index));
    }
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.type_args.size()));
    for (const auto *arg : key.type_args) {
        seed = hash_mix(seed, std::hash<const void *>{}(arg));
    }
    return seed;
}

TypeContext &TypeContext::global() {
    static TypeContext context;
    return context;
}

const Type *TypeContext::intern(TypeKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto iter = pool_.find(key); iter != pool_.end()) {
        return iter->second;
    }

    auto owned = std::make_unique<Type>();
    owned->payload = build_payload(key);

    const Type *raw = owned.get();
    storage_.push_back(std::move(owned));
    pool_.emplace(std::move(key), raw);
    return raw;
}

types::Payload TypeContext::build_payload(const TypeKey &key) {
    switch (key.kind) {
    case TypeKind::Any:
        return types::AnyT{};
    case TypeKind::Never:
        return types::NeverT{};
    case TypeKind::Error:
        return types::ErrorT{};
    case TypeKind::Unit:
        return types::UnitT{};
    case TypeKind::Bool:
        return types::BoolT{};
    case TypeKind::Int:
        return types::IntT{};
    case TypeKind::Float:
        return types::FloatT{};
    case TypeKind::String:
        return types::StringT{};
    case TypeKind::BoundedString:
        return types::BoundedStringT{
            .minimum = key.string_bounds ? key.string_bounds->first : 0,
            .maximum = key.string_bounds ? key.string_bounds->second : 0,
        };
    case TypeKind::UUID:
        return types::UUIDT{};
    case TypeKind::Timestamp:
        return types::TimestampT{};
    case TypeKind::Duration:
        return types::DurationT{};
    case TypeKind::Decimal:
        return types::DecimalT{.scale = key.decimal_scale.value_or(0)};
    case TypeKind::Struct:
        return types::StructT{
            .canonical_name = key.name,
            .symbol = key.nominal_symbol,
            .type_args = key.type_args,
        };
    case TypeKind::Enum:
        return types::EnumT{
            .canonical_name = key.name,
            .symbol = key.nominal_symbol,
            .type_args = key.type_args,
        };
    case TypeKind::EnumVariant:
        return types::EnumVariantT{
            .canonical_name = key.name,
            .symbol = key.nominal_symbol,
            .variant_name = key.variant_name,
            .type_args = key.type_args,
        };
    case TypeKind::Fn:
        // Fn types use the dedicated fn_pool_; build_payload should not be
        // called with TypeKind::Fn. Fall back defensively.
        return types::FnT{};
    case TypeKind::TypeVar:
        return types::TypeVarT{
            .index = key.type_var_index.value_or(0),
            .name = key.name,
        };
    }

    return types::AnyT{};
}

TypePtr TypeContext::make(TypeKind kind) {
    return intern(TypeKey{
        .kind = kind,
        .name = {},
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::error_type() {
    return make(TypeKind::Error);
}

TypePtr TypeContext::string() {
    return make(TypeKind::String);
}

TypePtr TypeContext::bounded_string(std::int64_t minimum, std::int64_t maximum) {
    return intern(TypeKey{
        .kind = TypeKind::BoundedString,
        .name = {},
        .variant_name = {},
        .string_bounds = std::make_pair(minimum, maximum),
        .decimal_scale = std::nullopt,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::decimal(std::int64_t scale) {
    return intern(TypeKey{
        .kind = TypeKind::Decimal,
        .name = {},
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = scale,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::struct_type(std::string canonical_name) {
    return struct_type(std::move(canonical_name), std::nullopt);
}

TypePtr TypeContext::struct_type(std::string canonical_name, SymbolId symbol) {
    return struct_type(std::move(canonical_name), std::optional<SymbolId>{symbol});
}

TypePtr TypeContext::struct_type(std::string canonical_name, std::optional<SymbolId> symbol) {
    return struct_type(std::move(canonical_name), std::move(symbol), {});
}

TypePtr TypeContext::struct_type(std::string canonical_name,
                                 SymbolId symbol,
                                 std::vector<TypePtr> type_args) {
    return struct_type(
        std::move(canonical_name), std::optional<SymbolId>{symbol}, std::move(type_args));
}

TypePtr TypeContext::struct_type(std::string canonical_name,
                                 std::optional<SymbolId> symbol,
                                 std::vector<TypePtr> type_args) {
    return intern(TypeKey{
        .kind = TypeKind::Struct,
        .name = std::move(canonical_name),
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .nominal_symbol = symbol,
        .type_var_index = std::nullopt,
        .type_args = std::move(type_args),
    });
}

TypePtr TypeContext::enum_type(std::string canonical_name) {
    return enum_type(std::move(canonical_name), std::nullopt);
}

TypePtr TypeContext::enum_type(std::string canonical_name, SymbolId symbol) {
    return enum_type(std::move(canonical_name), std::optional<SymbolId>{symbol});
}

TypePtr TypeContext::enum_type(std::string canonical_name, std::optional<SymbolId> symbol) {
    return enum_type(std::move(canonical_name), std::move(symbol), {});
}

TypePtr TypeContext::enum_type(std::string canonical_name,
                               SymbolId symbol,
                               std::vector<TypePtr> type_args) {
    return enum_type(
        std::move(canonical_name), std::optional<SymbolId>{symbol}, std::move(type_args));
}

TypePtr TypeContext::enum_type(std::string canonical_name,
                               std::optional<SymbolId> symbol,
                               std::vector<TypePtr> type_args) {
    return intern(TypeKey{
        .kind = TypeKind::Enum,
        .name = std::move(canonical_name),
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .nominal_symbol = symbol,
        .type_var_index = std::nullopt,
        .type_args = std::move(type_args),
    });
}

TypePtr TypeContext::enum_variant_type(std::string canonical_name, std::string variant_name) {
    return enum_variant_type(std::move(canonical_name), std::move(variant_name), std::nullopt);
}

TypePtr TypeContext::enum_variant_type(std::string canonical_name,
                                       std::string variant_name,
                                       SymbolId symbol) {
    return enum_variant_type(
        std::move(canonical_name), std::move(variant_name), std::optional<SymbolId>{symbol});
}

TypePtr TypeContext::enum_variant_type(std::string canonical_name,
                                       std::string variant_name,
                                       std::optional<SymbolId> symbol) {
    return enum_variant_type(
        std::move(canonical_name), std::move(variant_name), std::move(symbol), {});
}

TypePtr TypeContext::enum_variant_type(std::string canonical_name,
                                       std::string variant_name,
                                       SymbolId symbol,
                                       std::vector<TypePtr> type_args) {
    return enum_variant_type(std::move(canonical_name),
                             std::move(variant_name),
                             std::optional<SymbolId>{symbol},
                             std::move(type_args));
}

TypePtr TypeContext::enum_variant_type(std::string canonical_name,
                                       std::string variant_name,
                                       std::optional<SymbolId> symbol,
                                       std::vector<TypePtr> type_args) {
    return intern(TypeKey{
        .kind = TypeKind::EnumVariant,
        .name = std::move(canonical_name),
        .variant_name = std::move(variant_name),
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .nominal_symbol = symbol,
        .type_var_index = std::nullopt,
        .type_args = std::move(type_args),
    });
}

// ---------------------------------------------------------------------------
// Fn type interning (separate pool: variable child count + effect)
// ---------------------------------------------------------------------------

std::size_t TypeContext::FnKeyHash::operator()(const FnKey &key) const noexcept {
    auto seed = std::hash<std::size_t>{}(key.params.size());
    for (const auto *param : key.params) {
        seed = hash_mix(seed, std::hash<const void *>{}(param));
    }
    seed = hash_mix(seed, std::hash<const void *>{}(key.return_type));
    // Hash effect kind + capability count + set of values.
    seed = hash_mix(seed, std::hash<int>{}(static_cast<int>(key.effect.kind)));
    seed = hash_mix(seed, std::hash<std::size_t>{}(key.effect.capabilities.values.size()));
    for (const auto &cap : key.effect.capabilities.values) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(cap));
    }
    return seed;
}

const Type *TypeContext::intern_fn(FnKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto iter = fn_pool_.find(key); iter != fn_pool_.end()) {
        return iter->second;
    }

    auto owned = std::make_unique<Type>();
    owned->payload = build_fn_payload(key);

    const Type *raw = owned.get();
    storage_.push_back(std::move(owned));
    fn_pool_.emplace(std::move(key), raw);
    return raw;
}

types::Payload TypeContext::build_fn_payload(const FnKey &key) {
    types::FnT fn;
    fn.params = key.params;
    fn.return_type = key.return_type;
    fn.effect = key.effect;
    return fn;
}

TypePtr TypeContext::fn(std::vector<TypePtr> param_types,
                        TypePtr return_type,
                        EffectJudgement effect) {
    return intern_fn(FnKey{
        .params = std::move(param_types),
        .return_type = return_type,
        .effect = std::move(effect),
    });
}

TypePtr TypeContext::type_var(std::uint32_t index, std::string name) {
    return intern(TypeKey{
        .kind = TypeKind::TypeVar,
        .name = std::move(name),
        .variant_name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .nominal_symbol = std::nullopt,
        .type_var_index = index,
    });
}

} // namespace ahfl
