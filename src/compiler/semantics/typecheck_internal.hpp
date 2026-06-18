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
};

class TypeCheckPass;
class TypedHirBuilder;
class TypeCheckPhaseApi;

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

class EnvironmentBuilder {
  public:
    explicit EnvironmentBuilder(TypeCheckPhaseApi &api) : api_(&api) {}

    void run();

  private:
    TypeCheckPhaseApi *api_{nullptr};
};

class ConstSema {
  public:
    explicit ConstSema(TypeCheckPhaseApi &api) : api_(&api) {}

    void run();

  private:
    TypeCheckPhaseApi *api_{nullptr};
};

class ContractSema {
  public:
    explicit ContractSema(TypeCheckPhaseApi &api) : api_(&api) {}

    void run();

  private:
    TypeCheckPhaseApi *api_{nullptr};
};

class FlowSema {
  public:
    explicit FlowSema(TypeCheckPhaseApi &api) : api_(&api) {}

    void run();

  private:
    TypeCheckPhaseApi *api_{nullptr};
};

class WorkflowSema {
  public:
    explicit WorkflowSema(TypeCheckPhaseApi &api) : api_(&api) {}

    void run();

  private:
    TypeCheckPhaseApi *api_{nullptr};
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

  private:
    TypedProgram *program_{nullptr};
};

class TypeCheckPhaseApi {
  public:
    explicit TypeCheckPhaseApi(TypeCheckPass &driver) : driver_(&driver) {}

    void build_type_environment();
    void check_const_semantics();
    void check_contract_semantics();
    void check_flow_semantics();
    void check_workflow_semantics();

  private:
    TypeCheckPass *driver_{nullptr};
};

class TypeCheckPass final {
  public:
    explicit TypeCheckPass(TypeCheckSession session)
        : session_(std::move(session)), state_(), program_(session_.program),
          graph_(session_.graph), resolve_result_(session_.resolve_result), types_(&session_.types),
          options_(session_.options), result_(state_.result),
          current_source_(state_.current_source), current_source_id_(state_.current_source_id),
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
          hir_builder_(state_.result.typed_program) {}
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
    friend class TypeCheckPhaseApi;

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
    const SourceUnit *&current_source_;
    std::optional<SourceId> &current_source_id_;
    std::string &current_module_name_;
    std::unordered_map<std::size_t, const SourceUnit *> &source_units_by_id_;
    DiagnosticReporter reporter_;
    DeclarationIndex declaration_index_;

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
    TypedHirBuilder hir_builder_;

    std::unordered_map<std::size_t, ConstValue> const_values_;
    std::unordered_set<std::size_t> active_const_values_;
    std::unordered_set<std::size_t> failed_const_values_;
    TypeAliasResolutionState alias_resolution_;

    // Declaration indexing & environment building (typecheck_decls.cpp).
    void build_type_environment();
    void build_const_types();
    void build_struct_types();
    void build_enum_types();
    void build_capability_types();
    void build_predicate_types();
    void build_agent_types();
    void build_workflow_types();
    void build_flow_types();
    void build_flow_types_in_program(const ast::Program &program);
    void build_contract_types();
    void build_contract_types_in_program(const ast::Program &program);

    // Declaration-level checks (typecheck_decls.cpp).
    void check_const_initializers_in_program(const ast::Program &program);
    void check_const_initializers();
    void check_struct_defaults();
    void check_agent_context_defaults();

    // Contracts / flows / workflows (typecheck.cpp).
    void check_contracts_in_program(const ast::Program &program);
    void check_contracts();
    void check_flows_in_program(const ast::Program &program);
    void check_flows();
    void check_workflows_in_program(const ast::Program &program);
    void check_workflows();
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
    void remember_const_value(const ast::ExprSyntax &expr, const ConstValue &value);
    [[nodiscard]] bool ensure_const_value(SymbolId id, SourceRange use_range);

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const;
    [[nodiscard]] TypeResolver make_type_resolver();
    [[nodiscard]] TypePtr resolve_type(const ast::TypeSyntax &type);
    [[nodiscard]] TypePtr resolve_named_type(const ast::QualifiedName &name);
    [[nodiscard]] TypePtr resolve_type_symbol(SymbolId id, SourceRange use_range);
    [[nodiscard]] TypePtr resolve_type_alias(SymbolId id, SourceRange use_range);

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
    [[nodiscard]] ConstEvalResult
    check_const_expr(const ast::ExprSyntax &expr,
                     const ValueContext &context,
                     MaybeCRef<Type> expected_type,
                     std::string_view context_label,
                     std::optional<SymbolId> source_const = std::nullopt);

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
    [[nodiscard]] TypePtr optional_type(TypePtr value_type) const {
        return types_->optional(value_type);
    }
    [[nodiscard]] TypePtr list_type(TypePtr element_type) const {
        return types_->list(element_type);
    }
    [[nodiscard]] TypePtr set_type(TypePtr element_type) const {
        return types_->set(element_type);
    }
    [[nodiscard]] TypePtr map_type(TypePtr key_type, TypePtr value_type) const {
        return types_->map(key_type, value_type);
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

} // namespace ahfl
