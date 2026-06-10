#pragma once

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"
#include "ahfl/base/support/source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ahfl {

struct SourceGraph;
struct TypeCheckResult;

enum class TypedExprChildRole {
    Operand,
    LeftOperand,
    RightOperand,
    Argument,
    StructFieldValue,
    CollectionElement,
    MapKey,
    MapValue,
    Base,
    Index,
    Grouped,
};

enum class TypedCallTargetKind {
    None,
    Capability,
    Predicate,
};

struct TypedExpr;

struct TypedExprChild {
    TypedExprChildRole role{TypedExprChildRole::Operand};
    std::string name;
    // Index into TypedProgram::expressions (flat vector). UINT32_MAX means
    // "no child" (equivalent to a null pointer). Storing an index (instead
    // of a pointer / shared_ptr) keeps children valid even if the flat
    // vector reallocates, and guarantees that post-typecheck edits to the
    // expressions flat store are visible to child traversals (T1.3).
    std::uint32_t expr_index{UINT32_MAX};
};

struct TypedProgram;

// Helper: resolve a TypedExprChild to a pointer. Returns nullptr if the
// child's index is unset / out of range.
[[nodiscard]] const TypedExpr *
resolve_child(const TypedProgram &program, const TypedExprChild &child) noexcept;
[[nodiscard]] TypedExpr *
resolve_child_mut(TypedProgram &program, const TypedExprChild &child) noexcept;

struct TypedExpr {
    ast::ExprSyntaxKind kind{ast::ExprSyntaxKind::NoneLiteral};
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id{0};
    TypePtr type{nullptr};
    ExprEffect effect{ExprEffect::Pure};
    bool is_pure{true};
    std::optional<SymbolId> resolved_symbol;
    std::string semantic_name;
    TypedCallTargetKind call_target_kind{TypedCallTargetKind::None};
    std::string path_root;
    std::vector<std::string> member_path;
    std::vector<TypedExprChild> children;

    // ------------------------------------------------------------------------
    // Primitive payload mirrors of the originating ast::ExprSyntax.
    // Stored here so typed-tree passes (lowering, rewrites, equivalence) do
    // not need to reach back into the AST. Each field is only meaningful for
    // the `kind`s listed after it.
    // ------------------------------------------------------------------------
    bool bool_value{false};                        // BoolLiteral
    ast::ExprUnaryOp unary_op{ast::ExprUnaryOp::Not};       // Unary
    ast::ExprBinaryOp binary_op{ast::ExprBinaryOp::Implies}; // Binary
    std::string literal_spelling;                  // Integer/Float/Decimal/String/Duration literal
    std::string member_name;                       // MemberAccess (right-hand field name)
};

struct TypedDecl {
    ast::NodeKind kind{ast::NodeKind::Program};
    SymbolId symbol;
    SourceRange range;
    // Source owning this declaration. Set for SourceGraph-based typechecks;
    // left empty for single-program typechecks. The lowering pass uses this
    // to partition the flat TypedProgram::declarations list back into
    // per-source iteration (T1.4).
    std::optional<SourceId> source_id;
    // For ContractDecl/FlowDecl: SymbolId of the owning agent. For all other
    // declaration kinds (Struct/Enum/Agent/...) left empty: the `symbol` field
    // already records the declaration's own id.
    std::optional<SymbolId> associated_agent_symbol;
    TypePtr type{nullptr};
};

struct TypedProgram {
    const ast::Program *ast_program{nullptr};
    const SourceGraph *source_graph{nullptr};
    const ResolveResult *resolve_result{nullptr};
    const TypeCheckResult *type_check_result{nullptr};
    std::vector<TypedDecl> declarations;
    std::vector<TypedExpr> expressions;

    [[nodiscard]] const TypedExpr *find_expr(std::uint64_t node_id,
                                             std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedExpr *find_expr(std::uint64_t node_id,
                                       std::optional<SourceId> source_id) noexcept;

    // Range-based exact match. Equivalent to the legacy
    // TypeCheckResult::find_expression_type(range, source_id).
    [[nodiscard]] const TypedExpr *
    find_expr_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedExpr *
    find_expr_by_range(SourceRange range, std::optional<SourceId> source_id) noexcept;

    // Find the most specific (smallest) expression whose range contains the
    // given byte offset. Used by LSP hover for "what type is under the cursor".
    [[nodiscard]] const TypedExpr *
    find_expr_containing(std::size_t offset, std::optional<SourceId> source_id) const noexcept;
};

// ----------------------------------------------------------------------------
// TypedExpr visitor (AST-independent dispatch)
// ----------------------------------------------------------------------------
//
// `typed_visit(expr, visitor)` dispatches a TypedExpr to the matching
// `Visitor::visit_*(expr)` overload. Unlike `ast::visit_expr_syntax` this
// dispatcher never inspects `ast::ExprSyntax`; it reads the node kind,
// children, and payload from the TypedExpr itself so passes (typed-tree IR
// lowering, post-typecheck rewrites, equality checks) can walk the typed
// representation without pulling in the AST.
//
// Children ordering is guaranteed to match `ast::visit_expr_syntax` dispatch
// semantics so lowering produces byte-identical IR to the legacy path:
//   * Some           -> [Operand]
//   * Call           -> [Argument, ...]          (each child.name == param name)
//   * StructLiteral  -> [StructFieldValue, ...]  (each child.name == field name)
//   * List/Set       -> [CollectionElement, ...]
//   * Map            -> [MapKey, MapValue, ...]  (interleaved pairs)
//   * Unary          -> [Operand]
//   * Binary         -> [LeftOperand, RightOperand]
//   * MemberAccess   -> [Base]                   (child.name == member name)
//   * IndexAccess    -> [Base, Index]
//   * Group          -> [Grouped]
//   * Literals/Path/QualifiedValue -> no children
//
// The project's -Wswitch -Werror guard catches missing cases when
// `ast::ExprSyntaxKind` / `TypedExpr` coverage grows.
template <typename Visitor>
decltype(auto) typed_visit(const TypedExpr &expr, Visitor &&visitor) {
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

    return std::forward<Visitor>(visitor).visit_unknown(expr);
}

} // namespace ahfl
