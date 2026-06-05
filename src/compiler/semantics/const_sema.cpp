#include "ahfl/compiler/semantics/const_sema.hpp"

namespace ahfl {

namespace {

[[nodiscard]] bool is_const_expr_syntax(const ast::ExprSyntax &expr, std::string_view &reason) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::BoolLiteral:
    case ast::ExprSyntaxKind::IntegerLiteral:
    case ast::ExprSyntaxKind::FloatLiteral:
    case ast::ExprSyntaxKind::DecimalLiteral:
    case ast::ExprSyntaxKind::StringLiteral:
    case ast::ExprSyntaxKind::DurationLiteral:
    case ast::ExprSyntaxKind::NoneLiteral:
    case ast::ExprSyntaxKind::QualifiedValue:
        return true;
    case ast::ExprSyntaxKind::Some:
    case ast::ExprSyntaxKind::Group:
        return is_const_expr_syntax(*expr.first, reason);
    case ast::ExprSyntaxKind::StructLiteral:
        for (const auto &field : expr.struct_fields) {
            if (!is_const_expr_syntax(*field->value, reason)) {
                return false;
            }
        }
        return true;
    case ast::ExprSyntaxKind::ListLiteral:
    case ast::ExprSyntaxKind::SetLiteral:
        for (const auto &item : expr.items) {
            if (!is_const_expr_syntax(*item, reason)) {
                return false;
            }
        }
        return true;
    case ast::ExprSyntaxKind::MapLiteral:
        for (const auto &entry : expr.map_entries) {
            if (!is_const_expr_syntax(*entry->key, reason) ||
                !is_const_expr_syntax(*entry->value, reason)) {
                return false;
            }
        }
        return true;
    case ast::ExprSyntaxKind::MemberAccess:
        return is_const_expr_syntax(*expr.first, reason);
    case ast::ExprSyntaxKind::IndexAccess:
        return is_const_expr_syntax(*expr.first, reason) &&
               is_const_expr_syntax(*expr.second, reason);
    case ast::ExprSyntaxKind::Path:
        reason = "runtime path references are not compile-time constants";
        return false;
    case ast::ExprSyntaxKind::Call:
        reason = "capability and predicate calls are not compile-time constants";
        return false;
    case ast::ExprSyntaxKind::Unary:
    case ast::ExprSyntaxKind::Binary:
        reason = "constant folding for operators is not implemented";
        return false;
    }

    reason = "unsupported expression form";
    return false;
}

} // namespace

ConstExprSyntaxResult classify_const_expr_syntax(const ast::ExprSyntax &expr) {
    std::string_view reason = "unsupported expression form";
    return ConstExprSyntaxResult{
        .is_const = is_const_expr_syntax(expr, reason),
        .reason = reason,
    };
}

} // namespace ahfl
