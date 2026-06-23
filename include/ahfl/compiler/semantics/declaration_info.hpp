#pragma once

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/effect_judgement.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward declarations for AST where-clause nodes. Full definitions live in
// ahfl/compiler/frontend/ast.hpp. We only need a raw pointer here so the
// dependency can stay one-way.
namespace ahfl::ast {
struct WhereClauseSyntax;
struct WhereBoundSyntax;
} // namespace ahfl::ast

namespace ahfl {

/// Placeholder for a single resolved where-bound entry. Currently only mirrors
/// the raw AST bound identity (subject + trait names). Real semantic resolution
/// (symbol lookup for the type subject and trait references, subtype checking,
/// trait inheritance, etc.) happens in a later task. This struct exists purely
/// so downstream code can iterate over a where clause's bounds without poking
/// back into the AST.
struct WhereBoundInfo {
    std::string subject_name;   // raw spelling of the subject type parameter
    std::vector<std::string> trait_names; // raw trait / capability spellings
    SourceRange source_range;
};

/// Bundled where-clause info attached to nominal declarations that may carry
/// generic constraints. Holds both a back-pointer to the raw AST (for
/// diagnostic ranges during checking) and a lightweight semantic-side bound
/// list that downstream consumers can read without AST linkage.
struct WhereClauseInfo {
    const ast::WhereClauseSyntax* syntax{nullptr}; // raw AST pointer (non-owning)
    std::vector<WhereBoundInfo> bounds;            // resolved bound placeholder
};

struct ModuleDeclInfo {
    std::string name;
    SourceRange declaration_range;
};

struct ImportDeclInfo {
    std::string target_module;
    std::string alias;
    SourceRange declaration_range;
};

struct ConstDeclInfo {
    std::string canonical_name;
    std::string local_name;
    TypePtr type;
    SourceRange type_range;
    SourceRange value_range;
    SourceRange declaration_range;
};

struct TypeAliasDeclInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::string local_name;
    // Generic type-parameter names in declaration order. Empty for monomorphic
    // type aliases. Used by the type resolver to resolve type variables inside
    // the aliased type, and by instantiation to match type_args by position.
    std::vector<std::string> type_param_names;
    TypePtr aliased_type;
    SourceRange aliased_type_range;
    SourceRange declaration_range;
};

struct StructFieldInfo {
    std::string name;
    TypePtr type;
    bool has_default{false};
    SourceRange default_value_range;
    SourceRange declaration_range;
};

struct StructTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    // Generic type-parameter names in declaration order. Empty for monomorphic
    // structs. The indices match positions in the struct's type_args vector
    // (see types::StructT). Resolved by the typecheck pass during struct
    // declaration building; used by the type resolver to resolve type
    // variables inside field types and by monomorphization to align arguments.
    std::vector<std::string> type_param_names;
    std::vector<StructFieldInfo> fields;
    WhereClauseInfo where_clause;
    SourceRange declaration_range;

    [[nodiscard]] MaybeCRef<StructFieldInfo> find_field(std::string_view name) const;
    void rebuild_field_index();

    std::unordered_map<std::string, std::size_t> field_index_;
};

struct EnumVariantInfo {
    std::string name;
    // P1 (ADT, RFC §1.5): positional tuple payload types. Empty for the legacy
    // payload-less `enumDecl: IDENT` form, preserving full backward compatibility.
    // Resolved once by the typecheck pass (see build_enum_types) and consumed by
    // match arm narrowing (binding positions to payload slot types).
    std::vector<TypePtr> payload;
    SourceRange declaration_range;
};

struct EnumTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    // Generic type-parameter names in declaration order. Empty for monomorphic
    // enums. Indices align with enum instantiation type_args positions
    // (see types::EnumT). Resolved by the typecheck pass; used by type
    // variable resolution inside variant payload types and by monomorphization.
    std::vector<std::string> type_param_names;
    std::vector<EnumVariantInfo> variants;
    WhereClauseInfo where_clause;
    SourceRange declaration_range;

    [[nodiscard]] bool has_variant(std::string_view name) const noexcept;
    // P1 (ADT): linear lookup used by the match typecheck pass. The variant
    // count is small (an enum rarely has more than a handful of variants), so
    // no hash index is built — callers iterate at most once per match arm.
    [[nodiscard]] MaybeCRef<EnumVariantInfo> find_variant(std::string_view name) const;
    void rebuild_variant_index();

    std::unordered_set<std::string> variant_set_;
};

struct ParamTypeInfo {
    std::string name;
    TypePtr type;
    SourceRange declaration_range;
};

