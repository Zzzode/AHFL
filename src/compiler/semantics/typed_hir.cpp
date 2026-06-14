#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <cstdint>
#include <functional>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

} // namespace

std::size_t TypedSymbolLocalKeyHash::operator()(const TypedSymbolLocalKey &key) const noexcept {
    std::size_t hash = std::hash<std::string>{}(key.name);
    hash = hash_combine(hash, std::hash<std::string>{}(key.module_name));
    hash = hash_combine(hash, static_cast<std::size_t>(key.name_space));
    return hash;
}

std::size_t TypedReferenceKeyHash::operator()(const TypedReferenceKey &key) const noexcept {
    std::size_t hash = static_cast<std::size_t>(key.kind);
    hash = hash_combine(hash, key.begin_offset);
    hash = hash_combine(hash, key.end_offset);
    hash = hash_combine(hash, key.source_id_value);
    hash = hash_combine(hash, static_cast<std::size_t>(key.has_source_id));
    return hash;
}

std::size_t
TypedReferenceNoSourceKeyHash::operator()(const TypedReferenceNoSourceKey &key) const noexcept {
    std::size_t hash = static_cast<std::size_t>(key.kind);
    hash = hash_combine(hash, key.begin_offset);
    hash = hash_combine(hash, key.end_offset);
    return hash;
}

void TypedProgram::rebuild_resolver_indices() {
    symbol_id_index_.clear();
    symbol_local_index_.clear();
    reference_index_.clear();
    reference_no_source_index_.clear();

    symbol_id_index_.reserve(symbols.size());
    symbol_local_index_.reserve(symbols.size());
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        const auto compact_index = static_cast<std::uint32_t>(index);
        const auto &symbol = symbols[index];
        symbol_id_index_.insert_or_assign(symbol.id.value, compact_index);
        symbol_local_index_.insert_or_assign(
            TypedSymbolLocalKey{
                .name_space = symbol.name_space,
                .name = symbol.local_name,
                .module_name = symbol.module_name,
            },
            compact_index);
        symbol_local_index_.insert_or_assign(
            TypedSymbolLocalKey{
                .name_space = symbol.name_space,
                .name = symbol.canonical_name,
                .module_name = {},
            },
            compact_index);
    }

    reference_index_.reserve(references.size());
    reference_no_source_index_.reserve(references.size());
    for (std::size_t index = 0; index < references.size(); ++index) {
        const auto compact_index = static_cast<std::uint32_t>(index);
        const auto &reference = references[index];
        reference_index_.insert_or_assign(
            TypedReferenceKey{
                .kind = reference.kind,
                .begin_offset = reference.range.begin_offset,
                .end_offset = reference.range.end_offset,
                .source_id_value = reference.source_id.has_value()
                                       ? reference.source_id->value
                                       : std::size_t{0},
                .has_source_id = reference.source_id.has_value(),
            },
            compact_index);
        reference_no_source_index_.insert_or_assign(
            TypedReferenceNoSourceKey{
                .kind = reference.kind,
                .begin_offset = reference.range.begin_offset,
                .end_offset = reference.range.end_offset,
            },
            compact_index);
    }
}

void TypedProgram::rebuild_indices() {
    rebuild_resolver_indices();

    expr_index_.clear();
    expr_index_.reserve(expressions.size());
    for (std::size_t index = 0; index < expressions.size(); ++index) {
        const auto &expr = expressions[index];
        if (expr.node_id == 0) {
            continue;
        }
        expr_index_.insert_or_assign(
            ExprIndexKey{
                .node_id = expr.node_id,
                .source_id_value = expr.source_id.has_value() ? expr.source_id->value
                                                              : std::size_t{0},
                .has_source_id = expr.source_id.has_value(),
            },
            static_cast<std::uint32_t>(index));
    }
}

MaybeCRef<Symbol> TypedProgram::find_symbol(SymbolId id) const {
    if (const auto found = symbol_id_index_.find(id.value);
        found != symbol_id_index_.end() && found->second < symbols.size()) {
        return std::cref(symbols[found->second]);
    }
    if (id.value < symbols.size() && symbols[id.value].id == id) {
        return std::cref(symbols[id.value]);
    }
    return std::nullopt;
}

MaybeCRef<Symbol> TypedProgram::find_local_symbol(SymbolNamespace name_space,
                                                  std::string_view name,
                                                  std::string_view module_name) const {
    const auto found = symbol_local_index_.find(TypedSymbolLocalKey{
        .name_space = name_space,
        .name = std::string(name),
        .module_name = std::string(module_name),
    });
    if (found != symbol_local_index_.end() && found->second < symbols.size()) {
        return std::cref(symbols[found->second]);
    }
    return std::nullopt;
}

