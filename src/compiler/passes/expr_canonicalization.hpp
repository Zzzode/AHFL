#pragma once

#include "compiler/passes/pass_manager.hpp"

namespace ahfl::passes {

/// Canonicalizes `Bool` expressions in contracts and workflows:
/// - Double negation: !!x → x
/// - Constant folding: true && p → p, false || p → p
/// - Identity elimination: p && true → p, p || false → p
/// - Annihilators: false && p → false, true || p → true
///
/// Both the clause-level expression form (a bare `requires:` / `ensures:`
/// expression) and ordinary expressions embedded inside a temporal formula
/// (`invariant: always (true && ready(x))`, workflow `safety:` / `liveness:`)
/// are canonicalized: the embed is an ordinary `ExprRef` in the same arena, so
/// the rules apply there identically. Temporal operators themselves are left to
/// `TemporalSimplificationPass`.
class ExprCanonicalizationPass final : public Pass {
  public:
    [[nodiscard]] std::string_view name() const override {
        return "expr-canonicalization";
    }
    [[nodiscard]] bool run(ir::Program &program) override;
};

} // namespace ahfl::passes
