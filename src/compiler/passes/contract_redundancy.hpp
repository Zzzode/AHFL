#pragma once

#include "compiler/passes/pass_manager.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ahfl::passes {

/// Analysis result: reports duplicate contract clauses.
struct ContractRedundancyResult final : public AnalysisResult {
    struct DuplicateClause {
        std::string contract_target;
        std::size_t first_index;
        std::size_t second_index;
    };
    std::vector<DuplicateClause> duplicates;
};

/// Detects structurally identical clauses within the same contract.
class ContractRedundancyPass final : public AnalysisPass {
  public:
    [[nodiscard]] std::string_view name() const override { return "contract-redundancy"; }
    [[nodiscard]] std::unique_ptr<AnalysisResult> run(const ir::Program &program) override;
};

} // namespace ahfl::passes