struct CapabilityEffectTypeInfo {
    bool declared{false};
    int effect_kind{0};
    int receipt_mode{0};
    int retry_mode{0};
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
    WhereClauseInfo where_clause;
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
    std::string name;
    std::string value;
};

struct AgentTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr context_type;
    TypePtr output_type;
    std::vector<SymbolId> capability_symbols;
    SourceRange declaration_range;
    SourceRange input_type_range;
    SourceRange context_type_range;
    SourceRange output_type_range;

    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<AgentTransitionInfo> transitions;
    std::vector<AgentQuotaInfo> quota;
};

struct WorkflowNodeInfo {
    std::string name;
    std::string target_name;
    SymbolId target_symbol{0};
    std::vector<std::string> after;
    SourceRange source_range;
    SourceRange input_expr_range;
    SourceRange target_range;
};

struct WorkflowTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr output_type;
    SourceRange declaration_range;
    SourceRange input_type_range;
    SourceRange output_type_range;

    std::vector<WorkflowNodeInfo> nodes;
    std::vector<SourceRange> safety_ranges;
    std::vector<SourceRange> liveness_ranges;
    SourceRange return_value_range;
};

enum class StatePolicyKind {
    Retry,
    RetryOn,
    Timeout,
};

struct StatePolicyItemInfo {
    StatePolicyKind kind;
    std::string value;
    std::vector<std::string> retry_on_targets;
};

struct FlowStateHandlerInfo {
    std::string state_name;
    std::vector<StatePolicyItemInfo> policy;
    SourceRange body_range;
    SourceRange source_range;
};

struct FlowTypeInfo {
    SymbolId symbol{0};
    std::string target_name;
    SymbolId target_symbol{0};
    SourceRange target_range;
    std::vector<FlowStateHandlerInfo> state_handlers;
    SourceRange declaration_range;
};

struct ContractClauseInfo {
    int clause_kind{0};
    bool is_temporal{false};
    SourceRange expr_range;
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

// P2 (RFC §2): the three-state effect clause on a fn declaration
// (Pure / Nondet / Capability-list). The canonical kind is stored as an int
// mirroring the ast::EffectClauseKind enum so this header does not need to
// pull in the AST; the named capability symbol ids are captured by the
// resolver pass for the declared capabilities.
struct FnEffectClauseInfo {
    // 0 = Pure, 1 = Nondet, 2 = Capability (mirrors ast::EffectClauseKind).
    int kind{0};
    std::vector<SymbolId> capabilities;
    SourceRange source_range;
    // P4a (RFC corelib-effect-system.zh.md §2.2 / §6.1): the signature-level
    // effect judgement projected from this clause. Built by build_fn_types /
    // resolve_effect_clause_info; consumed by the fn-call effect derivation
    // and the verified-subset check. Defaults to Pure (matches kind==0).
    EffectJudgement judgement{EffectJudgement::make_pure()};
    // P4a (RFC §3.1 / §3.4): whether a `decreases` measure was present on the
    // clause. Pure functions require one; non-Pure functions may omit it.
    bool has_decreases{false};
};

// P2 (RFC §3.2.2 / §3.2.3 / §2 / §6): declaration-level signature of a
// top-level `fn`. Carries the resolved param/return types, the generic
// parameter names (instantiation happens at call sites in the typecheck
// pass), and the resolved effect clause. Body typecheck and
// where-clause bound evaluation are part of the FnDecl typecheck pass.
//
// P5 (RFC §3.3 / corelib-stdlib-api.zh.md §5): `builtin_name` is the
// @builtin hook name when the fn is declared with @builtin("name").
// Empty (nullopt) for normal user functions. Only stdlib modules may
// declare @builtin functions.
struct FnTypeInfo {
    SymbolId symbol{0};
    std::string canonical_name;
    std::string local_name;
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange return_type_range;
    // Generic type-parameter names in declaration order. The bound list and
    // where-clause constraints are validated by the typecheck pass; the
    // resolved types here are kept shallow so monomorphization (RFC §5) can
    // substitute type_args into a known parameter-name list without
    // re-parsing the AST.
    std::vector<std::string> type_param_names;
    FnEffectClauseInfo effect;
    bool has_body{false};
    SourceRange declaration_range;
    std::optional<std::string> builtin_name; // P5: @builtin hook name, nullopt if not a builtin
    // P2b (RFC §3.2.3): index of the function body's TypedBlock in
    // TypedProgram::blocks. UINT32_MAX when the function has no body or when
    // body type-checking has not been performed yet. Populated by FnSema.
    std::uint32_t body_block_index{UINT32_MAX};
};

// ============================================================================
// P3 (RFC §3.2.2 / type-system §1.3 / §1.4) trait & impl declaration info
// ============================================================================
//
// These mirror the FnTypeInfo pattern: resolved shallow type information keyed
// by SymbolId. The trait method/impl method signatures reuse ParamTypeInfo +
// FnEffectClauseInfo so the same signature-matching helpers work on both
// trait-declared and impl-provided methods. Impl method bodies are checked by
// ImplSema after all signatures are available, mirroring the FnSema split for
// top-level function declarations.

/// One method signature declared inside a `trait` block (RFC §1.3 TraitFnItem).
/// Trait methods carry no body (interface-only in P3).
struct TraitMethodInfo {
    std::string name;
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange return_type_range;
    std::vector<std::string> type_param_names;
    FnEffectClauseInfo effect;
    SourceRange declaration_range;
};

/// One associated type declared inside a `trait` block (RFC §1.3 AssocTypeItem).
struct TraitAssocTypeInfo {
    std::string name;
    std::vector<std::string> type_param_names;
    // Optional default type (`type A = T;`). nullptr when absent.
    TypePtr default_type;
    SourceRange declaration_range;
};

/// Resolved trait declaration (RFC §3.2.2 / type-system §1.3). `super_traits`
/// holds the resolved super-trait symbol ids; `self_type_param_name` is the
/// implicit `Self` (P3 stores the name so typecheck can substitute the impl's
/// target type). Items carry the method + assoc-type surface in source order.
struct TraitTypeInfo {
    SymbolId symbol{0};
    std::string canonical_name;
    std::string local_name;
    std::vector<std::string> type_param_names;
    std::vector<SymbolId> super_traits;
    std::vector<TraitMethodInfo> methods;
    std::vector<TraitAssocTypeInfo> assoc_types;
    SourceRange declaration_range;

