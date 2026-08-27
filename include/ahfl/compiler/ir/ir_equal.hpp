#pragma once

// KR5.11: reusable structural equality for the IR expression / temporal-expr
// layers. "Structural" means two nodes compare equal iff they have the same
// shape and payload, recursing through ExprRef / TemporalExprPtr children —
// independent of node identity (pointer / flat-store index), SourceRange, node
// id, or inferred effect. This lets analysis passes (e.g. contract-redundancy)
// detect semantically duplicate expressions rather than only exact copies that
// still share a flat-store slot.
//
// The comparison intentionally ignores:
//   * Expr::source_range, Expr::id, Expr::effect, Expr::resolved_type
//   * TemporalExpr::source_range
// because those are provenance / analysis metadata, not part of the
// expression's structure. Callers that need identity comparison should compare
// ExprRef indices directly instead.

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir {

/// Structural equality for two expression trees. Null (absent) refs compare
/// equal to each other and unequal to any present ref.
[[nodiscard]] bool exprs_structurally_equal(const ExprRef &lhs, const ExprRef &rhs);

/// Structural equality for two expression nodes (dereferenced).
[[nodiscard]] bool exprs_structurally_equal(const Expr &lhs, const Expr &rhs);

/// Structural equality for two temporal-expression trees.
[[nodiscard]] bool temporal_exprs_structurally_equal(const TemporalExpr &lhs,
                                                      const TemporalExpr &rhs);

} // namespace ahfl::ir
