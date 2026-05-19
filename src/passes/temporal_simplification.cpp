#include "ahfl/passes/temporal_simplification.hpp"

#include "ahfl/ir/ir.hpp"

#include <variant>

namespace ahfl::passes {

namespace {

// Recursively simplify a temporal expression. Returns true if modified.
bool simplify_temporal(ir::TemporalExprPtr &expr) {
    if (!expr) {
        return false;
    }

    bool modified = false;

    if (auto *unary = std::get_if<ir::TemporalUnaryExpr>(&expr->node)) {
        // First recurse
        modified |= simplify_temporal(unary->operand);

        // always(always(p)) → always(p)
        // eventually(eventually(p)) → eventually(p)
        if (unary->op == ir::TemporalUnaryOp::Always ||
            unary->op == ir::TemporalUnaryOp::Eventually) {
            if (auto *inner = std::get_if<ir::TemporalUnaryExpr>(&unary->operand->node)) {
                if (inner->op == unary->op) {
                    // Collapse: replace expr with the inner operand's operand
                    // but keep our outer operator
                    unary->operand = std::move(inner->operand);
                    return true;
                }
            }
        }

        // Double negation: not(not(p)) → p
        if (unary->op == ir::TemporalUnaryOp::Not) {
            if (auto *inner = std::get_if<ir::TemporalUnaryExpr>(&unary->operand->node)) {
                if (inner->op == ir::TemporalUnaryOp::Not) {
                    expr = std::move(inner->operand);
                    return true;
                }
            }
        }
    } else if (auto *binary = std::get_if<ir::TemporalBinaryExpr>(&expr->node)) {
        modified |= simplify_temporal(binary->lhs);
        modified |= simplify_temporal(binary->rhs);
    }

    return modified;
}

} // namespace

bool TemporalSimplificationPass::run(ir::Program &program) {
    bool any_modified = false;

    for (auto &decl : program.declarations) {
        // Simplify temporal expressions in contracts
        if (auto *contract = std::get_if<ir::ContractDecl>(&decl)) {
            for (auto &clause : contract->clauses) {
                if (auto *texpr = std::get_if<ir::TemporalExprPtr>(&clause.value)) {
                    any_modified |= simplify_temporal(*texpr);
                }
            }
        }

        // Simplify temporal expressions in workflows (safety/liveness)
        if (auto *wf = std::get_if<ir::WorkflowDecl>(&decl)) {
            for (auto &safety : wf->safety) {
                any_modified |= simplify_temporal(safety);
            }
            for (auto &liveness : wf->liveness) {
                any_modified |= simplify_temporal(liveness);
            }
        }
    }

    return any_modified;
}

} // namespace ahfl::passes