MaybeCRef<ResolvedReference>
TypedProgram::find_reference(ReferenceKind kind,
                             SourceRange range,
                             std::optional<SourceId> source_id) const {
    if (source_id.has_value()) {
        const auto found = reference_index_.find(TypedReferenceKey{
            .kind = kind,
            .begin_offset = range.begin_offset,
            .end_offset = range.end_offset,
            .source_id_value = source_id->value,
            .has_source_id = true,
        });
        if (found != reference_index_.end() && found->second < references.size()) {
            return std::cref(references[found->second]);
        }
    }

    const auto found = reference_no_source_index_.find(TypedReferenceNoSourceKey{
        .kind = kind,
        .begin_offset = range.begin_offset,
        .end_offset = range.end_offset,
    });
    if (found != reference_no_source_index_.end() && found->second < references.size()) {
        return std::cref(references[found->second]);
    }
    return std::nullopt;
}

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
    // O(1) fast path via reverse index.
    if (node_id != 0) {
        const auto it = expr_index_.find(ExprIndexKey{
            .node_id = node_id,
            .source_id_value = source_id.has_value() ? source_id->value
                                                     : std::size_t{0},
            .has_source_id = source_id.has_value(),
        });
        if (it != expr_index_.end() && it->second < expressions.size()) {
            return &expressions[it->second];
        }
    }
    // Fallback linear scan (node_id == 0 or index miss).
    for (const auto &expr : expressions) {
        if (expr.node_id == node_id && expr.source_id == source_id) {
            return &expr;
        }
    }
    return nullptr;
}

TypedExpr *TypedProgram::find_expr(std::uint64_t node_id,
                                   std::optional<SourceId> source_id) noexcept {
    // O(1) fast path via reverse index.
    if (node_id != 0) {
        const auto it = expr_index_.find(ExprIndexKey{
            .node_id = node_id,
            .source_id_value = source_id.has_value() ? source_id->value
                                                     : std::size_t{0},
            .has_source_id = source_id.has_value(),
        });
        if (it != expr_index_.end() && it->second < expressions.size()) {
            return &expressions[it->second];
        }
    }
    // Fallback linear scan (node_id == 0 or index miss).
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

const TypedBlock *TypedProgram::find_block_by_range(SourceRange range,
                                                    std::optional<SourceId> source_id) const noexcept {
    for (const auto &block : blocks) {
        if (block.range.begin_offset == range.begin_offset &&
            block.range.end_offset == range.end_offset && block.source_id == source_id) {
            return &block;
        }
    }
    return nullptr;
}

TypedBlock *TypedProgram::find_block_by_range(SourceRange range,
                                              std::optional<SourceId> source_id) noexcept {
    for (auto &block : blocks) {
        if (block.range.begin_offset == range.begin_offset &&
            block.range.end_offset == range.end_offset && block.source_id == source_id) {
            return &block;
        }
    }
    return nullptr;
}

const TypedStatement *TypedProgram::find_statement_by_range(SourceRange range,
                                                            TypedStmtKind kind,
                                                            std::optional<SourceId> source_id) const noexcept {
    for (const auto &stmt : statements) {
        if (stmt.range.begin_offset == range.begin_offset &&
            stmt.range.end_offset == range.end_offset && stmt.kind == kind &&
            stmt.source_id == source_id) {
            return &stmt;
        }
    }
    return nullptr;
}

TypedStatement *TypedProgram::find_statement_by_range(SourceRange range,
                                                      TypedStmtKind kind,
                                                      std::optional<SourceId> source_id) noexcept {
    for (auto &stmt : statements) {
        if (stmt.range.begin_offset == range.begin_offset &&
            stmt.range.end_offset == range.end_offset && stmt.kind == kind &&
            stmt.source_id == source_id) {
            return &stmt;
        }
    }
    return nullptr;
}

const TypedTemporalExpr *
TypedProgram::find_temporal_by_range(SourceRange range,
                                     std::optional<SourceId> source_id) const noexcept {
    for (const auto &te : temporal_exprs) {
        if (te.range.begin_offset == range.begin_offset &&
            te.range.end_offset == range.end_offset && te.source_id == source_id) {
            return &te;
        }
    }
    return nullptr;
}

TypedTemporalExpr *
TypedProgram::find_temporal_by_range(SourceRange range,
                                     std::optional<SourceId> source_id) noexcept {
    for (auto &te : temporal_exprs) {
        if (te.range.begin_offset == range.begin_offset &&
            te.range.end_offset == range.end_offset && te.source_id == source_id) {
            return &te;
        }
    }
    return nullptr;
}

} // namespace ahfl
