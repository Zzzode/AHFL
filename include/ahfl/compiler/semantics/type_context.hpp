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
    [[nodiscard]] TypePtr error_type();
    [[nodiscard]] TypePtr string();
    [[nodiscard]] TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum);
    [[nodiscard]] TypePtr decimal(std::int64_t scale);

    [[nodiscard]] TypePtr struct_type(std::string canonical_name);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name, std::optional<SymbolId> symbol);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name,
                                      SymbolId symbol,
                                      std::vector<TypePtr> type_args);
    [[nodiscard]] TypePtr struct_type(std::string canonical_name,
                                      std::optional<SymbolId> symbol,
                                      std::vector<TypePtr> type_args);

    [[nodiscard]] TypePtr enum_type(std::string canonical_name);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name, SymbolId symbol);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name, std::optional<SymbolId> symbol);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name,
                                    SymbolId symbol,
                                    std::vector<TypePtr> type_args);
    [[nodiscard]] TypePtr enum_type(std::string canonical_name,
                                    std::optional<SymbolId> symbol,
                                    std::vector<TypePtr> type_args);
    [[nodiscard]] TypePtr enum_variant_type(std::string canonical_name, std::string variant_name);
    [[nodiscard]] TypePtr
    enum_variant_type(std::string canonical_name, std::string variant_name, SymbolId symbol);
    [[nodiscard]] TypePtr enum_variant_type(std::string canonical_name,
                                            std::string variant_name,
                                            std::optional<SymbolId> symbol);
    [[nodiscard]] TypePtr enum_variant_type(std::string canonical_name,
                                            std::string variant_name,
                                            SymbolId symbol,
                                            std::vector<TypePtr> type_args);
    [[nodiscard]] TypePtr enum_variant_type(std::string canonical_name,
                                            std::string variant_name,
                                            std::optional<SymbolId> symbol,
                                            std::vector<TypePtr> type_args);

    [[nodiscard]] TypePtr fn(std::vector<TypePtr> param_types,
                             TypePtr return_type,
                             EffectJudgement effect);

    // P2 (RFC §5): create a named type variable. Used inside generic
    // declarations as a placeholder; substituted with a concrete type during
    // monomorphization. `index` is the zero-based position in the enclosing
    // declaration's type-parameter list — used as the substitution key
    // (index-based O(1) lookup, not string hashing).
    [[nodiscard]] TypePtr type_var(std::uint32_t index, std::string name);

    [[nodiscard]] static TypeContext &global();

  private:
    struct TypeKey {
        TypeKind kind{TypeKind::Any};
        std::string name;
        std::string variant_name;
        std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds;
        std::optional<std::int64_t> decimal_scale;
        std::optional<SymbolId> nominal_symbol;
        // P2: TypeVar's zero-based index within the enclosing generic
        // declaration's type-parameter list. Nullopt for all non-TypeVar kinds.
        // Using index as part of the interning key follows industry-standard
        // practice (Rust Substs position, Swift GenericTypeParamKey depth+index):
        // substitution is O(1) position-based lookup, not string-hash based.
        // The `name` field carries the user-visible label for diagnostics only.
        std::optional<std::uint32_t> type_var_index;
        // P2 (RFC §5): concrete type arguments of a generic nominal type
        // instantiation (struct/enum). Empty vector for monomorphic types.
        //
        // These are the *actual* type arguments of a concrete instantiation
        // (e.g. `Vec<Int>` has one type_arg: Int), not the parameter names of
        // the generic definition. They participate in structural interning: two
        // nominal types with the same name and the same type_arg pointers are
        // the same type (pointer-equal).
        //
        // Design: kept on TypeKey (and thus replicated in every interned
        // Struct/Enum/EnumVariant type) rather than on a separate key struct
        // because the number of unique instantiations per definition is
        // typically small (monomorphization budget is in the hundreds), and a
        // single key keeps the interning model uniform.
        std::vector<const Type *> type_args;

        [[nodiscard]] friend bool operator==(const TypeKey &lhs,
                                             const TypeKey &rhs) noexcept = default;
    };

    struct TypeKeyHash {
        [[nodiscard]] std::size_t operator()(const TypeKey &key) const noexcept;
    };

    // Fn types are structural and have a variable number of child types plus
    // an effect judgement. They use a separate key/pool so the flat TypeKey
    // stays lean for the majority of type kinds.
    struct FnKey {
        std::vector<const Type *> params;
        const Type *return_type{nullptr};
        EffectJudgement effect;

        [[nodiscard]] friend bool operator==(const FnKey &lhs,
                                             const FnKey &rhs) noexcept {
            if (lhs.params.size() != rhs.params.size()) {
                return false;
            }
            for (std::size_t i = 0; i < lhs.params.size(); ++i) {
                if (lhs.params[i] != rhs.params[i]) {
                    return false;
                }
            }
            if (lhs.return_type != rhs.return_type) {
                return false;
            }
            return lhs.effect == rhs.effect;
        }
    };

    struct FnKeyHash {
        [[nodiscard]] std::size_t operator()(const FnKey &key) const noexcept;
    };

    [[nodiscard]] const Type *intern(TypeKey key);
    [[nodiscard]] const Type *intern_fn(FnKey key);
    [[nodiscard]] static types::Payload build_payload(const TypeKey &key);
    [[nodiscard]] static types::Payload build_fn_payload(const FnKey &key);

    std::mutex mutex_;
    std::vector<std::unique_ptr<Type>> storage_;
    std::unordered_map<TypeKey, const Type *, TypeKeyHash> pool_;
    std::unordered_map<FnKey, const Type *, FnKeyHash> fn_pool_;
};

} // namespace ahfl
