#include "compiler/passes/expr_canonicalization.hpp"

#include "ahfl/base/support/overloaded.hpp"
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

bool canonicalize_expr(ir::ExprRef &expr);

// Canonicalize every ordinary expression embedded inside a temporal formula,
// recursing through the temporal operators. A temporal formula is a *mixing*
// of temporal atoms and ordinary expressions: `always (true && ready(x))`
// lowers to `always` applied to an `EmbeddedTemporalExpr` whose payload is the
// ordinary `true && ready(x)` tree. That payload lives in the same arena as a
// clause-level expression and is subject to the exact same `&&` / `||` /
// double-negation rules, so leaving it untouched would silently exempt every
// temporal guard from canonicalization (and, worse, make the pipeline's result
// depend on whether a formula happened to be written with or without a temporal
// wrapper).
//
// The per-alternative classification below is generated from
// `temporal_nodes.def` — the single source of truth for the temporal node set —
// so a node added there without a `CANON_TEMPORAL_<Name>` handler is a COMPILE
// ERROR naming the type (CLAUDE.md Principle 5) rather than a silently missed
// subtree.
bool canonicalize_temporal(ir::TemporalExprPtr &expr) {
    if (!expr) {
        return false;
    }

    bool modified = false;

#define CANON_TEMPORAL_LEAF(Name) [&](ir::Name &) {},
#define CANON_TEMPORAL_EmbeddedTemporalExpr(Name)                                        \
    [&](ir::Name &value) { modified |= canonicalize_expr(value.expr); },
#define CANON_TEMPORAL_TemporalUnaryExpr(Name)                                           \
    [&](ir::Name &value) { modified |= canonicalize_temporal(value.operand); },
#define CANON_TEMPORAL_TemporalBinaryExpr(Name)                                          \
    [&](ir::Name &value) {                                                               \
        modified |= canonicalize_temporal(value.lhs);                                    \
        modified |= canonicalize_temporal(value.rhs);                                    \
    },
    // `called` / `in_state` / `running` / `completed` name a runtime event or
    // state observation and carry no ordinary-expression payload.
#define CANON_TEMPORAL_CalledTemporalExpr(Name) CANON_TEMPORAL_LEAF(Name)
#define CANON_TEMPORAL_InStateTemporalExpr(Name) CANON_TEMPORAL_LEAF(Name)
#define CANON_TEMPORAL_RunningTemporalExpr(Name) CANON_TEMPORAL_LEAF(Name)
#define CANON_TEMPORAL_CompletedTemporalExpr(Name) CANON_TEMPORAL_LEAF(Name)
#define HANDLE_TEMPORAL_NODE(Name) CANON_TEMPORAL_##Name(Name)
    std::visit(Overloaded{
#include "ahfl/compiler/ir/temporal_nodes.def"
               },
               expr->node);
#undef HANDLE_TEMPORAL_NODE
#undef CANON_TEMPORAL_CompletedTemporalExpr
#undef CANON_TEMPORAL_RunningTemporalExpr
#undef CANON_TEMPORAL_InStateTemporalExpr
#undef CANON_TEMPORAL_CalledTemporalExpr
#undef CANON_TEMPORAL_TemporalBinaryExpr
#undef CANON_TEMPORAL_TemporalUnaryExpr
#undef CANON_TEMPORAL_EmbeddedTemporalExpr
#undef CANON_TEMPORAL_LEAF

    return modified;
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
        } else if (auto *temporal_ptr = std::get_if<ir::TemporalExprPtr>(&clause.value)) {
            modified |= canonicalize_temporal(*temporal_ptr);
        }
    }
    return modified;
}

bool canonicalize_workflow_properties(ir::WorkflowDecl &workflow) {
    bool modified = false;
    for (auto &safety : workflow.safety) {
        modified |= canonicalize_temporal(safety);
    }
    for (auto &liveness : workflow.liveness) {
        modified |= canonicalize_temporal(liveness);
    }
    return modified;
}

} // namespace

bool ExprCanonicalizationPass::run(ir::Program &program) {
    bool any_modified = false;

    for (auto &decl : program.declarations) {
        if (auto *contract = std::get_if<ir::ContractDecl>(&decl)) {
            any_modified |= canonicalize_contract_clauses(*contract);
        } else if (auto *workflow = std::get_if<ir::WorkflowDecl>(&decl)) {
            any_modified |= canonicalize_workflow_properties(*workflow);
        }
    }

    return any_modified;
}

} // namespace ahfl::passes
