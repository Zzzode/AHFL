#include "ahfl/compiler/semantics/typed_hir.hpp"

#include <cstdint>
#include <functional>
#include <utility>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t hash_combine(std::size_t seed, std::size_t value) noexcept {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

} // namespace

std::size_t InstanceKeyHash::operator()(const InstanceKey &key) const noexcept {
    std::size_t hash = std::hash<std::size_t>{}(key.decl_symbol.value);
    // Order-sensitive combine: identical type_args in identical order produce
    // identical hashes, and type_arg order is part of the InstanceKey identity.
    for (const auto *t : key.type_args) {
        const auto ptr_bits = reinterpret_cast<std::uintptr_t>(t);
        hash = hash_combine(hash, static_cast<std::size_t>(ptr_bits));
    }
    return hash;
}

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
                .source_id_value =
                    reference.source_id.has_value() ? reference.source_id->value : std::size_t{0},
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
                .source_id_value =
                    expr.source_id.has_value() ? expr.source_id->value : std::size_t{0},
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

MaybeCRef<ResolvedReference> TypedProgram::find_reference(ReferenceKind kind,
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

const TypedExpr *resolve_child(const TypedProgram &program, const TypedExprChild &child) noexcept {
    if (child.expr_index == UINT32_MAX)
        return nullptr;
    if (child.expr_index >= program.expressions.size())
        return nullptr;
    return &program.expressions[child.expr_index];
}

TypedExpr *resolve_child_mut(TypedProgram &program, const TypedExprChild &child) noexcept {
    if (child.expr_index == UINT32_MAX)
        return nullptr;
    if (child.expr_index >= program.expressions.size())
        return nullptr;
    return &program.expressions[child.expr_index];
}

const TypedExpr *TypedProgram::find_expr(std::uint64_t node_id,
                                         std::optional<SourceId> source_id) const noexcept {
    // O(1) fast path via reverse index.
    if (node_id != 0) {
        const auto it = expr_index_.find(ExprIndexKey{
            .node_id = node_id,
            .source_id_value = source_id.has_value() ? source_id->value : std::size_t{0},
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
            .source_id_value = source_id.has_value() ? source_id->value : std::size_t{0},
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

const TypedExpr *
TypedProgram::find_expr_by_range(SourceRange range,
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
TypedProgram::find_expr_containing(std::size_t offset,
                                   std::optional<SourceId> source_id) const noexcept {
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

const TypedBlock *
TypedProgram::find_block_by_range(SourceRange range,
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

const TypedStatement *TypedProgram::find_statement_by_range(
    SourceRange range, TypedStmtKind kind, std::optional<SourceId> source_id) const noexcept {
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

// ----------------------------------------------------------------------------
// P2d.S4a: monomorphize_decl() implementation
// ----------------------------------------------------------------------------
//
// Strategy:
//   1. Dedup: consult program.instance_index_ first.
//   2. Clone: deep-copy all records owned by the source decl's typed-hir
//      footprint into the flat stores, assigning new node_ids from
//      next_instance_node_id and remapping child/children_expr_index/
//      children_index / statement_indexes references.
//   3. Budget: accumulate the additive kMonoBudget* per-record constants and
//      write into mono_budget + the instance record itself.
//   4. Type substitution: rewrite `type` fields in every cloned TypedExpr,
//      TypedDecl, TypedStatement (let_type) using the provided key.type_args.
//      For this iteration we keep type substitution deliberately simple:
//      key.type_args is a "bind in source-declaration order" list applied to
//      the source decl's semantic types. Because AHFL's source syntax does
//      not yet expose generic declarations, the implementation stores the
//      type_args vector uninterpreted inside InstanceKey and, when building
//      the instance decl payload, writes any substituted CapabilityTypeInfo
//      / PredicateTypeInfo params with types taken from `key.type_args` if
//      the vector is non-empty and of matching size. Otherwise types are
//      copied verbatim (this preserves behavior for non-generic sources).

namespace {

// Monomorphization-local working state. Each successful `monomorphize_decl`
// call uses its own state (vectors are append-only, so two concurrent calls
// on different programs would be disjoint anyway; we are not MT-safe here).
struct MonoWork {
    std::uint32_t source_first_expr{0};
    std::uint32_t source_first_stmt{0};
    std::uint32_t source_first_block{0};
    std::uint32_t source_first_temporal{0};
    std::uint32_t target_first_expr{0};
    std::uint32_t target_first_stmt{0};
    std::uint32_t target_first_block{0};
    std::uint32_t target_first_temporal{0};
    std::uint64_t cost{0};
};

// Map a source-store index to its cloned counterpart. Indexes equal to
// UINT32_MAX pass through. Returns UINT32_MAX if the source index was below
// the work's "first owned" watermark (i.e. refers to something outside the
// current instance's footprint — those references are preserved verbatim
// because they point at shared, non-owned typed-hir records such as
// globally-visible constant expressions).
[[nodiscard]] std::uint32_t remap_index(const MonoWork & /*work*/,
                                        std::uint32_t source_idx,
                                        std::uint32_t source_first,
                                        std::uint32_t target_first,
                                        std::uint32_t source_count) noexcept {
    if (source_idx == UINT32_MAX) {
        return UINT32_MAX;
    }
    if (source_idx < source_first) {
        return source_idx;
    }
    const auto delta = source_idx - source_first;
    if (delta >= source_count) {
        // Out-of-range: preserve as-is (caller invariant violation; defensive).
        return source_idx;
    }
    return target_first + delta;
}

} // namespace

MonomorphizeResult monomorphize_decl(TypedProgram &program,
                                     const std::uint32_t source_decl_index,
                                     InstanceKey key) noexcept {
    if (source_decl_index >= program.declarations.size()) {
        return {MonomorphizeStatus::Created, UINT32_MAX};
    }

    // --------------------------
    // Step 1: dedup check.
    // --------------------------
    const auto dedup_it = program.instance_index_.find(key);
    if (dedup_it != program.instance_index_.end()) {
        ++program.mono_budget.dedup_hits;
        return {MonomorphizeStatus::DedupHit, dedup_it->second};
    }

    const TypedDecl &source_decl = program.declarations[source_decl_index];

    // --------------------------
    // Step 2: capture owned footprint.
    // --------------------------
    // For simplicity, the instance is considered to own every record in the
    // flat stores appended AFTER the source decl was typechecked. Because we
    // don't have a per-decl append boundary in this iteration, we conservatively
    // treat the full current sizes as "owned baseline" and clone ALL records.
    // This produces correct (if potentially redundant) 1:1 per-instance typed
    // trees; a later optimization pass can share unchanged subtrees.
    MonoWork work;
    work.source_first_expr = 0;
    work.source_first_stmt = 0;
    work.source_first_block = 0;
    work.source_first_temporal = 0;
    const auto source_expr_count = static_cast<std::uint32_t>(program.expressions.size());
    const auto source_stmt_count = static_cast<std::uint32_t>(program.statements.size());
    const auto source_block_count = static_cast<std::uint32_t>(program.blocks.size());
    const auto source_temporal_count =
        static_cast<std::uint32_t>(program.temporal_exprs.size());

    work.target_first_expr = source_expr_count;
    work.target_first_stmt = source_stmt_count;
    work.target_first_block = source_block_count;
    work.target_first_temporal = source_temporal_count;

    // --------------------------
    // Step 3: allocate cloned records (raw copy, then remap).
    // --------------------------
    // Expressions.
    program.expressions.reserve(program.expressions.size() + source_expr_count);
    for (std::uint32_t i = 0; i < source_expr_count; ++i) {
        TypedExpr clone = program.expressions[i];
        // Assign a new AST-disjoint node_id so downstream passes never alias
        // clones with their originating records. A zero node_id is left at
        // zero (unassigned, "no AST node").
        if (clone.node_id != 0) {
            clone.node_id = program.next_instance_node_id++;
        }
        // Children remap happens below.
        program.expressions.push_back(std::move(clone));
        work.cost += kMonoBudgetPerExpr;
    }

    // Blocks.
    program.blocks.reserve(program.blocks.size() + source_block_count);
    for (std::uint32_t i = 0; i < source_block_count; ++i) {
        TypedBlock clone = program.blocks[i];
        program.blocks.push_back(std::move(clone));
        work.cost += kMonoBudgetPerBlock;
    }

    // Statements.
    program.statements.reserve(program.statements.size() + source_stmt_count);
    for (std::uint32_t i = 0; i < source_stmt_count; ++i) {
        TypedStatement clone = program.statements[i];
        if (clone.node_id != 0) {
            clone.node_id = program.next_instance_node_id++;
        }
        program.statements.push_back(std::move(clone));
        work.cost += kMonoBudgetPerStmt;
    }

    // Temporal expressions.
    program.temporal_exprs.reserve(program.temporal_exprs.size() + source_temporal_count);
    for (std::uint32_t i = 0; i < source_temporal_count; ++i) {
        TypedTemporalExpr clone = program.temporal_exprs[i];
        if (clone.node_id != 0) {
            clone.node_id = program.next_instance_node_id++;
        }
        program.temporal_exprs.push_back(std::move(clone));
        work.cost += kMonoBudgetPerTemporal;
    }

    // --------------------------
    // Step 4: remap index references inside cloned records.
    // --------------------------
    for (std::uint32_t i = 0; i < source_expr_count; ++i) {
        TypedExpr &clone = program.expressions[work.target_first_expr + i];
        for (auto &child : clone.children) {
            child.expr_index = remap_index(work,
                                           child.expr_index,
                                           work.source_first_expr,
                                           work.target_first_expr,
                                           source_expr_count);
        }
    }

    for (std::uint32_t i = 0; i < source_block_count; ++i) {
        TypedBlock &clone = program.blocks[work.target_first_block + i];
        for (auto &si : clone.statement_indexes) {
            si = remap_index(work,
                             si,
                             work.source_first_stmt,
                             work.target_first_stmt,
                             source_stmt_count);
        }
    }

    for (std::uint32_t i = 0; i < source_stmt_count; ++i) {
        TypedStatement &clone = program.statements[work.target_first_stmt + i];
        for (auto &ei : clone.children_expr_index) {
            ei = remap_index(work,
                             ei,
                             work.source_first_expr,
                             work.target_first_expr,
                             source_expr_count);
        }
        clone.then_block_index = remap_index(work,
                                             clone.then_block_index,
                                             work.source_first_block,
                                             work.target_first_block,
                                             source_block_count);
        clone.else_block_index = remap_index(work,
                                             clone.else_block_index,
                                             work.source_first_block,
                                             work.target_first_block,
                                             source_block_count);
    }

    for (std::uint32_t i = 0; i < source_temporal_count; ++i) {
        TypedTemporalExpr &clone = program.temporal_exprs[work.target_first_temporal + i];
        for (auto &ci : clone.children_index) {
            // Temporal children can point either at `expressions` (Atom/Embedded)
            // or at `temporal_exprs` (Unary/Binary). Without a dedicated tag in
            // the existing data model we disambiguate heuristically: a child
            // index smaller than both stores' sizes is treated as temporal
            // (matches the TypedTemporalExpr semantics: Unary/Binary take
            // temporal children, Atom takes an expression child and indexes
            // into expressions). Because we clone BOTH stores uniformly the
            // exact split doesn't affect correctness of the index remap
            // itself: we just need to pick the same interpretation as the
            // original owner. We use `op` to disambiguate when possible,
            // falling back to remapping against temporal_exprs if the op is
            // temporal, against expressions otherwise.
            switch (clone.op) {
            case TypedTemporalOp::Atom:
                ci = remap_index(work,
                                 ci,
                                 work.source_first_expr,
                                 work.target_first_expr,
                                 source_expr_count);
                break;
            case TypedTemporalOp::NameLiteralCalled:
            case TypedTemporalOp::NameLiteralRunning:
            case TypedTemporalOp::NameLiteralCompleted:
            case TypedTemporalOp::StateLiteral:
                // Literal ops carry no indexes that require remap; no-op.
                break;
            case TypedTemporalOp::TemporalNot:
            case TypedTemporalOp::TemporalNext:
            case TypedTemporalOp::TemporalAlways:
            case TypedTemporalOp::TemporalEventually:
            case TypedTemporalOp::TemporalAnd:
            case TypedTemporalOp::TemporalOr:
            case TypedTemporalOp::TemporalImply:
            case TypedTemporalOp::TemporalUntil:
            default:
                ci = remap_index(work,
                                 ci,
                                 work.source_first_temporal,
                                 work.target_first_temporal,
                                 source_temporal_count);
                break;
            }
        }
    }

    // --------------------------
    // Step 5: clone the source TypedDecl and (optionally) substitute types.
    // --------------------------
    TypedDecl decl_clone = source_decl;
    // Rewrite the cloned decl's type using key.type_args when the payload
    // carries a callable signature (CapabilityTypeInfo / PredicateTypeInfo).
    if (const auto *cap = std::get_if<CapabilityTypeInfo>(&source_decl.payload);
        cap != nullptr && !key.type_args.empty()) {
        CapabilityTypeInfo new_payload = *cap;
        // Bind type_args in lockstep to the param list; leftover type_args
        // are ignored (future expansion would require arity validation).
        const std::size_t bind_n = std::min(key.type_args.size(), new_payload.params.size());
        for (std::size_t i = 0; i < bind_n; ++i) {
            if (key.type_args[i] != nullptr) {
                new_payload.params[i].type = key.type_args[i];
            }
        }
        // If there's a leftover trailing type_arg AND the capability has a
        // return_type, treat it as the substituted return type (this matches
        // Rust-style Fn<T0, T1, R> conventions).
        if (key.type_args.size() > new_payload.params.size() &&
            new_payload.return_type != nullptr) {
            const auto *ret = key.type_args[new_payload.params.size()];
            if (ret != nullptr) {
                new_payload.return_type = ret;
            }
        }
        decl_clone.payload = std::move(new_payload);
        decl_clone.type = source_decl.type;
    } else if (const auto *pred = std::get_if<PredicateTypeInfo>(&source_decl.payload);
               pred != nullptr && !key.type_args.empty()) {
        PredicateTypeInfo new_payload = *pred;
        const std::size_t bind_n = std::min(key.type_args.size(), new_payload.params.size());
        for (std::size_t i = 0; i < bind_n; ++i) {
            if (key.type_args[i] != nullptr) {
                new_payload.params[i].type = key.type_args[i];
            }
        }
        decl_clone.payload = std::move(new_payload);
        decl_clone.type = source_decl.type;
    }
    // For all other payloads we leave the cloned TypedDecl unchanged.

    const std::uint32_t decl_clone_index =
        static_cast<std::uint32_t>(program.declarations.size());
    program.declarations.push_back(std::move(decl_clone));
    work.cost += kMonoBudgetPerDecl;

    // --------------------------
    // Step 6: root enumeration.
    // --------------------------
    // "Roots" are the cloned flat-store indexes that directly correspond to
    // sub-records reachable from the cloned TypedDecl. Without an explicit
    // per-decl provenance map we report a 1:1 mapping of every owned clone:
    // the target_first_* .. target_first_* + count ranges are the roots
    // themselves.
    MonomorphizedInstance instance;
    instance.key = std::move(key);
    instance.decl_index = decl_clone_index;
    instance.root_expr_indexes.reserve(source_expr_count);
    for (std::uint32_t i = 0; i < source_expr_count; ++i) {
        instance.root_expr_indexes.push_back(work.target_first_expr + i);
    }
    instance.root_stmt_indexes.reserve(source_stmt_count);
    for (std::uint32_t i = 0; i < source_stmt_count; ++i) {
        instance.root_stmt_indexes.push_back(work.target_first_stmt + i);
    }
    instance.root_block_indexes.reserve(source_block_count);
    for (std::uint32_t i = 0; i < source_block_count; ++i) {
        instance.root_block_indexes.push_back(work.target_first_block + i);
    }
    instance.root_temporal_indexes.reserve(source_temporal_count);
    for (std::uint32_t i = 0; i < source_temporal_count; ++i) {
        instance.root_temporal_indexes.push_back(work.target_first_temporal + i);
    }
    instance.instantiated_type = program.declarations[decl_clone_index].type;
    instance.budget_cost = work.cost + kMonoBudgetOverhead;

    // --------------------------
    // Step 7: register into TypedProgram.
    // --------------------------
    const std::uint32_t instance_index =
        static_cast<std::uint32_t>(program.monomorphized_instances.size());
    program.monomorphized_instances.push_back(std::move(instance));
    // Re-create a key reference from the newly-stored instance (guaranteed to
    // outlive the function) and install it in the dedup map.
    program.instance_index_.emplace(program.monomorphized_instances.back().key, instance_index);

    program.mono_budget.total_cost += work.cost + kMonoBudgetOverhead;
    ++program.mono_budget.instances_created;

    // Keep expr_index_ consistent with freshly-cloned expression records.
    program.rebuild_indices();

    return {MonomorphizeStatus::Created, instance_index};
}

} // namespace ahfl
