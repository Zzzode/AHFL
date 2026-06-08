#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

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
    [[nodiscard]] MaybeCRef<StructTypeInfo> get_struct(const Type &type) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(SymbolId id) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> get_enum(const Type &type) const;
    [[nodiscard]] MaybeCRef<StructTypeInfo> find_struct(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<EnumTypeInfo> find_enum(std::string_view canonical_name) const;
    [[nodiscard]] MaybeCRef<CapabilityTypeInfo> get_capability(SymbolId id) const;
    [[nodiscard]] MaybeCRef<PredicateTypeInfo> get_predicate(SymbolId id) const;
    [[nodiscard]] MaybeCRef<AgentTypeInfo> get_agent(SymbolId id) const;
    [[nodiscard]] MaybeCRef<WorkflowTypeInfo> get_workflow(SymbolId id) const;

    // O(1) lookup: returns true iff `id` is the symbol of any agent's context struct.
    [[nodiscard]] bool is_agent_context_struct(SymbolId id) const noexcept;

  private:
    friend class TypeChecker;
    friend class TypeCheckPass;

    // Maintain reverse name index alongside primary maps so all queries are O(1) on average.
    void index_struct(std::size_t id, StructTypeInfo info);
    void index_enum(std::size_t id, EnumTypeInfo info);
    void mark_agent_context_struct(SymbolId id);

    std::unordered_map<std::size_t, TypePtr> const_types_;
    std::unordered_map<std::size_t, StructTypeInfo> structs_;
    std::unordered_map<std::size_t, EnumTypeInfo> enums_;
    std::unordered_map<std::size_t, CapabilityTypeInfo> capabilities_;
    std::unordered_map<std::size_t, PredicateTypeInfo> predicates_;
    std::unordered_map<std::size_t, AgentTypeInfo> agents_;
    std::unordered_map<std::size_t, WorkflowTypeInfo> workflows_;

    // Reverse indices: canonical_name -> SymbolId.value, for O(1) name lookups.
    std::unordered_map<std::string, std::size_t> struct_name_index_;
    std::unordered_map<std::string, std::size_t> enum_name_index_;

    // SymbolIds of structs used as any agent's context type.
    std::unordered_set<std::size_t> agent_context_struct_ids_;
};

struct ExpressionTypeInfo {
    SourceRange range;
    std::optional<SourceId> source_id;
    // Stable AST identity (ast::ExprSyntax::node_id). 0 means "unassigned",
    // which can happen for synthesized or pre-NodeId expressions.
    std::uint64_t node_id{0};
    TypePtr type;
    ExprEffect effect{ExprEffect::Pure};
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

    // Look up by stable AST node id. Preferred over the SourceRange-based
    // overload when callers can carry the originating ast::ExprSyntax pointer
    // because two expressions with overlapping ranges (e.g. a parent and a
    // synthetic child) can be disambiguated. NodeIds are only unique within
    // the source they were minted in, so callers in multi-source projects
    // must pass the source id alongside.
    [[nodiscard]] MaybeCRef<ExpressionTypeInfo>
    find_expression_type_by_node(std::uint64_t node_id,
                                 std::optional<SourceId> source_id = std::nullopt) const;

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

    struct ExpressionTypeNodeKey {
        std::uint64_t node_id{0};
        std::optional<SourceId> source_id;

        [[nodiscard]] friend bool operator==(const ExpressionTypeNodeKey &lhs,
                                             const ExpressionTypeNodeKey &rhs) noexcept = default;
    };

    struct ExpressionTypeNodeKeyHash {
        [[nodiscard]] std::size_t operator()(const ExpressionTypeNodeKey &key) const noexcept;
    };

    void rebuild_expression_type_lookup_cache() const;
    void ensure_expression_type_lookup_cache() const;
    void invalidate_expression_type_lookup_cache() const noexcept;
    [[nodiscard]] std::vector<ExpressionTypeInfo> &mutable_expression_types() noexcept;

    std::vector<ExpressionTypeInfo> expression_types_;
    mutable std::unordered_map<ExpressionTypeLookupKey, std::size_t, ExpressionTypeLookupKeyHash>
        expression_type_lookup_cache_;
    mutable std::unordered_map<ExpressionTypeNodeKey, std::size_t, ExpressionTypeNodeKeyHash>
        expression_type_node_cache_;
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
