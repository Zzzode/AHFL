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

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"
#include "ahfl/compiler/semantics/types.hpp"

namespace ahfl {

struct SourceGraph;
class TypeCheckPass;
class TypeContext;

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

    // Rebuild the name→index map from the fields vector. Call after the
    // fields vector is fully populated so find_field is O(1) amortized.
    void rebuild_field_index();

    // --- Internal cache, populated by rebuild_field_index().
    // Treated as private by convention; exposed so the type stays aggregate.
    std::unordered_map<std::string, std::size_t> field_index_;
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

    // Rebuild the name→index set from the variants vector. Call after the
    // variants vector is fully populated so has_variant is O(1) amortized.
    void rebuild_variant_index();

    // --- Internal cache, populated by rebuild_variant_index().
    // Treated as private by convention; exposed so the type stays aggregate.
    std::unordered_set<std::string> variant_set_;
};

struct ParamTypeInfo {
    std::string name;
    TypePtr type;
    SourceRange declaration_range;
};

struct CapabilityEffectTypeInfo {
    bool declared{false};
    int effect_kind{0};       // cast of ast::CapabilityEffectKind
    int receipt_mode{0};      // cast of ast::CapabilityReceiptMode
    int retry_mode{0};        // cast of ast::CapabilityRetryMode
    std::string domain;
    std::string idempotency_key;
    std::string timeout;
    std::string compensation;
    std::vector<std::string> policies;
    SourceRange source_range;
};

struct CapabilityTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange declaration_range;
    CapabilityEffectTypeInfo effect;
};

struct PredicateTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    SourceRange declaration_range;
};

struct AgentTransitionInfo {
    std::string from_state;
    std::string to_state;
};

struct AgentQuotaInfo {
    std::string name;   // e.g. "max_tool_calls", "max_execution_time"
    std::string value;  // e.g. "10", "5s"
};

struct AgentTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr context_type;
    TypePtr output_type;
    std::vector<SymbolId> capability_symbols;
    SourceRange declaration_range;

    // Structural payload fields (T1.8): populated during type-check for
    // downstream lowering without re-reading the AST.
    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<AgentTransitionInfo> transitions;
    std::vector<AgentQuotaInfo> quota;
};

// ============================================================================
// Workflow type info — expanded with node/temporal data (T1.8 Phase 2)
// ============================================================================

struct WorkflowNodeInfo {
    std::string name;
    std::string target_name;       // the spelling of the target reference
    SymbolId target_symbol{0};     // resolved symbol of target (agent)
    std::vector<std::string> after; // dependency names
    SourceRange source_range;
    SourceRange input_expr_range;  // range of the input expression (for typed expr lookup)
    SourceRange target_range;      // range of the target reference
};

struct WorkflowTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr output_type;
    SourceRange declaration_range;

    // T1.8 Phase 2: node/temporal data for typed lowering bridge
    std::vector<WorkflowNodeInfo> nodes;
    std::vector<SourceRange> safety_ranges;    // ranges of temporal formulas
    std::vector<SourceRange> liveness_ranges;  // ranges of temporal formulas
    SourceRange return_value_range;            // range of return value expression
};

// ============================================================================
// Flow type info (T1.8 Phase 2)
// ============================================================================

enum class StatePolicyKind {
    Retry,
    RetryOn,
    Timeout,
};

struct StatePolicyItemInfo {
    StatePolicyKind kind;
    std::string value;                         // retry limit or timeout duration spelling
    std::vector<std::string> retry_on_targets; // for RetryOn
};

struct FlowStateHandlerInfo {
    std::string state_name;
    std::vector<StatePolicyItemInfo> policy;
    SourceRange body_range;   // for lower_block typed bridge lookup
    SourceRange source_range;
};

struct FlowTypeInfo {
    SymbolId symbol{0};
    std::string target_name;       // spelling of target reference
    SymbolId target_symbol{0};     // resolved agent symbol
    SourceRange target_range;
    std::vector<FlowStateHandlerInfo> state_handlers;
    SourceRange declaration_range;
};

// ============================================================================
// Contract type info (T1.8 Phase 2)
// ============================================================================

struct ContractClauseInfo {
    int clause_kind;           // cast of ast::ContractClauseKind
    bool is_temporal{false};   // true if clause uses temporal_expr, false if uses expr
    SourceRange expr_range;    // range of the expression or temporal_expr
    SourceRange source_range;
};

struct ContractTypeInfo {
    SymbolId symbol{0};
    std::string target_name;
    SymbolId target_symbol{0};
    SourceRange target_range;
    std::vector<ContractClauseInfo> clauses;
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
    [[nodiscard]] std::optional<std::uint64_t> signature_fingerprint(SymbolId id) const;

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

    TypeCheckResult() {
        typed_program.type_check_result = this;
    }

    TypeCheckResult(const TypeCheckResult &other)
        : environment(other.environment), diagnostics(other.diagnostics),
          typed_program(other.typed_program), relation_trace(other.relation_trace) {
        typed_program.type_check_result = this;
    }

    TypeCheckResult(TypeCheckResult &&other) noexcept
        : environment(std::move(other.environment)), diagnostics(std::move(other.diagnostics)),
          typed_program(std::move(other.typed_program)),
          relation_trace(std::move(other.relation_trace)) {
        typed_program.type_check_result = this;
    }

    TypeCheckResult &operator=(const TypeCheckResult &other) {
        if (this == &other) {
            return *this;
        }
        environment = other.environment;
        diagnostics = other.diagnostics;
        typed_program = other.typed_program;
        relation_trace = other.relation_trace;
        typed_program.type_check_result = this;
        return *this;
    }

    TypeCheckResult &operator=(TypeCheckResult &&other) noexcept {
        if (this == &other) {
            return *this;
        }
        environment = std::move(other.environment);
        diagnostics = std::move(other.diagnostics);
        typed_program = std::move(other.typed_program);
        relation_trace = std::move(other.relation_trace);
        typed_program.type_check_result = this;
        return *this;
    }

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
    [[nodiscard]] TypeCheckResult
    check(const SourceGraph &graph, const ResolveResult &resolve_result, TypeContext &types) const;
    [[nodiscard]] TypeCheckResult check(const SourceGraph &graph,
                                        const ResolveResult &resolve_result,
                                        TypeContext &types,
                                        TypeCheckOptions options) const;
};

void dump_type_environment(const TypeEnvironment &environment,
                           const SymbolTable &symbols,
                           std::ostream &out);

} // namespace ahfl
