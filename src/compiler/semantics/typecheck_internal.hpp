#pragma once

// Internal header (not in public include/) shared by typecheck.cpp,
// typecheck_decls.cpp, typecheck_expr.cpp, and future split implementation files. It exposes
// the TypeCheckPass class plus the shared helper types so a single class
// definition can be implemented across multiple translation units, mirroring
// LLVM/Clang Sema's split between SemaDecl/SemaExpr/SemaStmt while keeping
// state on a single class.

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/const_sema.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/expression_sema.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/type_expectation.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/type_resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/compiler/semantics/typed_hir.hpp"

namespace ahfl {

namespace internal {

using BindingMap = ExpressionBindingMap;
using CallContext = ExpressionCallContext;
using TypedValue = ExpressionValue;
using ConstEvalResult = ConstExpressionResult;
using ValueContext = ExpressionContext;

[[nodiscard]] inline bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] inline bool is_error_type(const Type &type) noexcept {
    return type.holds<types::ErrorT>();
}

[[nodiscard]] inline bool is_bool_type(const Type &type) noexcept {
    return type.holds<types::BoolT>();
}

[[nodiscard]] inline bool is_numeric_type(const Type &type) noexcept {
    return type.holds<types::IntT>() || type.holds<types::FloatT>() ||
           type.holds<types::DecimalT>();
}

[[nodiscard]] inline MaybeCRef<Type> find_binding(const BindingMap &bindings,
                                                  std::string_view name) {
    if (const auto iter = bindings.find(std::string(name));
        iter != bindings.end() && static_cast<bool>(iter->second)) {
        return std::cref(*iter->second);
    }

    return std::nullopt;
}

template <typename T>
[[nodiscard]] MaybeCRef<T>
find_decl_ref(const std::unordered_map<std::size_t, std::reference_wrapper<const T>> &map,
              SymbolId id) {
    if (const auto iter = map.find(id.value); iter != map.end()) {
        return std::cref(iter->second.get());
    }

    return std::nullopt;
}

// ============================================================================
// Expression syntax visitor
// ============================================================================
//
// The actual visit_expr_syntax dispatch lives in ahfl::ast (see ast.hpp) so
// it can be shared across passes. This re-export merely exposes it under the
// `internal` alias used by the split typecheck implementation files.

using ast::visit_expr_syntax;

} // namespace internal

struct TypeCheckSession {
    const ast::Program *program{nullptr};
    const SourceGraph *graph{nullptr};
    const ResolveResult &resolve_result;
    TypeContext &types;
    TypeCheckOptions options;

    TypeCheckSession(const ast::Program *program_arg,
                     const SourceGraph *graph_arg,
                     const ResolveResult &resolve_arg,
                     TypeContext &types_arg,
                     TypeCheckOptions options_arg)
        : program(program_arg), graph(graph_arg), resolve_result(resolve_arg), types(types_arg),
          options(options_arg) {}
};

struct TypeCheckState {
    TypeCheckResult result;
    const SourceUnit *current_source{nullptr};
    std::optional<SourceId> current_source_id;
    std::string current_module_name;
    std::unordered_map<std::size_t, const SourceUnit *> source_units_by_id;

    void build_source_unit_index(const SourceGraph *graph);
    void enter_source(const SourceUnit &source);
    void leave_source();
    [[nodiscard]] MaybeCRef<SourceUnit> source_unit_for(const SourceGraph *graph,
                                                        SourceId id) const;
};

class DiagnosticReporter {
  public:
    DiagnosticReporter(DiagnosticBag &diagnostics, const SourceUnit *const &current_source)
        : diagnostics_(&diagnostics), current_source_(&current_source) {}

    void error(std::string message, SourceRange range);
    void note(std::string message, SourceRange range);
    void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                         std::string message,
                         SourceRange range);
    void typecheck_error(ErrorCode<DiagnosticCategory::TypeCheck> code,
                         std::string message,
                         SourceRange range,
                         std::vector<Diagnostic::Related> notes);

  private:
    DiagnosticBag *diagnostics_{nullptr};
    const SourceUnit *const *current_source_{nullptr};
};

