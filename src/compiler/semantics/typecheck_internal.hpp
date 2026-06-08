#pragma once

// Internal header (not in public include/) shared by typecheck.cpp,
// typecheck_decls.cpp, and any future split implementation file. It exposes
// the TypeCheckPass class plus the shared helper types so a single class
// definition can be implemented across multiple translation units, mirroring
// LLVM/Clang Sema's split between SemaDecl/SemaExpr/SemaStmt while keeping
// state on a single class.

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "ahfl/base/support/diagnostics.hpp"
#include "ahfl/base/support/source.hpp"

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
    CallContext call_context{CallContext::PureOnly};
    std::optional<SymbolId> current_agent;
};

[[nodiscard]] inline bool same_range(SourceRange lhs, SourceRange rhs) noexcept {
    return lhs.begin_offset == rhs.begin_offset && lhs.end_offset == rhs.end_offset;
}

[[nodiscard]] inline TypePtr make_any_type() {
    return Type::make(TypeKind::Any);
}

[[nodiscard]] inline TypePtr clone_or_any(MaybeCRef<Type> type) {
    if (!type.has_value()) {
        return make_any_type();
    }

    return type->get().clone();
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

[[nodiscard]] inline BindingMap clone_bindings(const BindingMap &bindings) {
    BindingMap result;
    result.reserve(bindings.size());

    for (const auto &[name, type] : bindings) {
        result.emplace(name, type ? type->clone() : make_any_type());
    }

    return result;
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
// Expression syntax visitor (Task #4)
// ============================================================================
//
// `visit_expr_syntax(expr, visitor)` dispatches an `ast::ExprSyntax` to the
// matching `Visitor::visit_*(expr)` overload. This keeps the dispatch point
// localised here so type-check / IR-lower / future analyses can register
// kind-specific handlers without each maintaining its own ExprSyntaxKind
// switch. Adding a new ExprSyntaxKind requires both adding a case below and
// a matching `visit_<NewKind>` member on every Visitor type, which is
// flagged at compile time by the project's -Wswitch -Werror policy.

template <typename Visitor>
decltype(auto) visit_expr_syntax(const ast::ExprSyntax &expr, Visitor &&visitor) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::BoolLiteral:
        return std::forward<Visitor>(visitor).visit_bool_literal(expr);
    case ast::ExprSyntaxKind::IntegerLiteral:
        return std::forward<Visitor>(visitor).visit_integer_literal(expr);
    case ast::ExprSyntaxKind::FloatLiteral:
        return std::forward<Visitor>(visitor).visit_float_literal(expr);
    case ast::ExprSyntaxKind::DecimalLiteral:
        return std::forward<Visitor>(visitor).visit_decimal_literal(expr);
    case ast::ExprSyntaxKind::StringLiteral:
        return std::forward<Visitor>(visitor).visit_string_literal(expr);
    case ast::ExprSyntaxKind::DurationLiteral:
        return std::forward<Visitor>(visitor).visit_duration_literal(expr);
    case ast::ExprSyntaxKind::NoneLiteral:
        return std::forward<Visitor>(visitor).visit_none_literal(expr);
    case ast::ExprSyntaxKind::Some:
        return std::forward<Visitor>(visitor).visit_some(expr);
    case ast::ExprSyntaxKind::Path:
        return std::forward<Visitor>(visitor).visit_path(expr);
    case ast::ExprSyntaxKind::QualifiedValue:
        return std::forward<Visitor>(visitor).visit_qualified_value(expr);
    case ast::ExprSyntaxKind::Call:
        return std::forward<Visitor>(visitor).visit_call(expr);
    case ast::ExprSyntaxKind::StructLiteral:
        return std::forward<Visitor>(visitor).visit_struct_literal(expr);
    case ast::ExprSyntaxKind::ListLiteral:
        return std::forward<Visitor>(visitor).visit_list_literal(expr);
    case ast::ExprSyntaxKind::SetLiteral:
        return std::forward<Visitor>(visitor).visit_set_literal(expr);
    case ast::ExprSyntaxKind::MapLiteral:
        return std::forward<Visitor>(visitor).visit_map_literal(expr);
    case ast::ExprSyntaxKind::Unary:
        return std::forward<Visitor>(visitor).visit_unary(expr);
    case ast::ExprSyntaxKind::Binary:
        return std::forward<Visitor>(visitor).visit_binary(expr);
    case ast::ExprSyntaxKind::MemberAccess:
        return std::forward<Visitor>(visitor).visit_member_access(expr);
    case ast::ExprSyntaxKind::IndexAccess:
        return std::forward<Visitor>(visitor).visit_index_access(expr);
    case ast::ExprSyntaxKind::Group:
        return std::forward<Visitor>(visitor).visit_group(expr);
    }

    // Unreachable: all ExprSyntaxKind cases are exhaustively handled above.
    // Project warnings (-Wswitch -Werror) flag any new kind that is added to
    // ExprSyntaxKind without a matching case here.
    return std::forward<Visitor>(visitor).visit_unknown(expr);
}

} // namespace internal

class TypeCheckPass final {
  public:
    TypeCheckPass(const ast::Program &program, const ResolveResult &resolve_result)
        : program_(&program), resolve_result_(resolve_result) {}
    TypeCheckPass(const SourceGraph &graph, const ResolveResult &resolve_result)
        : graph_(&graph), resolve_result_(resolve_result) {}

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
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range);
    [[nodiscard]] bool check_exact_schema_boundary(const Type &source,
                                                   const Type &target,
                                                   SchemaBoundaryKind boundary,
                                                   SourceRange range,
                                                   std::optional<SourceRange> expected_origin);
    [[nodiscard]] ConstEvalResult check_const_expr(const ast::ExprSyntax &expr,
                                                   const ValueContext &context,
                                                   MaybeCRef<Type> expected_type,
                                                   std::string_view context_label);

    [[nodiscard]] TypedValue check_expr(const ast::ExprSyntax &expr,
                                        const ValueContext &context,
                                        MaybeCRef<Type> expected_type = std::nullopt);
    [[nodiscard]] TypedValue check_expr_impl(const ast::ExprSyntax &expr,
                                             const ValueContext &context,
                                             MaybeCRef<Type> expected_type = std::nullopt);
    [[nodiscard]] TypedValue check_path(const ast::PathSyntax &path, const ValueContext &context);
    [[nodiscard]] TypedValue check_qualified_value(const ast::ExprSyntax &expr);
    [[nodiscard]] TypedValue check_call(const ast::ExprSyntax &expr, const ValueContext &context);
    [[nodiscard]] TypedValue check_struct_literal(const ast::ExprSyntax &expr,
                                                  const ValueContext &context);
    [[nodiscard]] TypedValue check_list_literal(const ast::ExprSyntax &expr,
                                                const ValueContext &context,
                                                MaybeCRef<Type> expected_type);
    [[nodiscard]] TypedValue check_set_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type);
    [[nodiscard]] TypedValue check_map_literal(const ast::ExprSyntax &expr,
                                               const ValueContext &context,
                                               MaybeCRef<Type> expected_type);
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
    [[nodiscard]] TypedValue typed(TypePtr type, bool is_pure = true) const;
    [[nodiscard]] TypedValue typed_effect(TypePtr type, ExprEffect effect) const;
    [[nodiscard]] TypedValue error_typed(bool is_pure = true) const;
    [[nodiscard]] TypedValue error_typed_effect(ExprEffect effect) const;
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
