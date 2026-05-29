#pragma once

#include "compiler/passes/pass_manager.hpp"

namespace ahfl::passes {

/// Canonicalizes expressions in contracts:
/// - Double negation: !!x → x
/// - Constant folding: true && p → p, false || p → p
/// - Identity elimination: p && true → p, p || false → p
class ExprCanonicalizationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override { return "expr-canonicalization"; }
    [[nodiscard]] bool run(ir::Program &program) override;
};

} // namespace ahfl::passes