struct DeclarationIndex {
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TypeAliasDecl>>
        type_alias_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::ConstDecl>> const_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::StructDecl>> struct_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::EnumDecl>> enum_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::CapabilityDecl>>
        capability_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::PredicateDecl>>
        predicate_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::AgentDecl>> agent_decls;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::WorkflowDecl>> workflow_decls;
    // P2 (RFC §3.2.2): top-level fn declarations keyed by Function symbol id.
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::FnDecl>> fn_decls;
    // P3 (RFC §3.2.2 / type-system §1.3): top-level trait declarations keyed
    // by Trait symbol id.
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TraitDecl>> trait_decls;
    // P3 (RFC §3.2.2 / type-system §1.4): all impl blocks in source order,
    // keyed by their source-order index. Impl blocks have no user-facing name.
    // Each entry carries its source-id alongside the AST reference so the
    // coherence check can resolve the impl's defining module without a symbol.
    struct ImplDeclEntry {
        std::reference_wrapper<const ast::ImplDecl> decl;
        std::optional<SourceId> source_id;
    };
    std::unordered_map<std::size_t, ImplDeclEntry> impl_decls;
};

class TypeCheckPass;
class TypedHirBuilder;

class DeclarationIndexBuilder {
  public:
    DeclarationIndexBuilder(const TypeCheckSession &session,
                            TypeCheckState &state,
                            DeclarationIndex &index,
                            TypedHirBuilder &hir)
        : session_(&session), state_(&state), index_(&index), hir_(&hir) {}

    void run();

  private:
    const TypeCheckSession *session_{nullptr};
    TypeCheckState *state_{nullptr};
    DeclarationIndex *index_{nullptr};
    TypedHirBuilder *hir_{nullptr};

    void index_program_declarations(const ast::Program &program);
    [[nodiscard]] MaybeCRef<Symbol> find_local(SymbolNamespace name_space,
                                               std::string_view name) const;
};

struct DeclarationPayloadUpdate {
    std::size_t declaration_index{0};
    TypePtr type{nullptr};
    TypedDeclPayload payload;
};

struct EnvironmentBuildResult {
    TypeEnvironment environment;
    std::vector<DeclarationPayloadUpdate> declaration_updates;
};

class EnvironmentBuilder {
  public:
    explicit EnvironmentBuilder(TypeCheckPass &driver) : driver_(&driver) {}

    [[nodiscard]] EnvironmentBuildResult run();

  private:
    TypeCheckPass *driver_{nullptr};
};

class ConstSema {
  public:
    explicit ConstSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};
    std::unordered_map<std::size_t, ConstValue> const_values_;
    std::unordered_set<std::size_t> active_const_values_;
    std::unordered_set<std::size_t> failed_const_values_;

    void remember_const_value(const ast::ExprSyntax &expr, const ConstValue &value);
    [[nodiscard]] bool ensure_const_value(SymbolId id, SourceRange use_range);
    [[nodiscard]] internal::ConstEvalResult
    check_const_expr(const ast::ExprSyntax &expr,
                     const internal::ValueContext &context,
                     MaybeCRef<Type> expected_type,
                     std::string_view context_label,
                     std::optional<SymbolId> source_const = std::nullopt);
    void check_const_initializers_in_program(const ast::Program &program);
    void check_const_initializers();
    void check_struct_defaults();
    void check_agent_context_defaults();
};

class ContractSema {
  public:
    explicit ContractSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};

    void check_contracts_in_program(const ast::Program &program);
    void check_contracts();
};

class FlowSema {
  public:
    explicit FlowSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};

    void check_flows_in_program(const ast::Program &program);
    void check_flows();
};

