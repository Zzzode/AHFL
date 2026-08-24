#include "verification/formal/subset.hpp"

#include "ahfl/compiler/ir/identity.hpp"

#include <string>
#include <variant>

namespace ahfl::formal {

SubsetEligibilityReport analyze_contract_subset_eligibility(const ir::Program &program,
                                                            const SmtEncodeOptions &options) {
    SubsetEligibilityReport report;

    for (const auto &decl : program.declarations) {
        const auto *contract = std::get_if<ir::ContractDecl>(&decl);
        if (contract == nullptr) {
            continue;
        }

        const auto target = std::string(ir::symbol_canonical_name(contract->target_ref));
        const auto &clauses = contract->clauses;
        for (std::size_t i = 0; i < clauses.size(); ++i) {
            const auto &clause = clauses[i];

            // Only data predicates (the ExprRef branch) go through the SMT
            // encoder. Temporal clauses are handled by the LTL path, and
            // decreases metrics are a termination concern, not a data
            // predicate — both are out of scope for this walk.
            const auto *expr = std::get_if<ir::ExprRef>(&clause.value);
            if (expr == nullptr) {
                continue;
            }
            if (clause.kind == ir::ContractClauseKind::Decreases) {
                continue;
            }

            ++report.data_predicate_clauses;
            const auto encoded = encode_predicate(*expr, options);
            if (encoded.ok()) {
                ++report.eligible_clauses;
                continue;
            }

            report.ineligible.push_back(SubsetIneligibleClause{
                .kind = clause.kind,
                .target_name = target,
                .clause_index = i,
                .rejection = encoded.rejection.value_or(SmtEncodeRejection::UnsupportedNode),
                .source_range = clause.source_range,
            });
        }
    }

    return report;
}

} // namespace ahfl::formal
