#pragma once

#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl {

class TypeContext {
  public:
    TypeContext() = default;
    TypeContext(const TypeContext &) = delete;
    TypeContext &operator=(const TypeContext &) = delete;
    TypeContext(TypeContext &&) = delete;
    TypeContext &operator=(TypeContext &&) = delete;
    ~TypeContext() = default;

    [[nodiscard]] TypePtr make(TypeKind kind);
    [[nodiscard]] TypePtr string();
    [[nodiscard]] TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum);
    [[nodiscard]] TypePtr decimal(std::int64_t scale);

    [[nodiscard]] TypePtr struct_type(std::string canonical_name);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name, std::optional<SymbolId> symbol);

    [[nodiscard]] TypePtr enum_type(std::string canonical_name);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name, std::optional<SymbolId> symbol);

    [[nodiscard]] TypePtr optional(TypePtr value_type);
    [[nodiscard]] TypePtr list(TypePtr element_type);
    [[nodiscard]] TypePtr set(TypePtr element_type);
    [[nodiscard]] TypePtr map(TypePtr key_type, TypePtr value_type);

    [[nodiscard]] static TypeContext &global();

  private:
    struct TypeKey {
        TypeKind kind{TypeKind::Any};
        std::string name;
        std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
        std::optional<std::int64_t> decimal_scale;
        const Type *first{nullptr};
        const Type *second{nullptr};
        std::optional<SymbolId> nominal_symbol;

        [[nodiscard]] friend bool operator==(const TypeKey &lhs,
                                             const TypeKey &rhs) noexcept = default;
    };

    struct TypeKeyHash {
        [[nodiscard]] std::size_t operator()(const TypeKey &key) const noexcept;
    };

    [[nodiscard]] const Type *intern(TypeKey key);
    [[nodiscard]] static types::Payload build_payload(const TypeKey &key);

    std::mutex mutex_;
    std::vector<std::unique_ptr<Type>> storage_;
    std::unordered_map<TypeKey, const Type *, TypeKeyHash> pool_;
};

} // namespace ahfl