class WorkflowSema {
  public:
    explicit WorkflowSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};

    void check_workflows_in_program(const ast::Program &program);
    void check_workflows();
};

// P2b (RFC §3.2.3): function-body type-check pass. Walks all top-level `fn`
// declarations that have a body, builds a parameter-binding context, and
// type-checks the body block. Also computes the body's overall effect and
// enforces the effect-underdeclared check.
class FnSema {
  public:
    explicit FnSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};

    void check_fns();
    void check_fns_in_program(const ast::Program &program);
    void check_fn_body(SymbolId fn_symbol, const ast::FnDecl &decl);
};

// P3c: impl-method body type-check pass. Signatures are resolved during
// EnvironmentBuilder::build_impl_types so method calls and trait signature
// matching can see every impl before bodies are walked. This pass mirrors
// FnSema for the method bodies themselves.
class ImplSema {
  public:
    explicit ImplSema(TypeCheckPass &driver) : driver_(&driver) {}

    void run();

  private:
    TypeCheckPass *driver_{nullptr};

    void check_impls();
    void check_impl_body(std::size_t impl_index,
                         const ast::ImplDecl &decl,
                         const ImplTypeInfo &impl_info);
    void check_impl_method_body(std::size_t impl_index,
                                const ast::FnDecl &method_decl,
                                const ImplTypeInfo &impl_info,
                                const ImplMethodInfo &method_info);
    void record_impl_method_body_index(std::size_t impl_index,
                                       std::string_view method_name,
                                       std::uint32_t body_block_index);
};

class TypedHirBuilder {
  public:
    explicit TypedHirBuilder(TypedProgram &program) : program_(&program) {}

    [[nodiscard]] TypedProgram &program() noexcept {
        return *program_;
    }

    void append_declaration(TypedDecl decl);
    void append_expression(TypedExpr expr);
    [[nodiscard]] std::uint32_t append_temporal_expr(TypedTemporalExpr expr);
    [[nodiscard]] std::uint32_t append_block(TypedBlock block);
    [[nodiscard]] std::uint32_t append_statement(TypedStatement statement);
    void apply_declaration_payload_updates(std::vector<DeclarationPayloadUpdate> updates);

  private:
    TypedProgram *program_{nullptr};
};

