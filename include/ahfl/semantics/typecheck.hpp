#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ahfl/frontend/ast.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/types.hpp"
#include "ahfl/support/diagnostics.hpp"
#include "ahfl/support/ownership.hpp"
#include "ahfl/support/source.hpp"

namespace ahfl {

struct SourceGraph;
class TypeCheckPass;

struct StructFieldInfo {
    std::string name;
    TypePtr type;
    bool has_default{false};
    SourceRange declaration_range;
};

struct StructTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<StructFieldInfo> fields;
    SourceRange declaration_range;

    [[nodiscard]] MaybeCRef<StructFieldInfo> find_field(std::string_view name) const;
};

struct EnumVariantInfo {
    std::string name;
    SourceRange declaration_range;
};

struct EnumTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<EnumVariantInfo> variants;
    SourceRange declaration_range;

    [[nodiscard]] bool has_variant(std::string_view name) const noexcept;
};

struct ParamTypeInfo {
    std::string name;
    TypePtr type;
    SourceRange declaration_range;
};

struct CapabilityTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange declaration_range;
};

struct PredicateTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    SourceRange declaration_range;
};

struct AgentTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr context_type;
    TypePtr output_type;
    std::vector<SymbolId> capability_symbols;
    SourceRange declaration_range;
};

struct WorkflowTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr output_type;
    SourceRange declaration_range;
};

class TypeEnvironment {
  public:
    [[nodiscard]] const std::unordered_map<std::size_t, TypePtr> &const_types() const noexcept {
        return const_types_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, StructTypeInfo> &structs() const noexcept {
        return structs_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, EnumTypeInfo> &enums() const noexcept {
        return enums_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, CapabilityTypeInfo> &
    capabilities() const noexcept {
        return capabilities_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, PredicateTypeInfo> &
    predicates() const noexcept {
        return predicates_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, AgentTypeInfo> &agents() const noexcept {
        return agents_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, WorkflowTypeInfo> &
    workflows() const noexcept {
        return workflows_;
    }

    [[nodiscard]] MaybeCRef<Type> get_const_type(SymbolId id) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(SymbolId id) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(SymbolId id) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> find_struct(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> find_enum(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<CapabilityTypeInfo> get_capability(SymbolId id) const;
    [[nodiscard]] MaybeCRef<PredicateTypeInfo> get_predicate(SymbolId id) const;
    [[nodiscard]] MaybeCRef<AgentTypeInfo> get_agent(SymbolId id) const;
    [[nodiscard]] MaybeCRef<WorkflowTypeInfo> get_workflow(SymbolId id) const;

  private:
    friend class TypeChecker;
    friend class TypeCheckPass;

    std::unordered_map<std::size_t, TypePtr> const_types_;
    std::unordered_map<std::size_t, StructTypeInfo> structs_;
    std::unordered_map<std::size_t, EnumTypeInfo> enums_;
    std::unordered_map<std::size_t, CapabilityTypeInfo> capabilities_;
    std::unordered_map<std::size_t, PredicateTypeInfo> predicates_;
    std::unordered_map<std::size_t, AgentTypeInfo> agents_;
    std::unordered_map<std::size_t, WorkflowTypeInfo> workflows_;
};

struct ExpressionTypeInfo {
    SourceRange range;
    std::optional<SourceId> source_id;
    TypePtr type;
    bool is_pure{true};
};

struct TypeCheckResult {
    TypeEnvironment environment;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }

    [[nodiscard]] const std::vector<ExpressionTypeInfo> &expression_types() const noexcept {
        return expression_types_;
    }

    [[nodiscard]] MaybeCRef<ExpressionTypeInfo>
    find_expression_type(SourceRange range, std::optional<SourceId> source_id = std::nullopt) const;

  private:
    friend class TypeCheckPass;

    struct ExpressionTypeLookupKey {
        std::size_t begin_offset{0};
        std::size_t end_offset{0};
        std::optional<SourceId> source_id;

        [[nodiscard]] friend bool operator==(const ExpressionTypeLookupKey &lhs,
                                             const ExpressionTypeLookupKey &rhs) noexcept = default;
    };

    struct ExpressionTypeLookupKeyHash {
        [[nodiscard]] std::size_t operator()(const ExpressionTypeLookupKey &key) const noexcept;
    };

    void rebuild_expression_type_lookup_cache() const;
    void ensure_expression_type_lookup_cache() const;
    void invalidate_expression_type_lookup_cache() const noexcept;
    [[nodiscard]] std::vector<ExpressionTypeInfo> &mutable_expression_types() noexcept;

    std::vector<ExpressionTypeInfo> expression_types_;
    mutable std::unordered_map<ExpressionTypeLookupKey,
                               std::size_t,
                               ExpressionTypeLookupKeyHash>
        expression_type_lookup_cache_;
    mutable std::size_t expression_type_lookup_cache_size_{0};
    mutable const ExpressionTypeInfo *expression_type_lookup_cache_data_{nullptr};
    mutable bool expression_type_lookup_cache_valid_{false};
};

class TypeChecker {
  public:
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result) const;
};

void dump_type_environment(const TypeEnvironment &environment,
                           const SymbolTable &symbols,
                           std::ostream &out);

} // namespace ahfl
