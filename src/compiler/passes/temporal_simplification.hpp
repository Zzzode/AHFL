#pragma once

#include "compiler/passes/pass_manager.hpp"

namespace ahfl::passes {

/// Simplifies temporal formulas in contracts and workflows:
/// - always(always(p)) → always(p)
/// - eventually(eventually(p)) → eventually(p)
/// - next(next(p)) is left as-is (semantically different)
class TemporalSimplificationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "temporal-simplification"; }
    [[nodiscard]] bool run(ir::Program &program) override;
};

} // namespace ahfl::passes