class TypeCheckPass final {
  public:
    explicit TypeCheckPass(TypeCheckSession session)
        : session_(std::move(session)), state_(), program_(session_.program),
          graph_(session_.graph), resolve_result_(session_.resolve_result), types_(&session_.types),
          options_(session_.options), result_(state_.result),
          environment_(&state_.result.environment), current_source_(state_.current_source),
          current_source_id_(state_.current_source_id),
          current_module_name_(state_.current_module_name),
          source_units_by_id_(state_.source_units_by_id),
          reporter_(state_.result.diagnostics, state_.current_source),
          type_alias_decls_(declaration_index_.type_alias_decls),
          const_decls_(declaration_index_.const_decls),
          struct_decls_(declaration_index_.struct_decls),
          enum_decls_(declaration_index_.enum_decls),
          capability_decls_(declaration_index_.capability_decls),
          predicate_decls_(declaration_index_.predicate_decls),
          agent_decls_(declaration_index_.agent_decls),
          workflow_decls_(declaration_index_.workflow_decls),
          fn_decls_(declaration_index_.fn_decls), trait_decls_(declaration_index_.trait_decls),
          impl_decls_(declaration_index_.impl_decls), hir_builder_(state_.result.typed_program) {}
    TypeCheckPass(const ast::Program &program, const ResolveResult &resolve_result)
        : TypeCheckPass(TypeCheckSession(
              &program, nullptr, resolve_result, TypeContext::global(), TypeCheckOptions{})) {}
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeCheckOptions options)
        : TypeCheckPass(
              TypeCheckSession(&program, nullptr, resolve_result, TypeContext::global(), options)) {
    }
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeContext &types)
        : TypeCheckPass(
              TypeCheckSession(&program, nullptr, resolve_result, types, TypeCheckOptions{})) {}
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeContext &types,
                  TypeCheckOptions options)
        : TypeCheckPass(TypeCheckSession(&program, nullptr, resolve_result, types, options)) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result)
        : TypeCheckPass(TypeCheckSession(
              nullptr, &graph, resolve_result, TypeContext::global(), TypeCheckOptions{})) {}
    TypeCheckPass(const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  TypeCheckOptions options)
        : TypeCheckPass(
              TypeCheckSession(nullptr, &graph, resolve_result, TypeContext::global(), options)) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result, TypeContext &types)
        : TypeCheckPass(
              TypeCheckSession(nullptr, &graph, resolve_result, types, TypeCheckOptions{})) {}
    TypeCheckPass(const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  TypeContext &types,
                  TypeCheckOptions options)
        : TypeCheckPass(TypeCheckSession(nullptr, &graph, resolve_result, types, options)) {}

    [[nodiscard]] TypeCheckResult run();

  private:
    friend class EnvironmentBuilder;
    friend class ConstSema;
    friend class ContractSema;
    friend class FlowSema;
    friend class WorkflowSema;
    friend class FnSema;
    friend class ImplSema;

    // Re-export internal aliases inside the class so existing implementation
    // files can keep referring to them by their unqualified names.
    using BindingMap = internal::BindingMap;
    using CallContext = internal::CallContext;
    using TypedValue = internal::TypedValue;
    using ConstEvalKind = ahfl::ConstEvalKind;
    using ConstEvalResult = internal::ConstEvalResult;
    using ValueContext = internal::ValueContext;

    TypeCheckSession session_;
    TypeCheckState state_;
    const ast::Program *&program_;
    const SourceGraph *&graph_;
    const ResolveResult &resolve_result_;
    TypeContext *types_{nullptr};
    TypeCheckOptions &options_;
    TypeRelationContext relations_;
    TypeCheckResult &result_;
    TypeEnvironment *environment_{nullptr};
    const SourceUnit *&current_source_;
    std::optional<SourceId> &current_source_id_;
    std::string &current_module_name_;
    std::unordered_map<std::size_t, const SourceUnit *> &source_units_by_id_;
    DiagnosticReporter reporter_;
    DeclarationIndex declaration_index_;

    // P2: currently-in-scope generic type parameter names. Nullptr when no
    // generic scope is active. Set by build_fn_types / FnSema so that
    // resolve_type produces TypeVar for names matching a type parameter.
    const std::vector<std::string> *current_type_param_names_{nullptr};

    // After the most recent check_statement call completes, holds the index
    // of the TypedStatement that was just appended to
    // result_.typed_program.statements. check_block reads this to link
    // statements into the flat block store.
    mutable std::optional<std::size_t> last_written_statement_index_;

    // Resolve a payload expression of a statement to its index in
    // result_.typed_program.expressions. Tries node_id lookup first, then
    // falls back to exact range-based lookup for synthesized expressions.
    // Returns UINT32_MAX when not found.
    [[nodiscard]] std::uint32_t
    resolve_payload_expr_index(const ast::ExprSyntax &expr) const noexcept;

    // Reverse-lookup the block index for a given AST BlockSyntax by walking
    // TypedProgram::blocks from the end. Used by If-statement wiring to avoid
    // double-push (check_block handles the push itself). Returns UINT32_MAX
    // when not found (shouldn't happen in well-formed inputs).
    [[nodiscard]] std::uint32_t
    find_block_index_by_range(const ast::BlockSyntax &block) const noexcept;

    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TypeAliasDecl>>
        &type_alias_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::ConstDecl>> &const_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::StructDecl>> &struct_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::EnumDecl>> &enum_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::CapabilityDecl>>
        &capability_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::PredicateDecl>>
        &predicate_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::AgentDecl>> &agent_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::WorkflowDecl>>
        &workflow_decls_;
    // P2 (RFC §3.2.2)
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::FnDecl>> &fn_decls_;
    // P3 (RFC §3.2.2 / type-system §1.3 / §1.4)
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TraitDecl>> &trait_decls_;
    std::unordered_map<std::size_t, DeclarationIndex::ImplDeclEntry> &impl_decls_;
    TypedHirBuilder hir_builder_;

    TypeAliasResolutionState alias_resolution_;

    [[nodiscard]] TypeEnvironment &environment() noexcept {
        return *environment_;
    }
    [[nodiscard]] const TypeEnvironment &environment() const noexcept {
        return *environment_;
    }

    // Declaration indexing & environment building (typecheck_decls.cpp).
    void build_const_types();
    void build_struct_types();
    void build_enum_types();
    void build_capability_types();
    void build_predicate_types();
    void build_agent_types();
    void build_workflow_types();
    // P2 (RFC §3.2.2): resolve fn signatures (params, return type, effect
    // clause, generic parameter names) and register FnTypeInfo in the
    // environment. Body typecheck and where-clause bound evaluation happen
    // in FnSema (separate pass).
    void build_fn_types();
    // P3 (RFC §3.2.2 / type-system §1.3): resolve trait signatures (methods,
    // assoc types, super-traits, generic params) and register TraitTypeInfo in
    // the environment keyed by Trait symbol id. Method bodies are absent by
    // definition; trait-method-call resolution happens at the typed call site
    // against the impl metadata built below.
    void build_trait_types();
    // P3 (RFC §3.2.2 / type-system §1.4): resolve each impl block — target
    // type, trait_ref (when present), method signatures, associated types —
    // and enforce coherence (orphan rule RFC §2.2), impl-vs-trait signature
    // matching, super-trait coverage, and per-trait duplicate-impl detection.
    // Impl method *bodies* are checked later by ImplSema after the full
    // environment exists; this signature pass is what dispatch and trait
    // matching need.
    void build_impl_types();
    // P3 effect-clause + trait-method signature resolvers (member functions so
    // they can reach private resolve_type / make_error_type / find_reference_here).
    FnEffectClauseInfo resolve_effect_clause_info(const Owned<ast::EffectClauseSyntax> &clause);
    // P4a (RFC §2.2 / §6.1): project an ast::EffectClauseSyntax to the
    // signature-level EffectJudgement (Pure / Nondet / CapabilitySet over
    // resolved capability symbols). Used by build_fn_types and
    // resolve_effect_clause_info to populate FnEffectClauseInfo::judgement.
    EffectJudgement build_effect_judgement(const ast::EffectClauseSyntax &clause);
    // P4a (RFC §2.6.4 / §4.5 V2): check that a function's inferred body effect
    // is covered by its declared effect (i.e. body ⊑ declared). Emits
    // EffectUnderdeclared if the body effect is stronger than declared.
    //
    // The body effect is passed as an ExprEffect (the expression-level
    // inference lattice) and projected to EffectJudgement via `project()`.
    //
    // TODO(FnSema): call this from the FnSema pass after type-checking the
    // function body. The body's overall ExprEffect is the join of all
    // statement/expression effects in the body (see check_block / check_expr).
    void check_fn_effect_underdeclared(SymbolId fn_symbol,
                                       ExprEffect body_effect,
                                       SourceRange body_range);
    TraitMethodInfo resolve_trait_method_info(const ast::TraitItemSyntax &item);
    void build_flow_types();
    void build_flow_types_in_program(const ast::Program &program);
    void build_contract_types();
    void build_contract_types_in_program(const ast::Program &program);

    // Walks a temporal expression tree, type-checking every embedded Expr
    // (EmbeddedExpr leaves) against Bool and building a parallel flat-store
    // of TypedTemporalExpr records in result_.typed_program.temporal_exprs.
    // Returns the index of the newly-created TypedTemporalExpr entry (callers
    // that don't need the index can freely ignore the return value).
    std::uint32_t check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
                                                const ValueContext &context);

    void check_block(const ast::BlockSyntax &block,
                     ValueContext &context,
                     MaybeCRef<Type> expected_return_type,
                     std::string_view state_name = "",
                     std::optional<SourceRange> expected_return_origin = std::nullopt);
    void check_statement(const ast::StatementSyntax &statement,
                         ValueContext &context,
                         MaybeCRef<Type> expected_return_type,
                         std::string_view state_name = "",
                         std::optional<SourceRange> expected_return_origin = std::nullopt);

    void enter_source(const SourceUnit &source);
    void leave_source();
    void build_source_unit_index();
    [[nodiscard]] MaybeCRef<SourceUnit> source_unit_for(SourceId id) const;
    template <typename Fn> decltype(auto) with_symbol_context(SymbolId id, Fn &&fn);
    [[nodiscard]] MaybeCRef<Symbol> find_local_here(SymbolNamespace name_space,
                                                    std::string_view name) const;
    [[nodiscard]] MaybeCRef<ResolvedReference> find_reference_here(ReferenceKind kind,
                                                                   SourceRange range) const;
    void error_here(std::string message, SourceRange range);
    void note_here(std::string message, SourceRange range);
    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range);
    void typecheck_error_here(ErrorCode<DiagnosticCategory::TypeCheck> code,
                              std::string message,
                              SourceRange range,
                              std::vector<Diagnostic::Related> notes);
    void non_pure_error_here(std::string_view context_label, ExprEffect effect, SourceRange range);
    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const;
    [[nodiscard]] TypeResolver make_type_resolver();
    [[nodiscard]] TypePtr resolve_type(const ast::TypeSyntax &type);
    // P2 (RFC §6): ExpressionSemaDelegate override — resolve a closure
    // parameter's type annotation.
    [[nodiscard]] TypePtr resolve_type_syntax(const ast::TypeSyntax &type);
    // P2c (RFC §3.5): record a resolved fn call site into the typed program
    // for the monomorphization pass. Called by PassExpressionSemaDelegate.
    void
    record_fn_call_site(SymbolId fn_symbol, SourceRange call_range, std::vector<TypePtr> type_args);
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name);
    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range);
    [[nodiscard]] TypePtr resolve_type_alias(SymbolId id, SourceRange use_range);

    // P3 (RFC §3.2.2 / type-system §1.4 / §2.2) impl-coherence helpers.
    // `nominal_symbol_of` returns the Struct/Enum SymbolId for a resolved type
    // (nullopt for non-nominal — compound types have no defining module).
    [[nodiscard]] std::optional<SymbolId> nominal_symbol_of(const Type &type) const;
    // `nominal_describe` renders a type for diagnostic messages; mirrors
    // Type::describe but stays inside typecheck_internal.cpp where it is used.
    [[nodiscard]] std::string nominal_describe(const Type &type) const;
    // `with_symbol_context_for_impl` runs `fn` with the impl block's defining
    // module as the current module (mirrors with_symbol_context but keyed by
    // the impl's source_id rather than a symbol).
    template <typename Fn>
    decltype(auto) with_symbol_context_for_impl(std::optional<SourceId> source_id, Fn &&fn);
    // Coherence (RFC §2.2 strict orphan rule): the impl must live in the
    // module that defines the trait or the module that defines the target
    // type. Diagnoses E::orphan_impl otherwise.
    void check_impl_coherence(const ImplTypeInfo &impl);
    // Trait-impl signature matching (RFC §2.1): every trait method has a
    // matching impl method with structurally-equal (params, return) types;
    // every impl method names a real trait method; every trait assoc type is
    // provided. Super-trait coverage is checked here too.
    void check_trait_impl_signature_match(const ImplTypeInfo &impl);
    // P3 (RFC §2.2) supporting helpers used by the coherence + signature
    // matcher. module_name_of resolves an impl source-id to its defining
    // module via the SourceGraph; module_of_symbol reads Symbol::module_name.
    [[nodiscard]] std::string module_name_of(std::optional<SourceId> source_id) const;
    [[nodiscard]] std::string module_of_symbol(SymbolId id) const;
    // Find an impl method by name (linear; impl methods are few).
    [[nodiscard]] MaybeCRef<ImplMethodInfo> find_impl_method(const ImplTypeInfo &impl,
                                                             std::string_view name) const;
    // Structural signature equality (params count + types, return type, effect
    // kind). Generic type-param names and where-clause constraints are not
    // compared in P3b — they are part of the trait-method-call resolution.
    [[nodiscard]] bool signatures_match(const TraitMethodInfo &trait_method,
                                        const ImplMethodInfo &impl_method) const;
    // Render a param type list for diagnostics: "Int, String".
    [[nodiscard]] std::string render_param_types(const std::vector<ParamTypeInfo> &params) const;
    // True iff some impl in the environment implements `trait_id` for nominal
    // `target_id`. Used by the super-trait coverage check (RFC §2.4).
    [[nodiscard]] bool impl_target_implements(SymbolId target_id, SymbolId trait_id) const;
    // P2d.S2 (RFC §3.5 / §2): check whether a resolved type satisfies a
    // referenced trait (i.e. there exists a trait impl in the environment for
    // this type). Returns true for nominal types with a matching impl; returns
    // false for compound / primitive types. Emits a TRAIT_BOUND_NOT_SATISFIED
    // diagnostic with the caller-supplied `range` on failure.
    //
    // Super-trait coverage is applied transitively per the trait's super_traits
    // list so a bound on a super-trait is satisfied when the target type
    // implements any sub-trait in the environment (§2.4).
    [[nodiscard]] bool check_bound(const Type &subject_type,
                                   std::string_view trait_name,
                                   SourceRange range);

    [[nodiscard]] MaybeCRef<ast::TypeAliasDecl> alias_decl_of(SymbolId id) const;

    void remember_expression_type(const ast::ExprSyntax &expr, const TypedValue &typed);
    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label);
    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label,
                                        std::optional<SourceRange> expected_origin);
    [[nodiscard]] bool check_assignable(const Type &source,
                                        const Type &target,
                                        SourceRange range,
                                        std::string_view context_label,
                                        const TypeExpectation &expectation);
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range);
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range,
                                                   std::optional<SourceRange> expected_origin);
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range,
                                                   const TypeExpectation &expectation);
    void check_schema_boundary_decl_type(const TypePtr &type,
                                         SchemaBoundaryKind boundary,
                                         SourceRange range);
    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type = std::nullopt);
    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        const TypeExpectation &expectation);
    [[nodiscard]] TypedValue check_expr_impl(const ast::ExprSyntax &expr,
                                             const ValueContext &context,
                                             MaybeCRef<Type> expected_type = std::nullopt,
                                             const TypeExpectation *expectation = nullptr);
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path, const ValueContext &context);
    [[nodiscard]] TypePtr make_any_type() const {
        return types_->make(TypeKind::Any);
    }
    [[nodiscard]] TypePtr make_error_type() const {
        return types_->error_type();
    }
    [[nodiscard]] TypePtr make_type(TypeKind kind) const {
        return types_->make(kind);
    }
    [[nodiscard]] TypePtr string_type() const {
        return types_->string();
    }
    [[nodiscard]] TypePtr bounded_string_type(std::int64_t minimum, std::int64_t maximum) const {
        return types_->bounded_string(minimum, maximum);
    }
    [[nodiscard]] TypePtr decimal_type(std::int64_t scale) const {
        return types_->decimal(scale);
    }
    [[nodiscard]] TypePtr struct_type(std::string canonical_name) const {
        return types_->struct_type(std::move(canonical_name));
    }
    [[nodiscard]] TypePtr struct_type(std::string canonical_name, SymbolId symbol) const {
        return types_->struct_type(std::move(canonical_name), symbol);
    }
    [[nodiscard]] TypePtr enum_type(std::string canonical_name) const {
        return types_->enum_type(std::move(canonical_name));
    }
    [[nodiscard]] TypePtr enum_type(std::string canonical_name, SymbolId symbol) const {
        return types_->enum_type(std::move(canonical_name), symbol);
    }
    [[nodiscard]] TypePtr enum_variant_type(std::string canonical_name,
                                            std::string variant_name,
                                            std::optional<SymbolId> symbol) const {
        return types_->enum_variant_type(
            std::move(canonical_name), std::move(variant_name), symbol);
    }
    [[nodiscard]] TypedValue typed(TypePtr type, bool is_pure = true) const;
    [[nodiscard]] TypedValue typed_effect(TypePtr type, ExprEffect effect) const;
    [[nodiscard]] TypedValue error_typed(bool is_pure = true) const;
    [[nodiscard]] TypedValue error_typed_effect(ExprEffect effect) const;
    // Helpers moved from the global internal namespace (Del-L2 cleanup).
    [[nodiscard]] TypePtr clone_or_any(MaybeCRef<Type> type) const {
        if (!type.has_value()) {
            return make_any_type();
        }
        return type->get().clone();
    }
    [[nodiscard]] BindingMap clone_bindings(const BindingMap &bindings) const {
        BindingMap result;
        result.reserve(bindings.size());
        for (const auto &[name, type] : bindings) {
            result.emplace(name, type ? type->clone() : make_any_type());
        }
        return result;
    }
};

