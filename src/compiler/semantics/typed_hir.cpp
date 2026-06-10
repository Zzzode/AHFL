#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <cstdint>

namespace ahfl {

const TypedExpr *
resolve_child(const TypedProgram &program, const TypedExprChild &child) noexcept {
    if (child.expr_index == UINT32_MAX) return nullptr;
    if (child.expr_index >= program.expressions.size()) return nullptr;
    return &program.expressions[child.expr_index];
}

TypedExpr *
resolve_child_mut(TypedProgram &program, const TypedExprChild &child) noexcept {
    if (child.expr_index == UINT32_MAX) return nullptr;
    if (child.expr_index >= program.expressions.size()) return nullptr;
    return &program.expressions[child.expr_index];
}

const TypedExpr *TypedProgram::find_expr(std::uint64_t node_id,
                                         std::optional<SourceId> source_id) const noexcept {
    for (const auto &expr : expressions) {
        if (expr.node_id == node_id && expr.source_id == source_id) {
            return &expr;
        }
    }
    return nullptr;
}

TypedExpr *TypedProgram::find_expr(std::uint64_t node_id,
                                   std::optional<SourceId> source_id) noexcept {
    for (auto &expr : expressions) {
        if (expr.node_id == node_id && expr.source_id == source_id) {
            return &expr;
        }
    }
    return nullptr;
}

const TypedExpr *TypedProgram::find_expr_by_range(SourceRange range,
                                                  std::optional<SourceId> source_id) const noexcept {
    for (const auto &expr : expressions) {
        if (expr.range.begin_offset == range.begin_offset &&
            expr.range.end_offset == range.end_offset && expr.source_id == source_id) {
            return &expr;
        }
    }
    return nullptr;
}

TypedExpr *TypedProgram::find_expr_by_range(SourceRange range,
                                            std::optional<SourceId> source_id) noexcept {
    for (auto &expr : expressions) {
        if (expr.range.begin_offset == range.begin_offset &&
            expr.range.end_offset == range.end_offset && expr.source_id == source_id) {
            return &expr;
        }
    }
    return nullptr;
}

const TypedExpr *
TypedProgram::find_expr_containing(std::size_t offset, std::optional<SourceId> source_id) const noexcept {
    const TypedExpr *best = nullptr;
    std::size_t best_size = 0;
    for (const auto &expr : expressions) {
        if (expr.source_id != source_id) {
            continue;
        }
        if (offset < expr.range.begin_offset || offset > expr.range.end_offset) {
            continue;
        }
        const auto size = expr.range.end_offset - expr.range.begin_offset;
        if (best == nullptr || size < best_size) {
            best = &expr;
            best_size = size;
        }
    }
    return best;
}

} // namespace ahfl
