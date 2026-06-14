#include "compiler/passes/expr_canonicalization.hpp"

#include "ahfl/compiler/ir/ir.hpp"

#include <variant>

namespace ahfl::passes {

namespace {

// Check if an expression is a boolean literal with given value.
bool is_bool_literal(const ir::ExprRef &expr, bool value) {
    if (!expr) {
        return false;
    }
    const auto *lit = std::get_if<ir::BoolLiteralExpr>(&expr->node);
    return lit != nullptr && lit->value == value;
}

// Recursively canonicalize an expression. Returns true if modified.
bool canonicalize_expr(ir::ExprRef &expr) {
    if (!expr) {
        return false;
    }

    bool modified = false;

    // First, recurse into children
    if (auto *unary = std::get_if<ir::UnaryExpr>(&expr->node)) {
        modified |= canonicalize_expr(unary->operand);

        // Double negation elimination: !!x → x
        if (unary->op == ir::ExprUnaryOp::Not) {
            if (auto *inner_unary = std::get_if<ir::UnaryExpr>(&unary->operand->node)) {
                if (inner_unary->op == ir::ExprUnaryOp::Not) {
                    // Replace expr with inner_unary->operand
                    auto inner = std::move(inner_unary->operand);
                    expr = std::move(inner);
                    return true;
                }
            }
        }
    } else if (auto *binary = std::get_if<ir::BinaryExpr>(&expr->node)) {
        modified |= canonicalize_expr(binary->lhs);
        modified |= canonicalize_expr(binary->rhs);

        // Constant folding for && and ||
        if (binary->op == ir::ExprBinaryOp::And) {
            // true && p → p
            if (is_bool_literal(binary->lhs, true)) {
                expr = std::move(binary->rhs);
                return true;
            }
            // p && true → p
            if (is_bool_literal(binary->rhs, true)) {
                expr = std::move(binary->lhs);
                return true;
            }
            // false && p → false
            if (is_bool_literal(binary->lhs, false)) {
                expr = std::move(binary->lhs);
                return true;
            }
            // p && false → false
            if (is_bool_literal(binary->rhs, false)) {
                expr = std::move(binary->rhs);
                return true;
            }
        } else if (binary->op == ir::ExprBinaryOp::Or) {
            // false || p → p
            if (is_bool_literal(binary->lhs, false)) {
                expr = std::move(binary->rhs);
                return true;
            }
            // p || false → p
            if (is_bool_literal(binary->rhs, false)) {
                expr = std::move(binary->lhs);
                return true;
            }
            // true || p → true
            if (is_bool_literal(binary->lhs, true)) {
                expr = std::move(binary->lhs);
                return true;
            }
            // p || true → true
            if (is_bool_literal(binary->rhs, true)) {
                expr = std::move(binary->rhs);
                return true;
            }
        }
    }

    return modified;
}

bool canonicalize_contract_clauses(ir::ContractDecl &contract) {
    bool modified = false;
    for (auto &clause : contract.clauses) {
        if (auto *expr_ptr = std::get_if<ir::ExprRef>(&clause.value)) {
            modified |= canonicalize_expr(*expr_ptr);
        }
    }
    return modified;
}

} // namespace

bool ExprCanonicalizationPass::run(ir::Program &program) {
    bool any_modified = false;

    for (auto &decl : program.declarations) {
        if (auto *contract = std::get_if<ir::ContractDecl>(&decl)) {
            any_modified |= canonicalize_contract_clauses(*contract);
        }
    }

    return any_modified;
}

} // namespace ahfl::passes
