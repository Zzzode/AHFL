#include "compiler/passes/contract_redundancy.hpp"

#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/ir_equal.hpp"

#include <string>
#include <variant>
#include <vector>

namespace ahfl::passes {

namespace {

// KR5.11: structural clause equality. Two contract clauses are redundant when
// they are the same clause kind and their conditions are structurally equal —
// i.e. the same expression / temporal-formula shape and payload, regardless of
// which flat-store slot each was lowered into. This replaces the previous
// pointer-identity comparison (which only caught exact copies still sharing a
// slot) with the reusable ir::*_structurally_equal utility, so semantically
// duplicate-but-separately-lowered clauses are detected.
bool clauses_structurally_equal(const ir::ContractClause &a, const ir::ContractClause &b) {
    if (a.kind != b.kind) {
        return false;
    }
    if (a.value.index() != b.value.index()) {
        return false;
    }
    if (std::holds_alternative<ir::ExprRef>(a.value)) {
        return ir::exprs_structurally_equal(std::get<ir::ExprRef>(a.value),
                                            std::get<ir::ExprRef>(b.value));
    }
    if (std::holds_alternative<ir::TemporalExprPtr>(a.value)) {
        const auto &lhs = std::get<ir::TemporalExprPtr>(a.value);
        const auto &rhs = std::get<ir::TemporalExprPtr>(b.value);
        if (lhs == nullptr || rhs == nullptr) {
            return lhs == rhs;
        }
        return ir::temporal_exprs_structurally_equal(*lhs, *rhs);
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
        const auto target = std::string(ir::symbol_canonical_name(contract->target_ref));
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            for (std::size_t j = i + 1; j < clauses.size(); ++j) {
                if (clauses_structurally_equal(clauses[i], clauses[j])) {
                    result->duplicates.push_back({target, i, j});
                }
            }
        }
    }

    return result;
}

} // namespace ahfl::passes