    // Linear lookups (small item count per trait, mirrors EnumTypeInfo).
    [[nodiscard]] MaybeCRef<TraitMethodInfo> find_method(std::string_view name) const;
    [[nodiscard]] MaybeCRef<TraitAssocTypeInfo> find_assoc_type(std::string_view name) const;
};

/// One associated type assignment inside an `impl` block
/// (`type A = T;`, RFC §1.4 AssocItemDef).
struct ImplAssocItemInfo {
    std::string name;
    TypePtr type;
    SourceRange declaration_range;
};

/// One method defined inside an `impl` block (RFC §1.4 FnDef). Impl methods do
/// not have standalone public Function symbols yet; call-site dispatch resolves
/// through the owning ImplTypeInfo plus method name. The resolved signature
/// mirrors TraitMethodInfo so signature-matching is structural compare.
struct ImplMethodInfo {
    std::string name;
    SymbolId symbol{0};
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange return_type_range;
    std::vector<std::string> type_param_names;
    FnEffectClauseInfo effect;
    bool has_body{false};
    SourceRange declaration_range;
    // P3c: index of the method body's TypedBlock in TypedProgram::blocks.
    // UINT32_MAX when the method has no body or body checking failed before a
    // block was recorded. Populated by ImplSema after the environment is built.
    std::uint32_t body_block_index{UINT32_MAX};
};

/// Resolved impl block (RFC §3.2.2 / type-system §1.4). `is_inherent` is true
/// when the impl has no `TraitRef for` (inherent impl). `trait_symbol` is the
/// resolved trait (nullopt for inherent impls). `target_type` is the resolved
/// impl target; `target_symbol` is the nominal symbol of the target type when
/// it resolves to a struct/enum (used by the coherence/orphan-rule check).
struct ImplTypeInfo {
    // Index in the source declaration order; impls have no user-facing name.
    std::size_t index{0};
    bool is_inherent{false};
    std::optional<SymbolId> trait_symbol;
    std::string trait_name;
    TypePtr target_type;
    std::optional<SymbolId> target_symbol;
    std::vector<std::string> type_param_names;
    std::vector<ImplMethodInfo> methods;
    std::vector<ImplAssocItemInfo> assoc_items;
    SourceRange declaration_range;
    SourceRange trait_ref_range;
    SourceRange target_type_range;
    // P3 (RFC §2.2 coherence): source unit the impl lives in, so the
    // orphan-rule check can resolve the impl's defining module without a
    // symbol. Resolved to a module name by the typecheck pass via
    // SourceGraph::sources.
    std::optional<SourceId> source_id;
    // Module name the impl is declared in (resolved at typecheck time from
    // source_id). Empty when the impl is in the anonymous top-level program.
    std::string module_name;
};

} // namespace ahfl
