#pragma once

// Internal header (not in public include/) shared by typecheck.cpp,
// typecheck_decls.cpp, and any future split implementation file. It exposes
// the TypeCheckPass class plus the shared helper types so a single class
// definition can be implemented across multiple translation units, mirroring
// LLVM/Clang Sema's split between SemaDecl/SemaExpr/SemaStmt while keeping
// state on a single class.

#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/flow_facts.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/type_expectation.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl {

namespace internal {

using BindingMap = std::unordered_map<std::string, TypePtr>;

enum class CallContext {
    PureOnly,
    Flow,
    Workflow,
};

struct TypedValue {
    TypePtr type;
    ExprEffect effect{ExprEffect::Pure};
    bool is_pure{true};
};

enum class ConstEvalKind {
    KnownConst,
    NotConst,
    Error,
};

struct ConstEvalResult {
    ConstEvalKind kind{ConstEvalKind::Error};
    TypedValue typed_value;
};

struct ValueContext {
    BindingMap bindings;
    FlowFacts flow_facts;
    CallContext call_context{CallContext::PureOnly};
    std::optional<SymbolId> current_agent;
};

[[nodiscard]] inline bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] inline bool is_error_type(const Type &type) noexcept {
    return type.holds<types::AnyT>() || type.holds<types::NeverT>();
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
// `internal` alias used by typecheck.cpp.

using ast::visit_expr_syntax;

} // namespace internal

class TypeCheckPass final {
  public:
    TypeCheckPass(const ast::Program &program, const ResolveResult &resolve_result)
        : program_(&program), resolve_result_(resolve_result), types_(&TypeContext::global()) {}
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeCheckOptions options)
        : program_(&program), resolve_result_(resolve_result), types_(&TypeContext::global()),
          options_(options) {}
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeContext &types)
        : program_(&program), resolve_result_(resolve_result), types_(&types) {}
    TypeCheckPass(const ast::Program &program,
                  const ResolveResult &resolve_result,
                  TypeContext &types,
                  TypeCheckOptions options)
        : program_(&program), resolve_result_(resolve_result), types_(&types), options_(options) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result)
        : graph_(&graph), resolve_result_(resolve_result), types_(&TypeContext::global()) {}
    TypeCheckPass(const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  TypeCheckOptions options)
        : graph_(&graph), resolve_result_(resolve_result), types_(&TypeContext::global()),
          options_(options) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result, TypeContext &types)
        : graph_(&graph), resolve_result_(resolve_result), types_(&types) {}
    TypeCheckPass(const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  TypeContext &types,
                  TypeCheckOptions options)
        : graph_(&graph), resolve_result_(resolve_result), types_(&types), options_(options) {}

    [[nodiscard]] TypeCheckResult run();

  private:
    // Re-export internal aliases inside the class so existing implementation
    // files can keep referring to them by their unqualified names.
    using BindingMap = internal::BindingMap;
    using CallContext = internal::CallContext;
    using TypedValue = internal::TypedValue;
    using ConstEvalKind = internal::ConstEvalKind;
    using ConstEvalResult = internal::ConstEvalResult;
    using ValueContext = internal::ValueContext;

    const ast::Program *program_{nullptr};
    const SourceGraph *graph_{nullptr};
    const ResolveResult &resolve_result_;
    TypeContext *types_{nullptr};
    TypeCheckOptions options_;
    TypeRelationContext relations_;
    TypeCheckResult result_;
    const SourceUnit *current_source_{nullptr};
    std::optional<SourceId> current_source_id_;
    std::string current_module_name_;

    std::unordered_map<std::size_t, std::reference_wrapper<const ast::TypeAliasDecl>>
        type_alias_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::ConstDecl>> const_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::StructDecl>> struct_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::EnumDecl>> enum_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::CapabilityDecl>>
        capability_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::PredicateDecl>>
        predicate_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::AgentDecl>> agent_decls_;
    std::unordered_map<std::size_t, std::reference_wrapper<const ast::WorkflowDecl>>
        workflow_decls_;

    std::unordered_map<std::size_t, TypePtr> resolved_alias_types_;
    std::unordered_set<std::size_t> active_aliases_;

    // Declaration indexing & environment building (typecheck_decls.cpp).
    void index_program_declarations(const ast::Program &program);
    void index_declarations();
    void build_type_environment();
    void build_const_types();
    void build_struct_types();
    void build_enum_types();
    void build_capability_types();
    void build_predicate_types();
    void build_agent_types();
    void build_workflow_types();

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
    void check_temporal_embedded_exprs(const ast::TemporalExprSyntax &expr,
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

    [[nodiscard]] MaybeCRef<Symbol> symbol_of(SymbolId id) const;
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
    [[nodiscard]] ConstEvalResult check_const_expr(const ast::ExprSyntax &expr,
                                                   const ValueContext &context,
                                                   MaybeCRef<Type> expected_type,
                                                   std::string_view context_label);

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
    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr);
    [[nodiscard]] TypedValue check_call(const ast::ExprSyntax &expr, const ValueContext &context);
    [[nodiscard]] TypedValue check_struct_literal(const ast::ExprSyntax &expr,
                                                  const ValueContext &context);
    [[nodiscard]] TypedValue check_list_literal(const ast::ExprSyntax &expr,
                                                const ValueContext &context,
                                                MaybeCRef<Type> expected_type,
                                                const TypeExpectation *expectation = nullptr);
    [[nodiscard]] TypedValue check_set_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type,
                                               const TypeExpectation *expectation = nullptr);
    [[nodiscard]] TypedValue check_map_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type,
                                               const TypeExpectation *expectation = nullptr);
    [[nodiscard]] TypedValue check_unary_expr(const ast::ExprSyntax &expr,
                                              const ValueContext &context);
    [[nodiscard]] TypedValue check_binary_expr(const ast::ExprSyntax &expr,
                                               const ValueContext &context);
    [[nodiscard]] TypedValue check_member_access(const ast::ExprSyntax &expr,
                                                 const ValueContext &context);
    [[nodiscard]] TypedValue check_index_access(const ast::ExprSyntax &expr,
                                                const ValueContext &context);

    [[nodiscard]] TypePtr
    field_access(const Type &base_type, std::string_view field_name, SourceRange range);
    [[nodiscard]] TypePtr make_any_type() const {
        return types_->make(TypeKind::Any);
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
