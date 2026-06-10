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
    seed = hash_mix(seed, std::hash<bool>{}(key.string_bounds.has_value()));
    if (key.string_bounds.has_value()) {
        seed = hash_mix(seed, std::hash<std::int64_t>{}(key.string_bounds->first));
        seed = hash_mix(seed, std::hash<std::int64_t>{}(key.string_bounds->second));
    }
    seed = hash_mix(seed, std::hash<bool>{}(key.decimal_scale.has_value()));
    if (key.decimal_scale.has_value()) {
        seed = hash_mix(seed, std::hash<std::int64_t>{}(*key.decimal_scale));
    }
    seed = hash_mix(seed, std::hash<const void *>{}(key.first));
    seed = hash_mix(seed, std::hash<const void *>{}(key.second));
    seed = hash_mix(seed, std::hash<bool>{}(key.nominal_symbol.has_value()));
    if (key.nominal_symbol.has_value()) {
        seed = hash_mix(seed, std::hash<std::size_t>{}(key.nominal_symbol->value));
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
        return types::StructT{.canonical_name = key.name, .symbol = key.nominal_symbol};
    case TypeKind::Enum:
        return types::EnumT{.canonical_name = key.name, .symbol = key.nominal_symbol};
    case TypeKind::Optional:
        return types::OptionalT{.inner = key.first};
    case TypeKind::List:
        return types::ListT{.element = key.first};
    case TypeKind::Set:
        return types::SetT{.element = key.first};
    case TypeKind::Map:
        return types::MapT{.key = key.first, .value = key.second};
    }

    return types::AnyT{};
}

TypePtr TypeContext::make(TypeKind kind) {
    return intern(TypeKey{
        .kind = kind,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = nullptr,
        .second = nullptr,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::string() {
    return make(TypeKind::String);
}

TypePtr TypeContext::bounded_string(std::int64_t minimum, std::int64_t maximum) {
    return intern(TypeKey{
        .kind = TypeKind::BoundedString,
        .name = {},
        .string_bounds = std::make_pair(minimum, maximum),
        .decimal_scale = std::nullopt,
        .first = nullptr,
        .second = nullptr,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::decimal(std::int64_t scale) {
    return intern(TypeKey{
        .kind = TypeKind::Decimal,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = scale,
        .first = nullptr,
        .second = nullptr,
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
    return intern(TypeKey{
        .kind = TypeKind::Struct,
        .name = std::move(canonical_name),
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = nullptr,
        .second = nullptr,
        .nominal_symbol = symbol,
    });
}

TypePtr TypeContext::enum_type(std::string canonical_name) {
    return enum_type(std::move(canonical_name), std::nullopt);
}

TypePtr TypeContext::enum_type(std::string canonical_name, SymbolId symbol) {
    return enum_type(std::move(canonical_name), std::optional<SymbolId>{symbol});
}

TypePtr TypeContext::enum_type(std::string canonical_name, std::optional<SymbolId> symbol) {
    return intern(TypeKey{
        .kind = TypeKind::Enum,
        .name = std::move(canonical_name),
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = nullptr,
        .second = nullptr,
        .nominal_symbol = symbol,
    });
}

TypePtr TypeContext::optional(TypePtr value_type) {
    return intern(TypeKey{
        .kind = TypeKind::Optional,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = value_type,
        .second = nullptr,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::list(TypePtr element_type) {
    return intern(TypeKey{
        .kind = TypeKind::List,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = element_type,
        .second = nullptr,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::set(TypePtr element_type) {
    return intern(TypeKey{
        .kind = TypeKind::Set,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = element_type,
        .second = nullptr,
        .nominal_symbol = std::nullopt,
    });
}

TypePtr TypeContext::map(TypePtr key_type, TypePtr value_type) {
    return intern(TypeKey{
        .kind = TypeKind::Map,
        .name = {},
        .string_bounds = std::nullopt,
        .decimal_scale = std::nullopt,
        .first = key_type,
        .second = value_type,
        .nominal_symbol = std::nullopt,
    });
}

} // namespace ahfl
