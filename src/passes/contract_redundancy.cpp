#include "ahfl/passes/contract_redundancy.hpp"

#include "ahfl/ir/ir.hpp"

#include <variant>
#include <vector>

namespace ahfl::passes {

namespace {

// Simple structural equality check for expressions (by kind only for now).
// A full deep comparison would need recursive traversal; this catches
// identical clause references (same pointer) and trivially equal structures.
bool clauses_structurally_equal(const ir::ContractClause &a, const ir::ContractClause &b) {
    if (a.kind != b.kind) {
        return false;
    }
    // Compare by variant index — same index means same expression type
    if (a.value.index() != b.value.index()) {
        return false;
    }
    // For a deeper structural comparison we'd need recursive equality on Expr trees.
    // For now, compare by pointer identity (catches exact duplicates from copy).
    if (std::holds_alternative<ir::ExprPtr>(a.value) &&
        std::holds_alternative<ir::ExprPtr>(b.value)) {
        return std::get<ir::ExprPtr>(a.value).get() == std::get<ir::ExprPtr>(b.value).get();
    }
    if (std::holds_alternative<ir::TemporalExprPtr>(a.value) &&
        std::holds_alternative<ir::TemporalExprPtr>(b.value)) {
        return std::get<ir::TemporalExprPtr>(a.value).get() ==
               std::get<ir::TemporalExprPtr>(b.value).get();
    }
    return false;
}

} // namespace

std::unique_ptr<AnalysisResult> ContractRedundancyPass::run(const ir::Program &program) {
    auto result = std::make_unique<ContractRedundancyResult>();

    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }

        const auto &clauses = contract->clauses;
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            for (std::size_t j = i + 1; j < clauses.size(); ++j) {
                if (clauses_structurally_equal(clauses[i], clauses[j])) {
                    result->duplicates.push_back({contract->target, i, j});
                }
            }
        }
    }

    return result;
}

} // namespace ahfl::passes
