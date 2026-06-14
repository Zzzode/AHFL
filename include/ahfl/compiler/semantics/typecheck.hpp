#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl {

struct SourceGraph;
class TypeCheckPass;
class TypeContext;

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

    [[nodiscard]] const std::unordered_map<std::size_t, FlowTypeInfo> &flows() const noexcept {
        return flows_;
    }

    [[nodiscard]] const std::unordered_map<std::size_t, ContractTypeInfo> &
    contracts() const noexcept {
        return contracts_;
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
    [[nodiscard]] MaybeCRef<FlowTypeInfo> get_flow(SymbolId id) const;
    [[nodiscard]] MaybeCRef<ContractTypeInfo> get_contract(SymbolId id) const;

    // O(1) lookup: returns true iff `id` is the symbol of any agent's context struct.
    [[nodiscard]] bool is_agent_context_struct(SymbolId id) const noexcept;

    // Compute a stable 64-bit fingerprint of a declaration's outward-facing
    // signature: a struct's field types, an enum's variants, a capability's
    // parameter / return types, etc. The fingerprint depends only on
    // hash-consed Type identities and canonical names, so two compilations
    // of an unchanged declaration always produce the same value.
    //
    // Returns std::nullopt when `id` does not correspond to a known
    // declaration in this environment. Intended consumers are incremental
    // rebuild drivers and LSP cache invalidation: when a declaration's
    // fingerprint is unchanged, downstream cached results can be reused.
    [[nodiscard]] std::optional<std::uint64_t>
    signature_fingerprint(SymbolId id) const;

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
    std::unordered_map<std::size_t, FlowTypeInfo> flows_;
    std::unordered_map<std::size_t, ContractTypeInfo> contracts_;

    // Reverse indices: canonical_name -> SymbolId.value, for O(1) name lookups.
    std::unordered_map<std::string, std::size_t> struct_name_index_;
    std::unordered_map<std::string, std::size_t> enum_name_index_;

    // SymbolIds of structs used as any agent's context type.
    std::unordered_set<std::size_t> agent_context_struct_ids_;
};

struct TypeCheckOptions {
    // Emit note-level diagnostics explaining why Optional<T> narrowing facts
    // were or were not produced from if conditions. Disabled by default so
    // normal user-facing diagnostics stay quiet.
    bool explain_narrowing{false};
    // Record relation checks performed by the type checker through
    // TypeRelationContext. Disabled by default to avoid retaining trace data in
    // normal compilation.
    bool trace_type_relations{false};
};

struct TypeCheckResult {
    TypeEnvironment environment;
    DiagnosticBag diagnostics;
    TypedProgram typed_program;
    RelationTrace relation_trace;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

class TypeChecker {
  public:
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types) const;
    [[nodiscard]] TypeCheckResult check(const ast::Program &program,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeCheckOptions options) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types,
                                        TypeCheckOptions options) const;
};

void dump_type_environment(const TypeEnvironment &environment,
                           const SymbolTable &symbols,
                           std::ostream &out);

} // namespace ahfl