template <typename Fn> decltype(auto) TypeCheckPass::with_symbol_context(SymbolId id, Fn &&fn) {
    const auto previous_source = current_source_;
    const auto previous_source_id = current_source_id_;
    const auto previous_module_name = current_module_name_;

    current_source_ = nullptr;
    current_source_id_.reset();
    current_module_name_.clear();

    if (const auto symbol = symbol_of(id); symbol.has_value()) {
        current_source_id_ = symbol->get().source_id;
        current_module_name_ = symbol->get().module_name;
        if (graph_ != nullptr && symbol->get().source_id.has_value()) {
            if (const auto source = source_unit_for(*symbol->get().source_id); source.has_value()) {
                current_source_ = &source->get();
            }
        }
    }

    using Result = std::invoke_result_t<Fn>;
    if constexpr (std::is_void_v<Result>) {
        std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
    } else {
        Result result = std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
        return result;
    }
}

// P3 (RFC §3.2.2 / type-system §1.4): runs `fn` with the impl block's
// defining source/module as the current context, then restores the previous
// context on exit. Impl blocks have no symbol, so the source is the impl's
// source_id (captured at indexing time). The defining module is resolved from
// the source via the SourceGraph; the no-graph single-program path keeps the
// anonymous top-level context (empty module name, nullopt source).
template <typename Fn>
decltype(auto) TypeCheckPass::with_symbol_context_for_impl(std::optional<SourceId> source_id,
                                                           Fn &&fn) {
    const auto previous_source = current_source_;
    const auto previous_source_id = current_source_id_;
    const auto previous_module_name = current_module_name_;

    current_source_id_ = source_id;
    current_module_name_ = module_name_of(source_id);
    current_source_ = nullptr;
    if (graph_ != nullptr && source_id.has_value()) {
        if (const auto source = source_unit_for(*source_id); source.has_value()) {
            current_source_ = &source->get();
        }
    }

    using Result = std::invoke_result_t<Fn>;
    if constexpr (std::is_void_v<Result>) {
        std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
    } else {
        Result result = std::forward<Fn>(fn)();
        current_source_ = previous_source;
        current_source_id_ = previous_source_id;
        current_module_name_ = previous_module_name;
        return result;
    }
}

} // namespace ahfl
