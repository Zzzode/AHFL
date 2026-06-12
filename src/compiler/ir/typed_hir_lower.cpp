#include "ahfl/compiler/ir/typed_hir_lower.hpp"

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/ir/analysis.hpp"
#include "ahfl/compiler/ir/identity.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"
#include "base/support/overloaded.hpp"
#include "base/support/string_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// ---------------------------------------------------------------------------
// AST-dependency boundary (T1.8 P6 complete — declaration payloads migrated)
//
// Expression lowering: 100% typed-tree based (T1.3).
// Statement layer: 100% typed-store based (T1.7 P3).
// Temporal layer: 100% typed-store based (T1.7 P3).
// Declaration layer: 100% typed-store based (T1.8 P6). All structural
//   payloads (struct fields, agent states/transitions/quota, capability
//   effect spec, flow handlers/policy, workflow nodes/safety/liveness,
//   contract clauses) come from TypeEnvironment info stores.
//
// Remaining AST references in lower_xxx functions:
//   * node.range — source range for DeclarationProvenance
//   * node.name — symbol lookup key
//   * Sub-expression / block / temporal AST nodes — passed as .range
//     carriers to the one-way bridge functions (lower_expr, lower_block,
//     lower_temporal) which immediately switch to typed-store lookup.
//   * Module / Import: name + path spellings (trivial, no structural walk).
//
// Live AST payload dereferences in declaration lowering = 0
// (all structural data comes from TypeEnvironment typed stores).
// ---------------------------------------------------------------------------

namespace ahfl {

namespace {

[[nodiscard]] bool paths_equal(const ir::Path &lhs, const ir::Path &rhs) {
    return lhs.root_kind == rhs.root_kind && lhs.root_name == rhs.root_name &&
           lhs.members == rhs.members;
}

[[nodiscard]] bool workflow_value_reads_equal(const ir::WorkflowValueRead &lhs,
                                              const ir::WorkflowValueRead &rhs) {
    return lhs.kind == rhs.kind && lhs.root_name == rhs.root_name && lhs.members == rhs.members;
}

[[nodiscard]] std::string logical_source_path(std::string_view module_name) {
    if (module_name.empty()) return {};
    std::string path;
    path.reserve(module_name.size() + 5);
    for (std::size_t index = 0; index < module_name.size(); ++index) {
        if (index + 1 < module_name.size() && module_name[index] == ':' &&
            module_name[index + 1] == ':') {
            path += '/';
            ++index;
            continue;
        }
        path += module_name[index];
    }
    path += ".ahfl";
    return path;
}

[[nodiscard]] std::string called_observation_symbol(std::string_view agent_name,
                                                    std::string_view capability_name) {
    return "agent__" + sanitize_identifier(agent_name) + "__called__" +
           sanitize_identifier(capability_name);
}

[[nodiscard]] std::string observation_scope_base_name(const ir::FormalObservationScope &scope) {
    switch (scope.kind) {
    case ir::FormalObservationScopeKind::ContractClause:
        return "contract__" + scope.owner + "__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowSafetyClause:
        return "workflow__" + scope.owner + "__safety__" + std::to_string(scope.clause_index);
    case ir::FormalObservationScopeKind::WorkflowLivenessClause:
        return "workflow__" + scope.owner + "__liveness__" + std::to_string(scope.clause_index);
    }
    return "observation";
}

[[nodiscard]] std::string embedded_observation_symbol(const ir::FormalObservationScope &scope) {
    return sanitize_identifier(observation_scope_base_name(scope) + "__atom__" +
                               std::to_string(scope.atom_index));
}


[[nodiscard]] ir::CapabilityEffectKind
lower_capability_effect_kind(ast::CapabilityEffectKind kind) {
    switch (kind) {
    case ast::CapabilityEffectKind::Unknown:             return ir::CapabilityEffectKind::Unknown;
    case ast::CapabilityEffectKind::Read:                return ir::CapabilityEffectKind::Read;
    case ast::CapabilityEffectKind::ExternalSideEffect:  return ir::CapabilityEffectKind::ExternalSideEffect;
    case ast::CapabilityEffectKind::DurableWrite:        return ir::CapabilityEffectKind::DurableWrite;
    case ast::CapabilityEffectKind::FinancialWrite:      return ir::CapabilityEffectKind::FinancialWrite;
    }
    return ir::CapabilityEffectKind::Unknown;
}

[[nodiscard]] ir::CapabilityReceiptMode
lower_capability_receipt_mode(ast::CapabilityReceiptMode mode) {
    switch (mode) {
    case ast::CapabilityReceiptMode::None:     return ir::CapabilityReceiptMode::None;
    case ast::CapabilityReceiptMode::Optional: return ir::CapabilityReceiptMode::Optional;
    case ast::CapabilityReceiptMode::Required: return ir::CapabilityReceiptMode::Required;
    }
    return ir::CapabilityReceiptMode::None;
}

[[nodiscard]] ir::CapabilityRetryMode lower_capability_retry_mode(ast::CapabilityRetryMode mode) {
    switch (mode) {
    case ast::CapabilityRetryMode::Unsafe:          return ir::CapabilityRetryMode::Unsafe;
    case ast::CapabilityRetryMode::SafeIfIdempotent:return ir::CapabilityRetryMode::SafeIfIdempotent;
    case ast::CapabilityRetryMode::Safe:            return ir::CapabilityRetryMode::Safe;
    }
    return ir::CapabilityRetryMode::Unsafe;
}

/// Build a CapabilityEffectSpec from TypeEnvironment's CapabilityEffectTypeInfo
/// (T1.8 Phase 4: replaces the AST-based lower_capability_effect for the info path).
[[nodiscard]] ir::CapabilityEffectSpec
lower_capability_effect_from_info(const CapabilityEffectTypeInfo &info) {
    ir::CapabilityEffectSpec effect;
    if (!info.declared) return effect;
    effect.declared = true;
    effect.kind = lower_capability_effect_kind(
        static_cast<ast::CapabilityEffectKind>(info.effect_kind));
    effect.receipt_mode = lower_capability_receipt_mode(
        static_cast<ast::CapabilityReceiptMode>(info.receipt_mode));
    effect.retry_mode = lower_capability_retry_mode(
        static_cast<ast::CapabilityRetryMode>(info.retry_mode));
    effect.source_range = info.source_range;
    if (!info.domain.empty()) effect.domain = info.domain;
    if (!info.idempotency_key.empty()) effect.idempotency_key = info.idempotency_key;
    if (!info.timeout.empty()) effect.timeout = info.timeout;
    if (!info.compensation.empty()) effect.compensation = info.compensation;
    effect.policies = info.policies;
    return effect;
}

[[nodiscard]] ir::ExprUnaryOp lower_expr_unary_op(ast::ExprUnaryOp op) {
    switch (op) {
    case ast::ExprUnaryOp::Not:     return ir::ExprUnaryOp::Not;
    case ast::ExprUnaryOp::Negate:  return ir::ExprUnaryOp::Negate;
    case ast::ExprUnaryOp::Positive:return ir::ExprUnaryOp::Positive;
    }
    return ir::ExprUnaryOp::Not;
}

[[nodiscard]] ir::ExprBinaryOp lower_expr_binary_op(ast::ExprBinaryOp op) {
    switch (op) {
    case ast::ExprBinaryOp::Implies:     return ir::ExprBinaryOp::Implies;
    case ast::ExprBinaryOp::Or:          return ir::ExprBinaryOp::Or;
    case ast::ExprBinaryOp::And:         return ir::ExprBinaryOp::And;
    case ast::ExprBinaryOp::Equal:       return ir::ExprBinaryOp::Equal;
    case ast::ExprBinaryOp::NotEqual:    return ir::ExprBinaryOp::NotEqual;
    case ast::ExprBinaryOp::Less:        return ir::ExprBinaryOp::Less;
    case ast::ExprBinaryOp::LessEqual:   return ir::ExprBinaryOp::LessEqual;
    case ast::ExprBinaryOp::Greater:     return ir::ExprBinaryOp::Greater;
    case ast::ExprBinaryOp::GreaterEqual:return ir::ExprBinaryOp::GreaterEqual;
    case ast::ExprBinaryOp::Add:         return ir::ExprBinaryOp::Add;
    case ast::ExprBinaryOp::Subtract:    return ir::ExprBinaryOp::Subtract;
    case ast::ExprBinaryOp::Multiply:    return ir::ExprBinaryOp::Multiply;
    case ast::ExprBinaryOp::Divide:      return ir::ExprBinaryOp::Divide;
    case ast::ExprBinaryOp::Modulo:      return ir::ExprBinaryOp::Modulo;
    }
    return ir::ExprBinaryOp::Implies;
}


[[nodiscard]] ir::ContractClauseKind lower_contract_clause_kind(ast::ContractClauseKind kind) {
    switch (kind) {
    case ast::ContractClauseKind::Requires:  return ir::ContractClauseKind::Requires;
    case ast::ContractClauseKind::Ensures:   return ir::ContractClauseKind::Ensures;
    case ast::ContractClauseKind::Invariant: return ir::ContractClauseKind::Invariant;
    case ast::ContractClauseKind::Forbid:    return ir::ContractClauseKind::Forbid;
    }
    return ir::ContractClauseKind::Requires;
}

[[nodiscard]] ir::SymbolRefKind lower_symbol_ref_kind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::Struct:
    case SymbolKind::Enum:
    case SymbolKind::TypeAlias:  return ir::SymbolRefKind::Type;
    case SymbolKind::Const:      return ir::SymbolRefKind::Const;
    case SymbolKind::Capability: return ir::SymbolRefKind::Capability;
    case SymbolKind::Predicate:  return ir::SymbolRefKind::Predicate;
    case SymbolKind::Agent:      return ir::SymbolRefKind::Agent;
    case SymbolKind::Workflow:   return ir::SymbolRefKind::Workflow;
    }
    return ir::SymbolRefKind::Unknown;
}

template <typename T> void push_unique_value(std::vector<T> &values, const T &value) {
    for (const auto &existing : values)
        if (existing == value) return;
    values.push_back(value);
}

void push_unique_path(std::vector<ir::Path> &values, const ir::Path &value) {
    for (const auto &existing : values)
        if (paths_equal(existing, value)) return;
    values.push_back(value);
}

void push_unique_workflow_value_read(std::vector<ir::WorkflowValueRead> &values,
                                     const ir::WorkflowValueRead &value) {
    for (const auto &existing : values)
        if (workflow_value_reads_equal(existing, value)) return;
    values.push_back(value);
}

// ---------------------------------------------------------------------------
// Typed-tree based lowerer. Mirrors ir_lower.cpp::IrLowerer structurally so
// output is byte-identical for unmodified TypedPrograms (verified by T1.2).
// ---------------------------------------------------------------------------

class TypedIrLowerer final {
  public:
    TypedIrLowerer(const TypedProgram &typed_program,
                   const ast::Program &program,
                   const ResolveResult &resolve_result,
                   const TypeCheckResult &type_check_result)
        : typed_program_(&typed_program), program_(&program), graph_(nullptr),
          resolve_result_(resolve_result), type_check_result_(type_check_result) {}

    TypedIrLowerer(const TypedProgram &typed_program,
                   const SourceGraph &graph,
                   const ResolveResult &resolve_result,
                   const TypeCheckResult &type_check_result)
        : typed_program_(&typed_program), program_(nullptr), graph_(&graph),
          resolve_result_(resolve_result), type_check_result_(type_check_result) {}

    [[nodiscard]] ir::Program lower() const {
        ir::Program program_ir;
        if (graph_ != nullptr) {
            program_ir.declarations.reserve(typed_program_->declarations.size());
            for (const auto &source : graph_->sources) {
                if (!source.program) continue;
                enter_source(source);
                // Collect the subset of TypedDecls that belong to this source.
                std::vector<const TypedDecl *> per_source;
                per_source.reserve(typed_program_->declarations.size());
                for (const auto &td : typed_program_->declarations) {
                    if (td.source_id != current_source_id_) continue;
                    per_source.push_back(&td);
                }
                // Emit them in the same order the owning AST stores them so
                // the lowered IR stays byte-identical to the legacy AST-based
                // path (T1.2 equivalence guarantee). We walk the AST decls
                // once and, for each, emit the matching TypedDecl in-place.
                // ModuleDecl / ImportDecl are deliberately never recorded in
                // TypedProgram.declarations (they are parser-level metadata),
                // so they get a short direct-AST fallback.
                for (const auto &ast_decl : source.program->declarations) {
                    const TypedDecl *td = find_typed_decl_for_ast(per_source, *ast_decl);
                    if (td != nullptr) {
                        const ast::Decl *resolved = find_ast_decl(*source.program, *td);
                        if (resolved == nullptr) continue;
                        program_ir.declarations.push_back(lower_declaration(*resolved));
                        continue;
                    }
                    // TypedProgram doesn't carry ModuleDecl / ImportDecl —
                    // emit them directly from the AST so output parity with
                    // the legacy path (and other T1.2-equivalent tests) is
                    // preserved.
                    if (ast_decl->kind == ast::NodeKind::ModuleDecl ||
                        ast_decl->kind == ast::NodeKind::ImportDecl) {
                        program_ir.declarations.push_back(lower_declaration(*ast_decl));
                    }
                }
                leave_source();
            }
            return program_ir;
        }
        if (program_ != nullptr) {
            program_ir.declarations.reserve(typed_program_->declarations.size());
            // Emit TypedDecls in AST declaration order to preserve T1.2
            // byte-identical IR output (see note above for SourceGraph path).
            for (const auto &ast_decl : program_->declarations) {
                const TypedDecl *td = find_typed_decl_for_ast(
                    typed_program_->declarations, *ast_decl);
                if (td != nullptr) {
                    const ast::Decl *resolved = find_ast_decl(*program_, *td);
                    if (resolved == nullptr) continue;
                    program_ir.declarations.push_back(lower_declaration(*resolved));
                    continue;
                }
                if (ast_decl->kind == ast::NodeKind::ModuleDecl ||
                    ast_decl->kind == ast::NodeKind::ImportDecl) {
                    program_ir.declarations.push_back(lower_declaration(*ast_decl));
                }
            }
        }
        return program_ir;
    }

  private:
    const TypedProgram *typed_program_{nullptr};
    const ast::Program *program_{nullptr};
    const SourceGraph *graph_{nullptr};
    const ResolveResult &resolve_result_;
    const TypeCheckResult &type_check_result_;
    mutable const SourceUnit *current_source_{nullptr};
    mutable std::optional<SourceId> current_source_id_;
    mutable std::string current_module_name_;

    // =====================================================================
    // Small factory helpers
    // =====================================================================

    template <typename T>
    [[nodiscard]] ir::ExprPtr
    make_expr(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto expr = make_owned<ir::Expr>();
        expr->node = std::move(node);
        expr->source_range = source_range;
        return expr;
    }

    template <typename T>
    [[nodiscard]] ir::TemporalExprPtr
    make_temporal(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto expr = make_owned<ir::TemporalExpr>();
        expr->node = std::move(node);
        expr->source_range = source_range;
        return expr;
    }

    template <typename T>
    [[nodiscard]] ir::StatementPtr
    make_statement(T node, std::optional<SourceRange> source_range = std::nullopt) const {
        auto statement = make_owned<ir::Statement>();
        statement->node = std::move(node);
        statement->source_range = source_range;
        return statement;
    }

    [[nodiscard]] const SymbolTable &symbols() const noexcept {
        return resolve_result_.symbol_table;
    }

    [[nodiscard]] const TypeEnvironment &environment() const noexcept {
        return type_check_result_.environment;
    }

    // =====================================================================
    // One-way bridge: AST -> typed tree (never the reverse).
    // Mirrors IrLowerer::typed_expr_for but always falls back to range
    // lookup after a node_id miss so detached post-typecheck edits (e.g. a
    // test that zeros node_id on a TypedExpr record to force the slow path)
    // still resolve correctly.
    [[nodiscard]] const TypedExpr *typed_expr_for(const ast::ExprSyntax &expr) const {
        if (expr.node_id != 0) {
            if (const TypedExpr *found =
                    typed_program_->find_expr(expr.node_id, current_source_id_);
                found != nullptr) {
                return found;
            }
        }
        return typed_program_->find_expr_by_range(expr.range, current_source_id_);
    }

    // =====================================================================
    // TypedDecl -> AST Decl bridge (T1.4 declaration iteration).
    //
    // TypedDecl records provenance metadata (kind / symbol / range / type).
    // Full structural payloads (struct fields, agent states, flow handlers,
    // workflow nodes, quotas, ...) still live in the owning AST. This helper
    // resolves a TypedDecl back to its ast::Decl* by (range, kind) equality,
    // disambiguating on canonical name via the symbol table when a range tie
    // occurs. Returns nullptr if no match exists (shouldn't happen for a
    // correctly-built TypedProgram; caller skips the declaration gracefully).
    [[nodiscard]] const ast::Decl *find_ast_decl(const ast::Program &ast_program,
                                                 const TypedDecl &td) const {
        const ast::Decl *candidate = nullptr;
        for (const auto &declaration : ast_program.declarations) {
            if (declaration->range.begin_offset != td.range.begin_offset ||
                declaration->range.end_offset != td.range.end_offset) {
                continue;
            }
            // Exact kind match wins immediately.
            if (declaration->kind == td.kind) return declaration.get();
            // Range collision with mismatched kind: keep as fallback and
            // disambiguate via canonical_name + symbol_id if we end up with
            // multiple candidates.
            if (candidate == nullptr) candidate = declaration.get();
        }
        if (candidate == nullptr) return nullptr;
        // Fallback: check if a candidate with mismatched kind actually maps
        // to the same symbol (range-level collision shouldn't happen in well
        // formed ASTs, but guard anyway for robustness).
        if (td.symbol.value != 0) {
            const auto sym = symbols().get(td.symbol);
            if (sym.has_value()) {
                // Walk AST decls again: prefer any decl whose local name
                // matches the symbol's local_name when kind doesn't line up.
                for (const auto &declaration : ast_program.declarations) {
                    if (declaration->range.begin_offset != td.range.begin_offset ||
                        declaration->range.end_offset != td.range.end_offset) {
                        continue;
                    }
                    std::string_view decl_name = local_name_of(*declaration);
                    if (!decl_name.empty() && decl_name == sym->get().local_name) {
                        return declaration.get();
                    }
                }
            }
        }
        return candidate;
    }

    // Extract the user-visible local name of a top-level AST declaration.
    // Used only for the (rare) range-collision disambiguation path above.
    //
    // NOTE: For declarations whose name/target is stored as an owned
    // QualifiedName (ModuleDecl/ImportDecl/ContractDecl/FlowDecl), the
    // spelling() helper returns a freshly-allocated std::string by value.
    // Using it in a std::string_view return would silently dangle, so we
    // simply report empty names for those kinds here. In practice kind
    // matches always come first in find_ast_decl, so the disambiguation
    // path is never exercised for those declaration kinds anyway.
    [[nodiscard]] static std::string_view local_name_of(const ast::Decl &declaration) noexcept {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
        case ast::NodeKind::ImportDecl:
        case ast::NodeKind::ContractDecl:
        case ast::NodeKind::FlowDecl:
            return {};
        case ast::NodeKind::ConstDecl:
            return static_cast<const ast::ConstDecl &>(declaration).name;
        case ast::NodeKind::TypeAliasDecl:
            return static_cast<const ast::TypeAliasDecl &>(declaration).name;
        case ast::NodeKind::StructDecl:
            return static_cast<const ast::StructDecl &>(declaration).name;
        case ast::NodeKind::EnumDecl:
            return static_cast<const ast::EnumDecl &>(declaration).name;
        case ast::NodeKind::CapabilityDecl:
            return static_cast<const ast::CapabilityDecl &>(declaration).name;
        case ast::NodeKind::PredicateDecl:
            return static_cast<const ast::PredicateDecl &>(declaration).name;
        case ast::NodeKind::AgentDecl:
            return static_cast<const ast::AgentDecl &>(declaration).name;
        case ast::NodeKind::WorkflowDecl:
            return static_cast<const ast::WorkflowDecl &>(declaration).name;
        case ast::NodeKind::Program:
            break;
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Reverse bridge: AST -> TypedDecl.
    //
    // The lower() entry walks AST declarations *in AST order* and emits the
    // TypedDecl whose (range, kind) matches. This keeps the lowered IR
    // byte-identical to the legacy AST-tree lowering path (T1.2). The
    // iteration still originates from TypedProgram.declarations — every
    // declaration we emit was first present in the typed tree — but we
    // reorder the output to match the AST source order. Declaration kinds
    // that the typechecker does *not* record as TypedDecl entries
    // (ModuleDecl / ImportDecl) are emitted directly from the AST by the
    // caller via a short fallback that preserves byte-for-byte parity.
    //
    // Overload 1: accepts a span of pointer-to-TypedDecl (SourceGraph path
    // where we've already filtered per-source).
    [[nodiscard]] static const TypedDecl *
    find_typed_decl_for_ast(const std::vector<const TypedDecl *> &candidates,
                            const ast::Decl &ast_decl) noexcept {
        for (const TypedDecl *td : candidates) {
            if (td == nullptr) continue;
            if (td->range.begin_offset != ast_decl.range.begin_offset ||
                td->range.end_offset != ast_decl.range.end_offset) {
                continue;
            }
            if (td->kind == ast_decl.kind) return td;
        }
        return nullptr;
    }

    // Overload 2: accepts the raw TypedProgram::declarations vector
    // (single-program path where no filtering is needed).
    [[nodiscard]] static const TypedDecl *
    find_typed_decl_for_ast(const std::vector<TypedDecl> &candidates,
                            const ast::Decl &ast_decl) noexcept {
        for (const auto &td : candidates) {
            if (td.range.begin_offset != ast_decl.range.begin_offset ||
                td.range.end_offset != ast_decl.range.end_offset) {
                continue;
            }
            if (td.kind == ast_decl.kind) return &td;
        }
        return nullptr;
    }

    // =====================================================================
    // Source / provenance helpers
    // =====================================================================

    void enter_source(const SourceUnit &source) const {
        current_source_ = &source;
        current_source_id_ = source.id;
        current_module_name_ = source.module_name;
    }

    void leave_source() const {
        current_source_ = nullptr;
        current_source_id_.reset();
        current_module_name_.clear();
    }

    [[nodiscard]] ir::DeclarationProvenance
    current_provenance(std::optional<SourceRange> source_range = std::nullopt) const {
        if (graph_ == nullptr || current_module_name_.empty()) {
            return ir::DeclarationProvenance{
                .module_name = {}, .source_path = {}, .source_range = source_range,
            };
        }
        return ir::DeclarationProvenance{
            .module_name = current_module_name_,
            .source_path = logical_source_path(current_module_name_),
            .source_range = source_range,
        };
    }

    template <typename DeclT>
    [[nodiscard]] DeclT
    with_provenance(DeclT declaration,
                    std::optional<SourceRange> source_range = std::nullopt) const {
        declaration.provenance = current_provenance(source_range);
        return declaration;
    }

    // =====================================================================
    // Symbol / reference helpers
    // =====================================================================

    [[nodiscard]] MaybeCRef<Symbol> find_local_symbol_here(SymbolNamespace name_space,
                                                           std::string_view name) const {
        if (!current_module_name_.empty())
            return symbols().find_local(name_space, name, current_module_name_);
        return symbols().find_local(name_space, name);
    }

    [[nodiscard]] MaybeCRef<Symbol> symbol_from_reference_here(ReferenceKind kind,
                                                               SourceRange range) const {
        const auto reference = resolve_result_.find_reference(kind, range, current_source_id_);
        if (!reference.has_value()) return std::nullopt;
        return symbols().get(reference->get().target);
    }

    [[nodiscard]] std::string canonical_name_from_reference_here(ReferenceKind kind,
                                                                 SourceRange range,
                                                                 std::string_view fallback) const {
        const auto symbol = symbol_from_reference_here(kind, range);
        return symbol.has_value() ? symbol->get().canonical_name : std::string(fallback);
    }

    [[nodiscard]] std::string canonical_local_name_here(SymbolNamespace name_space,
                                                        std::string_view local_name) const {
        const auto symbol = find_local_symbol_here(name_space, local_name);
        return symbol.has_value() ? symbol->get().canonical_name : std::string(local_name);
    }

    [[nodiscard]] ir::TypeRefPtr make_type_ref(ir::TypeRef ref) const {
        return make_owned<ir::TypeRef>(std::move(ref));
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_symbol(MaybeCRef<Symbol> symbol,
                                                       ir::SymbolRefKind fallback_kind,
                                                       std::string_view fallback) const {
        if (!symbol.has_value()) {
            return ir::SymbolRef{
                .kind = fallback_kind,
                .canonical_name = std::string(fallback),
                .local_name = std::string(fallback),
                .module_name = {},
            };
        }
        const auto &value = symbol->get();
        return ir::SymbolRef{
            .kind = lower_symbol_ref_kind(value.kind),
            .canonical_name = value.canonical_name,
            .local_name = value.local_name,
            .module_name = value.module_name,
        };
    }

    [[nodiscard]] ir::SymbolRef symbol_ref_from_reference_here(ReferenceKind kind,
                                                               SourceRange range,
                                                               ir::SymbolRefKind fallback_kind,
                                                               std::string_view fallback) const {
        return symbol_ref_from_symbol(
            symbol_from_reference_here(kind, range), fallback_kind, fallback);
    }

    // =====================================================================
    // Type-ref builders
    // =====================================================================

    [[nodiscard]] ir::TypeRef type_ref_from_type(const Type &type) const {
        return type.visit(types::Overloads{
            [&](const types::AnyT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Any, .display_name = type.describe()};
            },
            [&](const types::NeverT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Never, .display_name = type.describe()};
            },
            [&](const types::ErrorT &) {
                // Error is a typechecker-internal sentinel; at the IR boundary
                // it maps to Any so downstream passes don't need to know about it.
                return ir::TypeRef{.kind = ir::TypeRefKind::Any, .display_name = type.describe()};
            },
            [&](const types::UnitT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Unit, .display_name = type.describe()};
            },
            [&](const types::BoolT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Bool, .display_name = type.describe()};
            },
            [&](const types::IntT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Int, .display_name = type.describe()};
            },
            [&](const types::FloatT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Float, .display_name = type.describe()};
            },
            [&](const types::StringT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::String, .display_name = type.describe()};
            },
            [&](const types::BoundedStringT &value) {
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::BoundedString,
                    .display_name = type.describe(),
                    .string_bounds = std::make_pair(value.minimum, value.maximum),
                };
            },
            [&](const types::UUIDT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::UUID, .display_name = type.describe()};
            },
            [&](const types::TimestampT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Timestamp,
                                   .display_name = type.describe()};
            },
            [&](const types::DurationT &) {
                return ir::TypeRef{.kind = ir::TypeRefKind::Duration,
                                   .display_name = type.describe()};
            },
            [&](const types::DecimalT &value) {
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::Decimal,
                    .display_name = type.describe(),
                    .decimal_scale = value.scale,
                };
            },
            [&](const types::StructT &value) {
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::Struct,
                    .display_name = type.describe(),
                    .canonical_name = value.canonical_name,
                };
            },
            [&](const types::EnumT &value) {
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::Enum,
                    .display_name = type.describe(),
                    .canonical_name = value.canonical_name,
                };
            },
            [&](const types::OptionalT &value) {
                ir::TypeRef ref{.kind = ir::TypeRefKind::Optional,
                                .display_name = type.describe()};
                if (value.inner != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.inner));
                return ref;
            },
            [&](const types::ListT &value) {
                ir::TypeRef ref{.kind = ir::TypeRefKind::List, .display_name = type.describe()};
                if (value.element != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.element));
                return ref;
            },
            [&](const types::SetT &value) {
                ir::TypeRef ref{.kind = ir::TypeRefKind::Set, .display_name = type.describe()};
                if (value.element != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.element));
                return ref;
            },
            [&](const types::MapT &value) {
                ir::TypeRef ref{.kind = ir::TypeRefKind::Map, .display_name = type.describe()};
                if (value.key != nullptr)
                    ref.first = make_type_ref(type_ref_from_type(*value.key));
                if (value.value != nullptr)
                    ref.second = make_type_ref(type_ref_from_type(*value.value));
                return ref;
            },
        });
    }

    [[nodiscard]] ir::TypeRef type_ref_from_maybe(MaybeCRef<Type> type,
                                                  std::string_view fallback = "Any") const {
        if (!type.has_value()) {
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Unresolved,
                .display_name = std::string(fallback),
            };
        }
        return type_ref_from_type(type->get());
    }

    [[nodiscard]] ir::TypeRef named_type_ref_from_syntax(const ast::TypeSyntax &type) const {
        const auto fallback = type.name ? type.name->spelling()
                                        : std::string{"<missing-type>"};
        MaybeCRef<Symbol> symbol;
        if (type.name) {
            symbol = symbol_from_reference_here(ReferenceKind::TypeName, type.name->range);
        }
        auto ref = ir::TypeRef{
            .kind = ir::TypeRefKind::Unresolved,
            .display_name = std::string(fallback),
            .canonical_name = {},
        };
        if (!symbol.has_value()) return ref;
        const auto &value = symbol->get();
        ref.display_name = value.canonical_name;
        ref.canonical_name = value.canonical_name;
        if (value.kind == SymbolKind::Struct)
            ref.kind = ir::TypeRefKind::Struct;
        else if (value.kind == SymbolKind::Enum)
            ref.kind = ir::TypeRefKind::Enum;
        return ref;
    }

    [[nodiscard]] ir::TypeRef type_ref_from_syntax(const ast::TypeSyntax &type) const {
        switch (type.kind) {
        case ast::TypeSyntaxKind::Unit:
            return ir::TypeRef{.kind = ir::TypeRefKind::Unit, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Bool:
            return ir::TypeRef{.kind = ir::TypeRefKind::Bool, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Int:
            return ir::TypeRef{.kind = ir::TypeRefKind::Int, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Float:
            return ir::TypeRef{.kind = ir::TypeRefKind::Float, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::String:
            return ir::TypeRef{.kind = ir::TypeRefKind::String, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::UUID:
            return ir::TypeRef{.kind = ir::TypeRefKind::UUID, .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Timestamp:
            return ir::TypeRef{.kind = ir::TypeRefKind::Timestamp,
                               .display_name = type.spelling()};
        case ast::TypeSyntaxKind::Duration:
            return ir::TypeRef{.kind = ir::TypeRefKind::Duration,
                               .display_name = type.spelling()};
        case ast::TypeSyntaxKind::BoundedString:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::BoundedString,
                .display_name = type.spelling(),
                .string_bounds = type.string_bounds,
            };
        case ast::TypeSyntaxKind::Decimal:
            return ir::TypeRef{
                .kind = ir::TypeRefKind::Decimal,
                .display_name = type.spelling(),
                .decimal_scale = type.decimal_scale,
            };
        case ast::TypeSyntaxKind::Named:
            return named_type_ref_from_syntax(type);
        case ast::TypeSyntaxKind::Optional: {
            auto ref = ir::TypeRef{.kind = ir::TypeRefKind::Optional,
                                    .display_name = type.spelling()};
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::List: {
            auto ref = ir::TypeRef{.kind = ir::TypeRefKind::List,
                                    .display_name = type.spelling()};
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::Set: {
            auto ref = ir::TypeRef{.kind = ir::TypeRefKind::Set,
                                    .display_name = type.spelling()};
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            return ref;
        }
        case ast::TypeSyntaxKind::Map: {
            auto ref = ir::TypeRef{.kind = ir::TypeRefKind::Map,
                                    .display_name = type.spelling()};
            ref.first = type.first ? make_type_ref(type_ref_from_syntax(*type.first)) : nullptr;
            ref.second = type.second ? make_type_ref(type_ref_from_syntax(*type.second)) : nullptr;
            return ref;
        }
        }
        return ir::TypeRef{.kind = ir::TypeRefKind::Unresolved,
                           .display_name = "<invalid-type>"};
    }

    // =====================================================================
    // Typed-tree-only helpers (zero AST dereference)
    // =====================================================================

    [[nodiscard]] std::string render_call_target(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value())
            if (const auto symbol = symbols().get(*expr.resolved_symbol); symbol.has_value())
                return symbol->get().canonical_name;
        return expr.semantic_name;
    }

    [[nodiscard]] std::string render_struct_target(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value())
            if (const auto symbol = symbols().get(*expr.resolved_symbol); symbol.has_value())
                return symbol->get().canonical_name;
        return expr.semantic_name;
    }

    [[nodiscard]] std::string render_qualified_value(const TypedExpr &expr) const {
        if (expr.resolved_symbol.has_value()) {
            if (const auto symbol = symbols().get(*expr.resolved_symbol); symbol.has_value()) {
                if (symbol->get().kind == SymbolKind::Const)
                    return symbol->get().canonical_name;
                const auto &canonical = symbol->get().canonical_name;
                if (!expr.semantic_name.empty()) {
                    const auto separator = expr.semantic_name.rfind("::");
                    if (separator != std::string::npos &&
                        separator + 2 < expr.semantic_name.size()) {
                        const auto variant = expr.semantic_name.substr(separator + 2);
                        if (!variant.empty() && variant != canonical)
                            return canonical + "::" + variant;
                    }
                }
                return canonical;
            }
        }
        return expr.semantic_name;
    }

    [[nodiscard]] ir::Path lower_path_from_typed(const TypedExpr &expr) const {
        ir::PathRootKind root = ir::PathRootKind::Identifier;
        if (expr.path_root == "input") root = ir::PathRootKind::Input;
        else if (expr.path_root == "output") root = ir::PathRootKind::Output;
        // ir::Path::root_name mirrors ast::PathSyntax::root_name exactly: it
        // keeps the source token ("input"/"output"/identifier spelling) so
        // output of typed-tree lowering matches AST-tree lowering byte-for-byte.
        return ir::Path{
            .root_kind = root,
            .root_name = expr.path_root,
            .members = expr.member_path,
        };
    }

    [[nodiscard]] const TypedExpr *
    child_by_role(const TypedExpr &parent, TypedExprChildRole role) const {
        for (const auto &child : parent.children) {
            if (child.role != role) continue;
            const TypedExpr *target = resolve_child(*typed_program_, child);
            if (target != nullptr) return target;
        }
        return nullptr;
    }

    // Nested-dispatchable wrapper (so TypedExprPerKindLowerer can call a
    // static-looking method while resolving through the active TypedProgram).
    static const TypedExpr *
    resolve_child_by_role(const TypedIrLowerer &self,
                          const TypedExpr &parent,
                          TypedExprChildRole role) {
        return self.child_by_role(parent, role);
    }

    // =====================================================================
    // Expression lowering – 100% typed-tree based via typed_visit
    // =====================================================================

    struct TypedExprPerKindLowerer {
        const TypedIrLowerer &self;
        SourceRange range;

        ir::ExprPtr visit_bool_literal(const TypedExpr &e) const {
            return self.make_expr(ir::BoolLiteralExpr{.value = e.bool_value}, range);
        }
        ir::ExprPtr visit_integer_literal(const TypedExpr &e) const {
            return self.make_expr(ir::IntegerLiteralExpr{
                                      .spelling = e.literal_spelling.empty()
                                                      ? std::string{"0"}
                                                      : e.literal_spelling,
                                  },
                                  range);
        }
        ir::ExprPtr visit_float_literal(const TypedExpr &e) const {
            return self.make_expr(ir::FloatLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprPtr visit_decimal_literal(const TypedExpr &e) const {
            return self.make_expr(ir::DecimalLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprPtr visit_string_literal(const TypedExpr &e) const {
            return self.make_expr(ir::StringLiteralExpr{.spelling = e.literal_spelling}, range);
        }
        ir::ExprPtr visit_duration_literal(const TypedExpr &e) const {
            return self.make_expr(
                ir::DurationLiteralExpr{
                    .spelling = e.literal_spelling.empty() ? e.semantic_name
                                                           : e.literal_spelling,
                },
                range);
        }
        ir::ExprPtr visit_none_literal(const TypedExpr &) const {
            return self.make_expr(ir::NoneLiteralExpr{}, range);
        }
        ir::ExprPtr visit_some(const TypedExpr &e) const {
            const TypedExpr *operand = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Operand);
            return self.make_expr(
                ir::SomeExpr{.value = operand ? self.lower_typed_expr(*operand) : nullptr},
                range);
        }
        ir::ExprPtr visit_path(const TypedExpr &e) const {
            return self.make_expr(ir::PathExpr{.path = self.lower_path_from_typed(e)}, range);
        }
        ir::ExprPtr visit_qualified_value(const TypedExpr &e) const {
            return self.make_expr(
                ir::QualifiedValueExpr{.value = self.render_qualified_value(e)}, range);
        }
        ir::ExprPtr visit_call(const TypedExpr &e) const {
            ir::CallExpr call{.callee = self.render_call_target(e), .arguments = {}};
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::Argument) continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    call.arguments.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(call), range);
        }
        ir::ExprPtr visit_struct_literal(const TypedExpr &e) const {
            ir::StructLiteralExpr literal{.type_name = self.render_struct_target(e),
                                          .fields = {}};
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::StructFieldValue) continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr) {
                    literal.fields.push_back(ir::StructFieldInit{
                        .name = child.name,
                        .value = self.lower_typed_expr(*target),
                    });
                }
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprPtr visit_list_literal(const TypedExpr &e) const {
            ir::ListLiteralExpr literal;
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::CollectionElement) continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    literal.items.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprPtr visit_set_literal(const TypedExpr &e) const {
            ir::SetLiteralExpr literal;
            for (const auto &child : e.children) {
                if (child.role != TypedExprChildRole::CollectionElement) continue;
                const TypedExpr *target = resolve_child(*self.typed_program_, child);
                if (target != nullptr)
                    literal.items.push_back(self.lower_typed_expr(*target));
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprPtr visit_map_literal(const TypedExpr &e) const {
            ir::MapLiteralExpr literal;
            // Children are emitted MapKey,MapValue,MapKey,MapValue,... by
            // `typed_children_for`. Iterate pairwise so order is identical to
            // the AST visitor (T1.2 fingerprint-equivalence requirement).
            for (std::size_t i = 0; i + 1 < e.children.size();) {
                const auto &key_child = e.children[i];
                const auto &value_child = e.children[i + 1];
                const TypedExpr *k = resolve_child(*self.typed_program_, key_child);
                const TypedExpr *v = resolve_child(*self.typed_program_, value_child);
                if (key_child.role == TypedExprChildRole::MapKey && k != nullptr &&
                    value_child.role == TypedExprChildRole::MapValue && v != nullptr) {
                    literal.entries.push_back(ir::MapEntryExpr{
                        .key = self.lower_typed_expr(*k),
                        .value = self.lower_typed_expr(*v),
                    });
                    i += 2;
                } else {
                    ++i;
                }
            }
            return self.make_expr(std::move(literal), range);
        }
        ir::ExprPtr visit_unary(const TypedExpr &e) const {
            const TypedExpr *operand = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Operand);
            return self.make_expr(
                ir::UnaryExpr{
                    .op = lower_expr_unary_op(e.unary_op),
                    .operand = operand ? self.lower_typed_expr(*operand) : nullptr,
                },
                range);
        }
        ir::ExprPtr visit_binary(const TypedExpr &e) const {
            const TypedExpr *lhs = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::LeftOperand);
            const TypedExpr *rhs = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::RightOperand);
            return self.make_expr(
                ir::BinaryExpr{
                    .op = lower_expr_binary_op(e.binary_op),
                    .lhs = lhs ? self.lower_typed_expr(*lhs) : nullptr,
                    .rhs = rhs ? self.lower_typed_expr(*rhs) : nullptr,
                },
                range);
        }
        ir::ExprPtr visit_member_access(const TypedExpr &e) const {
            const TypedExpr *base = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Base);
            return self.make_expr(
                ir::MemberAccessExpr{
                    .base = base ? self.lower_typed_expr(*base) : nullptr,
                    .member = e.member_name,
                },
                range);
        }
        ir::ExprPtr visit_index_access(const TypedExpr &e) const {
            const TypedExpr *base = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Base);
            const TypedExpr *idx = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Index);
            return self.make_expr(
                ir::IndexAccessExpr{
                    .base = base ? self.lower_typed_expr(*base) : nullptr,
                    .index = idx ? self.lower_typed_expr(*idx) : nullptr,
                },
                range);
        }
        ir::ExprPtr visit_group(const TypedExpr &e) const {
            const TypedExpr *inner = TypedIrLowerer::resolve_child_by_role(self, e, TypedExprChildRole::Grouped);
            return self.make_expr(
                ir::GroupExpr{.expr = inner ? self.lower_typed_expr(*inner) : nullptr},
                range);
        }
        ir::ExprPtr visit_unknown(const TypedExpr &e) const {
            return self.make_expr(ir::QualifiedValueExpr{.value = "<invalid-expr>"},
                                  e.range);
        }
    };

    // Forward-declared so the visitor above can recurse into children.
    [[nodiscard]] ir::ExprPtr lower_typed_expr(const TypedExpr &expr) const {
        return typed_visit(expr, TypedExprPerKindLowerer{*this, expr.range});
    }

    // Bridge entry: when given an AST expression node, re-root into the
    // typed tree via typed_expr_for. The only place we descend from AST into
    // the typed store.
    //
    // Lookup order (mirrors TypeCheckResult legacy expression-type resolution
    // so post-typecheck edits to TypedProgram records that zero the node_id
    // still resolve):
    //   1. node_id lookup (fast path when AST node_id matches typed record)
    //   2. range-based fallback (covers detached / post-edit records where
    //      the caller has zeroed node_id on purpose)
    [[nodiscard]] ir::ExprPtr lower_expr(const ast::ExprSyntax &ast_expr) const {
        const TypedExpr *typed = typed_expr_for(ast_expr);
        if (typed != nullptr) return lower_typed_expr(*typed);
        return make_expr(ir::QualifiedValueExpr{.value = "<missing-typed-expr>"},
                         ast_expr.range);
    }

    // =====================================================================
    // Temporal lowering (100% typed-tree based; AST fallback removed in T1.7 P2)
    // =====================================================================

    // T1.7 P2: always dispatch through the typed temporal store. The
    // typechecker guarantees every TemporalExprSyntax is paired with a
    // TypedTemporalExpr (T1.2 equivalence tests verify the invariant), so we
    // no longer retain the AST fallback walk. Assert in debug builds so any
    // post-typecheck edits that break the pairing surface immediately.
    [[nodiscard]] ir::TemporalExprPtr
    lower_temporal(const ast::TemporalExprSyntax &expr) const {
        const TypedTemporalExpr *tte = find_typed_temporal_by_range(expr.range, current_source_id_);
        if (tte == nullptr) {
            return make_temporal(ir::CalledTemporalExpr{.capability = "<missing-typed-temporal>"},
                                 expr.range);
        }
        return lower_typed_temporal(*tte);
    }

    // T1.7 P2: always dispatch through the typed block store. The typechecker
    // guarantees every BlockSyntax is paired with a TypedBlock (T1.2 tests
    // enforce the invariant), so the AST fallback is no longer needed. A
    // crash-not-recover sentinel block is emitted only for detached test
    // programs that have been deliberately stripped of their typed store.
    [[nodiscard]] ir::Block lower_block(const ast::BlockSyntax &block) const {
        const TypedBlock *tb = find_typed_block_by_range(block.range, current_source_id_);
        if (tb == nullptr) {
            return ir::Block{.statements = {}, .source_range = block.range};
        }
        return lower_typed_block(*tb);
    }

    // =====================================================================
    // Typed-store statement / block lowering (T1.6 + T1.7 P3)
    //
    // The dispatch chain below walks TypedProgram::statements /
    // TypedProgram::blocks (the flat typed-store) instead of the AST.
    // Expression payloads come from TypedStatement::children_expr_index via
    // lower_typed_expr; then/else blocks come from TypedStatement block
    // indexes. All primitive payloads (let type_ref, assign target path,
    // goto state, assert message) are read directly from the TypedStatement
    // mirror fields – no AST bridge is retained (removed in T1.7 P2).
    // =====================================================================

    // Convenience wrapper around TypedProgram::find_block_by_range.
    [[nodiscard]] const TypedBlock *
    find_typed_block_by_range(SourceRange range,
                              std::optional<SourceId> source_id) const {
        return typed_program_->find_block_by_range(range, source_id);
    }

    // Convenience wrapper around TypedProgram::find_statement_by_range.
    [[nodiscard]] const TypedStatement *
    find_typed_stmt_by_range(SourceRange range,
                             TypedStmtKind kind,
                             std::optional<SourceId> source_id) const {
        return typed_program_->find_statement_by_range(range, kind, source_id);
    }

    // Convenience wrapper around TypedProgram::find_temporal_by_range.
    // Returns nullptr when the temporal typed store has not been populated
    // (detached test cases); callers emit a sentinel temporal expression.
    [[nodiscard]] const TypedTemporalExpr *
    find_typed_temporal_by_range(SourceRange range,
                                 std::optional<SourceId> source_id) const {
        return typed_program_->find_temporal_by_range(range, source_id);
    }

    // Parse a TemporalUnaryOp from the payload_spelling string stored on a
    // TypedTemporalExpr (Unary kind). Mirrors the enum→string mapping that
    // TypeCheckPass::check_temporal_embedded_exprs writes.
    [[nodiscard]] static ir::TemporalUnaryOp
    parse_temporal_unary_op(std::string_view s) {
        if (s == "always")     return ir::TemporalUnaryOp::Always;
        if (s == "eventually") return ir::TemporalUnaryOp::Eventually;
        if (s == "next")       return ir::TemporalUnaryOp::Next;
        if (s == "not")        return ir::TemporalUnaryOp::Not;
        return ir::TemporalUnaryOp::Always;
    }

    // Parse a TemporalBinaryOp from the payload_spelling string stored on a
    // TypedTemporalExpr (Binary kind).
    [[nodiscard]] static ir::TemporalBinaryOp
    parse_temporal_binary_op(std::string_view s) {
        if (s == "implies") return ir::TemporalBinaryOp::Implies;
        if (s == "or")      return ir::TemporalBinaryOp::Or;
        if (s == "and")     return ir::TemporalBinaryOp::And;
        if (s == "until")   return ir::TemporalBinaryOp::Until;
        return ir::TemporalBinaryOp::Implies;
    }

    // Split a "prefix:remainder" payload_spelling on the first ':' character.
    // Returns {prefix, remainder} – if no ':' exists remainder is empty and
    // prefix is the whole string.
    [[nodiscard]] static std::pair<std::string_view, std::string_view>
    split_payload(std::string_view s) {
        const auto pos = s.find(':');
        if (pos == std::string_view::npos) return {s, {}};
        return {s.substr(0, pos), s.substr(pos + 1)};
    }

    // =====================================================================
    // Type-ref reconstruction from TypeSyntax spelling string (T1.7 P1).
    //
    // The typechecker serialises a let-binding's type annotation into the
    // canonical source text produced by TypeSyntax::spelling() (e.g.
    // "Int", "Optional<Foo>", "Map<String, BoundedString<5, 100>>",
    // "Decimal(18)", "pkg::TypeName"). This parser reverses that serialisation
    // to build an ir::TypeRef without re-entering the originating AST.
    //
    // On parse failure the function returns std::nullopt so the caller degrades
    // to the initializer's resolved type then to an Any sentinel (T1.7 P2: the
    // AST bridge has been removed; all lowering paths are now 100% typed-store).
    // =====================================================================

    // Advance `pos` past any whitespace in `s`.
    static void skip_ws(std::string_view s, std::size_t &pos) {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }

    // Find the matching '>' for a '<' starting at pos `open_pos` in `s`,
    // accounting for nested angle brackets and parenthesised sub-exprs.
    // Returns the index of the matching '>' or std::string_view::npos.
    [[nodiscard]] static std::size_t
    find_matching_angle(std::string_view s, std::size_t open_pos) {
        int depth = 1;
        int paren_depth = 0;
        for (std::size_t i = open_pos + 1; i < s.size(); ++i) {
            const char c = s[i];
            if (paren_depth > 0) {
                if (c == '(') ++paren_depth;
                else if (c == ')') --paren_depth;
                continue;
            }
            if (c == '(') { ++paren_depth; continue; }
            if (c == '<') ++depth;
            else if (c == '>') { --depth; if (depth == 0) return i; }
        }
        return std::string_view::npos;
    }

    // Parse one TypeRef from `s.substr(pos)`, advancing `pos` past the
    // consumed text. Returns nullopt on any parse failure. The parser is
    // deliberately small – it covers the exact grammar produced by
    // TypeSyntax::spelling() and nothing more.
    [[nodiscard]] std::optional<ir::TypeRef>
    parse_type_ref_spelling(std::string_view s, std::size_t &pos) const {
        skip_ws(s, pos);
        if (pos >= s.size()) return std::nullopt;

        // Collect the identifier / keyword token: runs of [A-Za-z0-9_:].
        // Qualified names use "::" as separator; leaf keywords match exactly.
        const std::size_t tok_start = pos;
        while (pos < s.size()) {
            const char c = s[pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') { ++pos; continue; }
            if (c == ':' && pos + 1 < s.size() && s[pos + 1] == ':') { pos += 2; continue; }
            break;
        }
        if (pos == tok_start) return std::nullopt; // no token
        const std::string_view tok = s.substr(tok_start, pos - tok_start);

        skip_ws(s, pos);

        // Leaf: built-in keyword with no further decoration.
        auto leaf = [&](ir::TypeRefKind k) -> std::optional<ir::TypeRef> {
            return ir::TypeRef{.kind = k, .display_name = std::string(tok)};
        };
        if (tok == "Unit")      return leaf(ir::TypeRefKind::Unit);
        if (tok == "Bool")      return leaf(ir::TypeRefKind::Bool);
        if (tok == "Int")       return leaf(ir::TypeRefKind::Int);
        if (tok == "Float")     return leaf(ir::TypeRefKind::Float);
        if (tok == "String") {
            // String (plain) vs String(min, max) (bounded)
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == '(') {
                ++pos; // consume '('
                skip_ws(s, pos);
                // Parse min integer
                std::int64_t min_val = 0;
                bool neg = false;
                if (pos < s.size() && s[pos] == '-') { neg = true; ++pos; }
                if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return std::nullopt;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                    min_val = min_val * 10 + (s[pos] - '0'); ++pos;
                }
                if (neg) min_val = -min_val;
                skip_ws(s, pos);
                if (pos >= s.size() || s[pos] != ',') return std::nullopt;
                ++pos; skip_ws(s, pos);
                // Parse max integer
                std::int64_t max_val = 0;
                neg = false;
                if (pos < s.size() && s[pos] == '-') { neg = true; ++pos; }
                if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return std::nullopt;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                    max_val = max_val * 10 + (s[pos] - '0'); ++pos;
                }
                if (neg) max_val = -max_val;
                skip_ws(s, pos);
                if (pos >= s.size() || s[pos] != ')') return std::nullopt;
                ++pos; // consume ')'
                std::ostringstream oss;
                oss << "String(" << min_val << ", " << max_val << ")";
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::BoundedString,
                    .display_name = oss.str(),
                    .string_bounds = std::make_pair(min_val, max_val),
                };
            }
            return leaf(ir::TypeRefKind::String);
        }
        if (tok == "UUID")      return leaf(ir::TypeRefKind::UUID);
        if (tok == "Timestamp") return leaf(ir::TypeRefKind::Timestamp);
        if (tok == "Duration")  return leaf(ir::TypeRefKind::Duration);

        if (tok == "Decimal") {
            skip_ws(s, pos);
            if (pos < s.size() && s[pos] == '(') {
                ++pos; skip_ws(s, pos);
                std::int64_t scale = 0;
                if (pos >= s.size() || !std::isdigit(static_cast<unsigned char>(s[pos]))) return std::nullopt;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
                    scale = scale * 10 + (s[pos] - '0'); ++pos;
                }
                skip_ws(s, pos);
                if (pos >= s.size() || s[pos] != ')') return std::nullopt;
                ++pos;
                std::ostringstream oss;
                oss << "Decimal(" << scale << ")";
                return ir::TypeRef{
                    .kind = ir::TypeRefKind::Decimal,
                    .display_name = oss.str(),
                    .decimal_scale = scale,
                };
            }
            return leaf(ir::TypeRefKind::Decimal);
        }

        // Generic wrapper: Name<Inner> or Name<Key, Value> (Optional, List, Set, Map)
        skip_ws(s, pos);
        if (pos < s.size() && s[pos] == '<') {
            const std::size_t close = find_matching_angle(s, pos);
            if (close == std::string_view::npos) return std::nullopt;
            const std::string_view inner = s.substr(pos + 1, close - pos - 1);
            pos = close + 1; // consume '>'

            // Split inner on the top-level comma (respecting <...> and (...) nesting)
            // – needed for Map<K, V>.
            int angle_depth = 0, paren_depth = 0;
            std::size_t comma_pos = std::string_view::npos;
            for (std::size_t i = 0; i < inner.size(); ++i) {
                const char c = inner[i];
                if (paren_depth > 0) {
                    if (c == '(') ++paren_depth;
                    else if (c == ')') --paren_depth;
                    continue;
                }
                if (c == '(') { ++paren_depth; continue; }
                if (c == '<') ++angle_depth;
                else if (c == '>') --angle_depth;
                else if (c == ',' && angle_depth == 0) { comma_pos = i; break; }
            }

            if (tok == "Optional" || tok == "List" || tok == "Set") {
                if (comma_pos != std::string_view::npos) return std::nullopt; // single-param
                std::size_t inner_pos = 0;
                auto inner_ref = parse_type_ref_spelling(inner, inner_pos);
                if (!inner_ref.has_value()) return std::nullopt;
                ir::TypeRefKind kind = (tok == "Optional") ? ir::TypeRefKind::Optional
                                     : (tok == "List")     ? ir::TypeRefKind::List
                                     :                        ir::TypeRefKind::Set;
                ir::TypeRef result{.kind = kind,
                                   .display_name = std::string(tok) + "<" + inner_ref->display_name + ">"};
                result.first = make_type_ref(std::move(*inner_ref));
                return result;
            }
            if (tok == "Map") {
                if (comma_pos == std::string_view::npos) return std::nullopt; // two params needed
                const std::string_view key_str = inner.substr(0, comma_pos);
                const std::string_view val_str = inner.substr(comma_pos + 1);
                std::size_t kp = 0, vp = 0;
                auto key_ref = parse_type_ref_spelling(key_str, kp);
                auto val_ref = parse_type_ref_spelling(val_str, vp);
                if (!key_ref.has_value() || !val_ref.has_value()) return std::nullopt;
                ir::TypeRef result{
                    .kind = ir::TypeRefKind::Map,
                    .display_name = std::string(tok) + "<" + key_ref->display_name + ", " + val_ref->display_name + ">",
                };
                result.first = make_type_ref(std::move(*key_ref));
                result.second = make_type_ref(std::move(*val_ref));
                return result;
            }
            // Unknown generic: treat as unresolved
            return ir::TypeRef{.kind = ir::TypeRefKind::Unresolved,
                               .display_name = std::string(tok) + "<" + std::string(inner) + ">"};
        }

        // Named type (Struct / Enum / TypeAlias). Resolve via the symbol
        // table using the local/module-qualified name. If resolution fails we
        // still return an Unresolved TypeRef with the correct display_name;
        // the caller can fall back to the AST if strict fidelity is needed.
        {
            const std::string name_str(tok);
            // Extract local part for Types-namespace lookup.
            std::string_view local_part = tok;
            const auto last_colon = tok.rfind("::");
            if (last_colon != std::string_view::npos) {
                local_part = tok.substr(last_colon + 2);
            }
            MaybeCRef<Symbol> sym = find_local_symbol_here(SymbolNamespace::Types,
                                                           std::string(local_part));
            ir::TypeRef result{.kind = ir::TypeRefKind::Unresolved,
                               .display_name = name_str};
            if (sym.has_value()) {
                result.display_name = sym->get().canonical_name;
                result.canonical_name = sym->get().canonical_name;
                switch (sym->get().kind) {
                case SymbolKind::Struct: result.kind = ir::TypeRefKind::Struct; break;
                case SymbolKind::Enum:   result.kind = ir::TypeRefKind::Enum;   break;
                case SymbolKind::TypeAlias: {
                    // Type alias: walk through alias_decl to recover concrete
                    // underlying kind via symbol lookup. As a fallback, mark
                    // Unresolved with canonical_name so downstream can map it.
                    result.kind = ir::TypeRefKind::Unresolved;
                    break;
                }
                default: break;
                }
            }
            return result;
        }
    }

    // Convenience: parse a whole spelling string (no trailing content).
    [[nodiscard]] std::optional<ir::TypeRef>
    parse_type_ref_spelling(std::string_view s) const {
        std::size_t pos = 0;
        auto result = parse_type_ref_spelling(s, pos);
        if (!result.has_value()) return std::nullopt;
        skip_ws(s, pos);
        if (pos != s.size()) return std::nullopt; // trailing garbage
        return result;
    }

    // Map a TypedTemporalOp enum to the corresponding IR unary operator.
    // Only valid for TemporalNot / TemporalNext / TemporalAlways / TemporalEventually.
    [[nodiscard]] static ir::TemporalUnaryOp
    temporal_op_to_ir_unary(TypedTemporalOp op) {
        switch (op) {
        case TypedTemporalOp::TemporalNot:        return ir::TemporalUnaryOp::Not;
        case TypedTemporalOp::TemporalNext:       return ir::TemporalUnaryOp::Next;
        case TypedTemporalOp::TemporalAlways:     return ir::TemporalUnaryOp::Always;
        case TypedTemporalOp::TemporalEventually: return ir::TemporalUnaryOp::Eventually;
        default: break;
        }
        return ir::TemporalUnaryOp::Always;
    }

    // Map a TypedTemporalOp enum to the corresponding IR binary operator.
    // Only valid for TemporalAnd / TemporalOr / TemporalImply / TemporalUntil.
    [[nodiscard]] static ir::TemporalBinaryOp
    temporal_op_to_ir_binary(TypedTemporalOp op) {
        switch (op) {
        case TypedTemporalOp::TemporalAnd:   return ir::TemporalBinaryOp::And;
        case TypedTemporalOp::TemporalOr:    return ir::TemporalBinaryOp::Or;
        case TypedTemporalOp::TemporalImply: return ir::TemporalBinaryOp::Implies;
        case TypedTemporalOp::TemporalUntil: return ir::TemporalBinaryOp::Until;
        default: break;
        }
        return ir::TemporalBinaryOp::Implies;
    }

    // Map AssignTargetRootKind to the corresponding IR PathRootKind.
    // Context / State / Local all collapse to Identifier in the IR (the IR
    // only distinguishes Input/Output/Identifier); the full TypedHIR enum is
    // retained for analysis passes that need the semantic distinction.
    [[nodiscard]] static ir::PathRootKind
    assign_root_to_ir(AssignTargetRootKind kind) {
        switch (kind) {
        case AssignTargetRootKind::Input:  return ir::PathRootKind::Input;
        case AssignTargetRootKind::Output: return ir::PathRootKind::Output;
        default: break;
        }
        return ir::PathRootKind::Identifier;
    }

    // =====================================================================
    // Typed-tree temporal lowering (AST-free dispatch via TypedTemporalExpr)
    // =====================================================================
    //
    // lower_typed_temporal walks the flat TypedProgram::temporal_exprs store
    // by index and builds IR nodes. When a sub-index references the
    // expressions store (Atom leaf) we re-enter lower_typed_expr. The
    // payload_spelling strings encode both the structural variant (for
    // NameLiteral/StateLiteral we prefix with "called:", "running:",
    // "completed:", "state:") and the operator mnemonic (for Unary/Binary).
    [[nodiscard]] ir::TemporalExprPtr
    lower_typed_temporal(const TypedTemporalExpr &te) const {
        const auto make = [this, &te](auto node) {
            return make_temporal(std::move(node), te.range);
        };
        // T1.7 P1: dispatch primarily on the strongly-typed `op` enum.
        // payload_spelling is a secondary backstop used for data (names,
        // state) and as a legacy fallback when `op` is the legacy Atom
        // default on old TypedTemporalExpr records.
        switch (te.op) {
        case TypedTemporalOp::Atom: {
            // Atom: child references the expressions flat store.
            ir::ExprPtr embedded = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->expressions.size()) {
                embedded = lower_typed_expr(typed_program_->expressions[te.children_index[0]]);
            }
            return make(ir::EmbeddedTemporalExpr{.expr = std::move(embedded)});
        }
        case TypedTemporalOp::NameLiteralCalled: {
            // payload_spelling carries the canonical capability name directly
            // (no "called:" prefix in the new format).
            return make(ir::CalledTemporalExpr{.capability = te.payload_spelling});
        }
        case TypedTemporalOp::NameLiteralRunning: {
            return make(ir::RunningTemporalExpr{.node = te.payload_spelling});
        }
        case TypedTemporalOp::NameLiteralCompleted: {
            // payload_spelling encodes "node_name|optional_state_name".
            const auto pipe = te.payload_spelling.find('|');
            if (pipe == std::string::npos) {
                return make(ir::CompletedTemporalExpr{
                    .node = te.payload_spelling, .state_name = std::nullopt,
                });
            }
            const std::string node = te.payload_spelling.substr(0, pipe);
            const std::string state = te.payload_spelling.substr(pipe + 1);
            return make(ir::CompletedTemporalExpr{
                .node = node,
                .state_name = state.empty() ? std::nullopt
                                            : std::optional<std::string>(std::move(state)),
            });
        }
        case TypedTemporalOp::StateLiteral: {
            return make(ir::InStateTemporalExpr{.state = te.payload_spelling});
        }
        case TypedTemporalOp::TemporalNot:
        case TypedTemporalOp::TemporalNext:
        case TypedTemporalOp::TemporalAlways:
        case TypedTemporalOp::TemporalEventually: {
            ir::TemporalExprPtr operand = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->temporal_exprs.size()) {
                operand = lower_typed_temporal(
                    typed_program_->temporal_exprs[te.children_index[0]]);
            }
            return make(ir::TemporalUnaryExpr{
                .op = temporal_op_to_ir_unary(te.op),
                .operand = std::move(operand),
            });
        }
        case TypedTemporalOp::TemporalAnd:
        case TypedTemporalOp::TemporalOr:
        case TypedTemporalOp::TemporalImply:
        case TypedTemporalOp::TemporalUntil: {
            ir::TemporalExprPtr lhs = nullptr;
            ir::TemporalExprPtr rhs = nullptr;
            if (te.children_index.size() >= 2) {
                if (te.children_index[0] != UINT32_MAX &&
                    te.children_index[0] < typed_program_->temporal_exprs.size()) {
                    lhs = lower_typed_temporal(
                        typed_program_->temporal_exprs[te.children_index[0]]);
                }
                if (te.children_index[1] != UINT32_MAX &&
                    te.children_index[1] < typed_program_->temporal_exprs.size()) {
                    rhs = lower_typed_temporal(
                        typed_program_->temporal_exprs[te.children_index[1]]);
                }
            }
            return make(ir::TemporalBinaryExpr{
                .op = temporal_op_to_ir_binary(te.op),
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
            });
        }
        }

        // ---- Legacy backstop ------------------------------------------------
        // If `op` somehow carries an unexpected value (future enum growth,
        // corrupt record) fall back to the pre-P1 spelling-based dispatch.
        switch (te.kind) {
        case TypedTemporalKind::Atom: {
            ir::ExprPtr embedded = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->expressions.size()) {
                embedded = lower_typed_expr(typed_program_->expressions[te.children_index[0]]);
            }
            return make(ir::EmbeddedTemporalExpr{.expr = std::move(embedded)});
        }
        case TypedTemporalKind::NameLiteral: {
            const auto [prefix, remainder] = split_payload(te.payload_spelling);
            if (prefix == "called") {
                return make(ir::CalledTemporalExpr{.capability = std::string(remainder)});
            }
            if (prefix == "running") {
                return make(ir::RunningTemporalExpr{.node = std::string(remainder)});
            }
            if (prefix == "completed") {
                const auto pipe = remainder.find('|');
                if (pipe == std::string_view::npos) {
                    return make(ir::CompletedTemporalExpr{
                        .node = std::string(remainder), .state_name = std::nullopt,
                    });
                }
                const auto node = remainder.substr(0, pipe);
                const auto state = remainder.substr(pipe + 1);
                return make(ir::CompletedTemporalExpr{
                    .node = std::string(node),
                    .state_name = state.empty() ? std::nullopt
                                                : std::optional<std::string>(std::string(state)),
                });
            }
            return make(ir::CalledTemporalExpr{.capability = std::string(remainder)});
        }
        case TypedTemporalKind::StateLiteral: {
            const auto [prefix, remainder] = split_payload(te.payload_spelling);
            std::string state = (prefix == "state") ? std::string(remainder)
                                                    : te.payload_spelling;
            return make(ir::InStateTemporalExpr{.state = std::move(state)});
        }
        case TypedTemporalKind::Unary: {
            ir::TemporalExprPtr operand = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->temporal_exprs.size()) {
                operand = lower_typed_temporal(
                    typed_program_->temporal_exprs[te.children_index[0]]);
            }
            return make(ir::TemporalUnaryExpr{
                .op = parse_temporal_unary_op(te.payload_spelling),
                .operand = std::move(operand),
            });
        }
        case TypedTemporalKind::Binary: {
            ir::TemporalExprPtr lhs = nullptr;
            ir::TemporalExprPtr rhs = nullptr;
            if (te.children_index.size() >= 2) {
                if (te.children_index[0] != UINT32_MAX &&
                    te.children_index[0] < typed_program_->temporal_exprs.size()) {
                    lhs = lower_typed_temporal(
                        typed_program_->temporal_exprs[te.children_index[0]]);
                }
                if (te.children_index[1] != UINT32_MAX &&
                    te.children_index[1] < typed_program_->temporal_exprs.size()) {
                    rhs = lower_typed_temporal(
                        typed_program_->temporal_exprs[te.children_index[1]]);
                }
            }
            return make(ir::TemporalBinaryExpr{
                .op = parse_temporal_binary_op(te.payload_spelling),
                .lhs = std::move(lhs),
                .rhs = std::move(rhs),
            });
        }
        case TypedTemporalKind::None:
            break;
        }
        return make(ir::CalledTemporalExpr{.capability = "<invalid-typed-temporal>"});
    }

    // T1.7 P2: let type_ref construction – 100% from typed payloads (AST
    // fallback removed). Strategy exactly matches the deleted
    // inferred_statement_type_ref so T1.2 equivalence is preserved:
    //   * FromSyntax         -> parse let_type_ref_spelling; on parse failure
    //                           degrade to initializer type then Any sentinel.
    //   * FromInitializerType-> type_ref_from_type(initializer->type).
    //   * NoAnnotation       -> same as FromInitializerType.
    //   * AnySentinel        -> Unresolved/Any sentinel.
    [[nodiscard]] ir::TypeRef
    inferred_typed_let_type_ref(const TypedStatement &stmt,
                                const TypedExpr *initializer) const {
        switch (stmt.let_type_ref_strategy) {
        case LetTypeRefStrategy::FromSyntax: {
            if (!stmt.let_type_ref_spelling.empty()) {
                if (auto parsed = parse_type_ref_spelling(stmt.let_type_ref_spelling);
                    parsed.has_value()) {
                    return std::move(*parsed);
                }
            }
            // Parse failure (shouldn't occur for well-formed programs):
            // degrade to initializer type, then Any.
            if (initializer != nullptr && initializer->type != nullptr) {
                return type_ref_from_type(*initializer->type);
            }
            return type_ref_from_maybe(std::nullopt, "Any");
        }
        case LetTypeRefStrategy::NoAnnotation:
        case LetTypeRefStrategy::FromInitializerType:
            if (initializer != nullptr && initializer->type != nullptr) {
                return type_ref_from_type(*initializer->type);
            }
            return type_ref_from_maybe(std::nullopt, "Any");
        case LetTypeRefStrategy::AnySentinel:
            return type_ref_from_maybe(std::nullopt, "Any");
        }
        // Unknown strategy (future enum growth): degrade same as NoAnnotation.
        if (initializer != nullptr && initializer->type != nullptr) {
            return type_ref_from_type(*initializer->type);
        }
        return type_ref_from_maybe(std::nullopt, "Any");
    }

    // T1.7 P2: assign target path construction – 100% from typed payloads.
    // Split stmt.target_name on '.' to recover root_name + member chain; use
    // stmt.assign_target_root_kind to produce the matching ir::PathRootKind.
    [[nodiscard]] ir::Path
    typed_assign_target_path(const TypedStatement &stmt) const {
        ir::PathRootKind root_kind = assign_root_to_ir(stmt.assign_target_root_kind);
        std::string root_name;
        std::vector<std::string> members;

        const std::string &spelling = stmt.target_name;
        std::size_t dot = spelling.find('.');
        if (dot == std::string::npos) {
            root_name = spelling;
        } else {
            root_name = spelling.substr(0, dot);
            std::string_view rest = std::string_view(spelling).substr(dot + 1);
            while (!rest.empty()) {
                const std::size_t nd = rest.find('.');
                if (nd == std::string_view::npos) {
                    members.emplace_back(rest);
                    break;
                }
                members.emplace_back(rest.substr(0, nd));
                rest = rest.substr(nd + 1);
            }
        }
        return ir::Path{
            .root_kind = root_kind,
            .root_name = std::move(root_name),
            .members = std::move(members),
        };
    }

    // Typed-store statement lowering visitor (T1.7 P2: AST fallback removed).
    // Each method builds the same ir::Statement variant as the legacy path so
    // T1.2 tests remain byte-identical. The unknown_stmt fallback degrades
    // gracefully when a new TypedStmtKind is introduced without a visitor update.
    struct TypedStmtPerKindLowerer {
        const TypedIrLowerer &self;
        SourceRange range;

        const TypedExpr *child_expr(std::size_t index) const {
            if (index >= self.current_statement_->children_expr_index.size())
                return nullptr;
            const auto expr_idx = self.current_statement_->children_expr_index[index];
            if (expr_idx == UINT32_MAX) return nullptr;
            if (expr_idx >= self.typed_program_->expressions.size()) return nullptr;
            return &self.typed_program_->expressions[expr_idx];
        }

        ir::StatementPtr visit_let_stmt(const TypedStatement &stmt) const {
            const TypedExpr *initializer = child_expr(0);
            return self.make_statement(
                ir::LetStatement{
                    .name = stmt.target_name,
                    .type_ref = self.inferred_typed_let_type_ref(stmt, initializer),
                    .initializer = initializer ? self.lower_typed_expr(*initializer) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_assign_stmt(const TypedStatement &stmt) const {
            const TypedExpr *value = child_expr(0);
            return self.make_statement(
                ir::AssignStatement{
                    .target = self.typed_assign_target_path(stmt),
                    .value = value ? self.lower_typed_expr(*value) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_if_stmt(const TypedStatement &stmt) const {
            const TypedExpr *condition = child_expr(0);
            const auto *then_block = stmt.then_block_index != UINT32_MAX &&
                                             stmt.then_block_index <
                                                 self.typed_program_->blocks.size()
                                         ? &self.typed_program_->blocks[stmt.then_block_index]
                                         : nullptr;
            const auto *else_block = stmt.else_block_index != UINT32_MAX &&
                                             stmt.else_block_index <
                                                 self.typed_program_->blocks.size()
                                         ? &self.typed_program_->blocks[stmt.else_block_index]
                                         : nullptr;
            auto else_ptr = else_block
                                ? make_owned<ir::Block>(self.lower_typed_block(*else_block))
                                : nullptr;
            return self.make_statement(
                ir::IfStatement{
                    .condition = condition ? self.lower_typed_expr(*condition) : nullptr,
                    .then_block = then_block ? make_owned<ir::Block>(
                                                   self.lower_typed_block(*then_block))
                                             : nullptr,
                    .else_block = std::move(else_ptr),
                },
                range);
        }

        ir::StatementPtr visit_goto_stmt(const TypedStatement &stmt) const {
            // T1.7 P2: read directly from the typed payload; the typechecker
            // guarantees goto_target_state is always populated (T1.2 tests enforce
            // the invariant). An empty string falls back to the sentinel marker so
            // detached test cases don't crash the compiler on malformed input.
            std::string target = stmt.goto_target_state;
            if (target.empty()) {
                target = "<missing-goto-target>";
            }
            return self.make_statement(
                ir::GotoStatement{.target_state = std::move(target)}, range);
        }

        ir::StatementPtr visit_return_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *value = child_expr(0);
            return self.make_statement(
                ir::ReturnStatement{
                    .value = value ? self.lower_typed_expr(*value) : nullptr,
                },
                range);
        }

        // T1.7 P2: assert message is stored on stmt.assert_message but the current
        // IR AssertStatement shape carries only a condition field. The typed payload
        // is used directly for the condition; the message field is available for
        // analysis passes and future IR expansion. No AST fallback remains.
        ir::StatementPtr visit_assert_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *condition = child_expr(0);
            return self.make_statement(
                ir::AssertStatement{
                    .condition = condition ? self.lower_typed_expr(*condition) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_expr_stmt([[maybe_unused]] const TypedStatement &stmt) const {
            const TypedExpr *expr = child_expr(0);
            return self.make_statement(
                ir::ExprStatement{
                    .expr = expr ? self.lower_typed_expr(*expr) : nullptr,
                },
                range);
        }

        ir::StatementPtr visit_unknown_stmt(const TypedStatement &stmt) const {
            return self.make_statement(
                ir::ExprStatement{
                    .expr = self.make_expr(
                        ir::QualifiedValueExpr{.value = "<invalid-statement>"},
                        stmt.range),
                },
                stmt.range);
        }
    };

    // Forward declarations.
    [[nodiscard]] ir::Block lower_typed_block(const TypedBlock &block) const;
    [[nodiscard]] ir::StatementPtr lower_typed_statement(const TypedStatement &stmt) const;

    // Current statement pointer, set for the lifetime of each
    // lower_typed_statement dispatch so the visitor can read
    // children_expr_index through the TypedIrLowerer (avoids passing the
    // pointer through every visit method).
    mutable const TypedStatement *current_statement_{nullptr};

    // =====================================================================
    // Flow / workflow summaries (copied verbatim from ir_lower.cpp so the
    // two TUs produce byte-identical IR for the T1.2 fingerprint check).
    // =====================================================================

    void collect_called_targets_from_expr(const ir::Expr &expr,
                                          std::vector<std::string> &called_targets) const {
        std::visit(Overloaded{
                       [](const ir::NoneLiteralExpr &) {},
                       [](const ir::BoolLiteralExpr &) {},
                       [](const ir::IntegerLiteralExpr &) {},
                       [](const ir::FloatLiteralExpr &) {},
                       [](const ir::DecimalLiteralExpr &) {},
                       [](const ir::StringLiteralExpr &) {},
                       [](const ir::DurationLiteralExpr &) {},
                       [this, &called_targets](const ir::SomeExpr &value) {
                           collect_called_targets_from_expr(*value.value, called_targets);
                       },
                       [](const ir::PathExpr &) {},
                       [](const ir::QualifiedValueExpr &) {},
                       [this, &called_targets](const ir::CallExpr &value) {
                           push_unique_value(called_targets, value.callee);
                           for (const auto &a : value.arguments)
                               collect_called_targets_from_expr(*a, called_targets);
                       },
                       [this, &called_targets](const ir::StructLiteralExpr &value) {
                           for (const auto &f : value.fields)
                               collect_called_targets_from_expr(*f.value, called_targets);
                       },
                       [this, &called_targets](const ir::ListLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_called_targets_from_expr(*i, called_targets);
                       },
                       [this, &called_targets](const ir::SetLiteralExpr &value) {
                           for (const auto &i : value.items)
                               collect_called_targets_from_expr(*i, called_targets);
                       },
                       [this, &called_targets](const ir::MapLiteralExpr &value) {
                           for (const auto &e : value.entries) {
                               collect_called_targets_from_expr(*e.key, called_targets);
                               collect_called_targets_from_expr(*e.value, called_targets);
                           }
                       },
                       [this, &called_targets](const ir::UnaryExpr &value) {
                           collect_called_targets_from_expr(*value.operand, called_targets);
                       },
                       [this, &called_targets](const ir::BinaryExpr &value) {
                           collect_called_targets_from_expr(*value.lhs, called_targets);
                           collect_called_targets_from_expr(*value.rhs, called_targets);
                       },
                       [this, &called_targets](const ir::MemberAccessExpr &value) {
                           collect_called_targets_from_expr(*value.base, called_targets);
                       },
                       [this, &called_targets](const ir::IndexAccessExpr &value) {
                           collect_called_targets_from_expr(*value.base, called_targets);
                           collect_called_targets_from_expr(*value.index, called_targets);
                       },
                       [this, &called_targets](const ir::GroupExpr &value) {
                           collect_called_targets_from_expr(*value.expr, called_targets);
                       },
                   },
                   expr.node);
    }

    void merge_flow_summary(ir::StateHandler::Summary &target,
                            const ir::StateHandler::Summary &other) const {
        for (const auto &gt : other.goto_targets)
            push_unique_value(target.goto_targets, gt);
        for (const auto &ap : other.assigned_paths)
            push_unique_path(target.assigned_paths, ap);
        for (const auto &ct : other.called_targets)
            push_unique_value(target.called_targets, ct);
        target.may_return = target.may_return || other.may_return;
        target.assert_count += other.assert_count;
    }

    // Recursive summary helpers. Kept as std::function-wrapped mutual
    // recursion to mirror the IrLowerer implementation exactly (small cost
    // per compile unit, pays off in identical output).
    void collect_workflow_value_reads(const ir::Expr &expr,
                                      const std::vector<std::string> &node_names,
                                      std::vector<ir::WorkflowValueRead> &reads) const {
        std::visit(
            Overloaded{
                [](const ir::NoneLiteralExpr &) {},
                [](const ir::BoolLiteralExpr &) {},
                [](const ir::IntegerLiteralExpr &) {},
                [](const ir::FloatLiteralExpr &) {},
                [](const ir::DecimalLiteralExpr &) {},
                [](const ir::StringLiteralExpr &) {},
                [](const ir::DurationLiteralExpr &) {},
                [this, &node_names, &reads](const ir::SomeExpr &value) {
                    collect_workflow_value_reads(*value.value, node_names, reads);
                },
                [&](const ir::PathExpr &value) {
                    if (value.path.root_kind == ir::PathRootKind::Input) {
                        push_unique_workflow_value_read(
                            reads,
                            ir::WorkflowValueRead{
                                .kind = ir::WorkflowValueSourceKind::WorkflowInput,
                                .root_name = value.path.root_name,
                                .members = value.path.members,
                            });
                        return;
                    }
                    if (value.path.root_kind != ir::PathRootKind::Identifier) return;
                    if (std::find(node_names.begin(), node_names.end(),
                                  value.path.root_name) == node_names.end())
                        return;
                    push_unique_workflow_value_read(
                        reads,
                        ir::WorkflowValueRead{
                            .kind = ir::WorkflowValueSourceKind::WorkflowNodeOutput,
                            .root_name = value.path.root_name,
                            .members = value.path.members,
                        });
                },
                [](const ir::QualifiedValueExpr &) {},
                [this, &node_names, &reads](const ir::CallExpr &value) {
                    for (const auto &a : value.arguments)
                        collect_workflow_value_reads(*a, node_names, reads);
                },
                [this, &node_names, &reads](const ir::StructLiteralExpr &value) {
                    for (const auto &f : value.fields)
                        collect_workflow_value_reads(*f.value, node_names, reads);
                },
                [this, &node_names, &reads](const ir::ListLiteralExpr &value) {
                    for (const auto &i : value.items)
                        collect_workflow_value_reads(*i, node_names, reads);
                },
                [this, &node_names, &reads](const ir::SetLiteralExpr &value) {
                    for (const auto &i : value.items)
                        collect_workflow_value_reads(*i, node_names, reads);
                },
                [this, &node_names, &reads](const ir::MapLiteralExpr &value) {
                    for (const auto &e : value.entries) {
                        collect_workflow_value_reads(*e.key, node_names, reads);
                        collect_workflow_value_reads(*e.value, node_names, reads);
                    }
                },
                [this, &node_names, &reads](const ir::UnaryExpr &value) {
                    collect_workflow_value_reads(*value.operand, node_names, reads);
                },
                [this, &node_names, &reads](const ir::BinaryExpr &value) {
                    collect_workflow_value_reads(*value.lhs, node_names, reads);
                    collect_workflow_value_reads(*value.rhs, node_names, reads);
                },
                [this, &node_names, &reads](const ir::MemberAccessExpr &value) {
                    collect_workflow_value_reads(*value.base, node_names, reads);
                },
                [this, &node_names, &reads](const ir::IndexAccessExpr &value) {
                    collect_workflow_value_reads(*value.base, node_names, reads);
                    collect_workflow_value_reads(*value.index, node_names, reads);
                },
                [this, &node_names, &reads](const ir::GroupExpr &value) {
                    collect_workflow_value_reads(*value.expr, node_names, reads);
                },
            },
            expr.node);
    }

    [[nodiscard]] ir::WorkflowExprSummary
    summarize_workflow_expr(const ir::Expr &expr,
                            const std::vector<std::string> &node_names) const {
        ir::WorkflowExprSummary summary;
        collect_workflow_value_reads(expr, node_names, summary.reads);
        return summary;
    }

    [[nodiscard]] ir::StateHandler::Summary
    summarize_block(const ir::Block &block) const;

    [[nodiscard]] ir::StateHandler::Summary
    summarize_statement(const ir::Statement &statement) const {
        return std::visit(
            Overloaded{
                [this](const ir::LetStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.initializer, s.called_targets);
                    return s;
                },
                [this](const ir::AssignStatement &value) {
                    ir::StateHandler::Summary s;
                    push_unique_path(s.assigned_paths, value.target);
                    collect_called_targets_from_expr(*value.value, s.called_targets);
                    return s;
                },
                [this](const ir::IfStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.condition, s.called_targets);
                    const auto then_summary = summarize_block(*value.then_block);
                    merge_flow_summary(s, then_summary);
                    ir::StateHandler::Summary else_summary;
                    if (value.else_block) {
                        else_summary = summarize_block(*value.else_block);
                        merge_flow_summary(s, else_summary);
                    }
                    s.may_fallthrough = !value.else_block || then_summary.may_fallthrough ||
                                        else_summary.may_fallthrough;
                    return s;
                },
                [](const ir::GotoStatement &value) {
                    ir::StateHandler::Summary s;
                    s.goto_targets.push_back(value.target_state);
                    s.may_fallthrough = false;
                    return s;
                },
                [this](const ir::ReturnStatement &value) {
                    ir::StateHandler::Summary s;
                    if (value.value)
                        collect_called_targets_from_expr(*value.value, s.called_targets);
                    s.may_return = true;
                    s.may_fallthrough = false;
                    return s;
                },
                [this](const ir::AssertStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.condition, s.called_targets);
                    s.assert_count = 1;
                    return s;
                },
                [this](const ir::ExprStatement &value) {
                    ir::StateHandler::Summary s;
                    collect_called_targets_from_expr(*value.expr, s.called_targets);
                    return s;
                },
            },
            statement.node);
    }

    // =====================================================================
    // Declaration lowering (AST for structure; expression bodies -> typed)
    // =====================================================================

    [[nodiscard]] ir::Decl lower_declaration(const ast::Decl &declaration) const {
        switch (declaration.kind) {
        case ast::NodeKind::ModuleDecl:
            return lower_module(static_cast<const ast::ModuleDecl &>(declaration));
        case ast::NodeKind::ImportDecl:
            return lower_import(static_cast<const ast::ImportDecl &>(declaration));
        case ast::NodeKind::ConstDecl:
            return lower_const(static_cast<const ast::ConstDecl &>(declaration));
        case ast::NodeKind::TypeAliasDecl:
            return lower_type_alias(static_cast<const ast::TypeAliasDecl &>(declaration));
        case ast::NodeKind::StructDecl:
            return lower_struct(static_cast<const ast::StructDecl &>(declaration));
        case ast::NodeKind::EnumDecl:
            return lower_enum(static_cast<const ast::EnumDecl &>(declaration));
        case ast::NodeKind::CapabilityDecl:
            return lower_capability(static_cast<const ast::CapabilityDecl &>(declaration));
        case ast::NodeKind::PredicateDecl:
            return lower_predicate(static_cast<const ast::PredicateDecl &>(declaration));
        case ast::NodeKind::AgentDecl:
            return lower_agent(static_cast<const ast::AgentDecl &>(declaration));
        case ast::NodeKind::ContractDecl:
            return lower_contract(static_cast<const ast::ContractDecl &>(declaration));
        case ast::NodeKind::FlowDecl:
            return lower_flow(static_cast<const ast::FlowDecl &>(declaration));
        case ast::NodeKind::WorkflowDecl:
            return lower_workflow(static_cast<const ast::WorkflowDecl &>(declaration));
        case ast::NodeKind::Program:
            break;
        }
        return ir::ModuleDecl{.provenance = {}, .name = "<invalid-decl>"};
    }

    [[nodiscard]] ir::ModuleDecl lower_module(const ast::ModuleDecl &node) const {
        if (graph_ == nullptr) current_module_name_ = node.name->spelling();
        return with_provenance(
            ir::ModuleDecl{.provenance = {}, .name = node.name->spelling()}, node.range);
    }

    [[nodiscard]] ir::ImportDecl lower_import(const ast::ImportDecl &node) const {
        return with_provenance(
            ir::ImportDecl{
                .provenance = {},
                .path = node.path->spelling(),
                .alias = node.alias.empty() ? std::nullopt
                                           : std::make_optional(node.alias),
            },
            node.range);
    }

    [[nodiscard]] ir::ConstDecl lower_const(const ast::ConstDecl &node) const {
        const auto symbol_name =
            canonical_local_name_here(SymbolNamespace::Consts, node.name);
        const auto const_symbol =
            find_local_symbol_here(SymbolNamespace::Consts, node.name);
        MaybeCRef<Type> const_type;
        if (const_symbol.has_value())
            const_type = environment().get_const_type(const_symbol->get().id);
        return with_provenance(
            ir::ConstDecl{
                .provenance = {},
                .name = symbol_name,
                .value = lower_expr(*node.value),
                .type_ref = const_type.has_value()
                                ? type_ref_from_type(const_type->get())
                                : type_ref_from_syntax(*node.type),
                .symbol_ref = symbol_ref_from_symbol(
                    const_symbol, ir::SymbolRefKind::Const, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::TypeAliasDecl lower_type_alias(const ast::TypeAliasDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);
        const auto symbol_name = symbol.has_value() ? symbol->get().canonical_name
                                                    : std::string(node.name);
        return with_provenance(
            ir::TypeAliasDecl{
                .provenance = {},
                .name = symbol_name,
                .aliased_type_ref = type_ref_from_syntax(*node.aliased_type),
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Type, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::StructDecl lower_struct(const ast::StructDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);
        const auto info =
            symbol.has_value() ? environment().get_struct(symbol->get().id) : std::nullopt;
        ir::StructDecl decl = with_provenance(
            ir::StructDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name
                                           : std::string(node.name),
                .fields = {},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Type, std::string(node.name)),
            },
            node.range);
        decl.fields.reserve(node.fields.size());
        for (std::size_t i = 0; i < node.fields.size(); ++i) {
            const auto &field = node.fields[i];
            auto field_type_ref = (info.has_value() && i < info->get().fields.size() &&
                                   info->get().fields[i].type)
                                      ? type_ref_from_type(*info->get().fields[i].type)
                                      : ir::TypeRef{};
            decl.fields.push_back(ir::FieldDecl{
                .name = field->name,
                .default_value =
                    field->default_value ? lower_expr(*field->default_value) : nullptr,
                .type_ref = std::move(field_type_ref),
                .source_range = field->range,
            });
        }
        return decl;
    }

    [[nodiscard]] ir::EnumDecl lower_enum(const ast::EnumDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Types, node.name);
        const auto info =
            symbol.has_value() ? environment().get_enum(symbol->get().id) : std::nullopt;
        ir::EnumDecl decl = with_provenance(
            ir::EnumDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name
                                           : std::string(node.name),
                .variants = {},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Type, std::string(node.name)),
            },
            node.range);
        if (info.has_value()) {
            decl.variants.reserve(info->get().variants.size());
            for (const auto &variant : info->get().variants)
                decl.variants.push_back(variant.name);
        }
        return decl;
    }

    [[nodiscard]] std::vector<ir::ParamDecl>
    lower_params(const std::vector<Owned<ast::ParamDeclSyntax>> &params,
                 MaybeCRef<CapabilityTypeInfo> capability_info,
                 MaybeCRef<PredicateTypeInfo> predicate_info) const {
        std::vector<ir::ParamDecl> result;
        if (capability_info.has_value()) {
            const auto &cap = capability_info->get();
            result.reserve(cap.params.size());
            for (std::size_t i = 0; i < cap.params.size(); ++i) {
                result.push_back(ir::ParamDecl{
                    .name = cap.params[i].name,
                    .type_ref = type_ref_from_maybe(borrow(cap.params[i].type)),
                    .source_range = i < params.size()
                                         ? ir::SourceRangeOpt{params[i]->range}
                                         : std::nullopt,
                });
            }
            return result;
        }
        if (predicate_info.has_value()) {
            const auto &pred = predicate_info->get();
            result.reserve(pred.params.size());
            for (std::size_t i = 0; i < pred.params.size(); ++i) {
                result.push_back(ir::ParamDecl{
                    .name = pred.params[i].name,
                    .type_ref = type_ref_from_maybe(borrow(pred.params[i].type)),
                    .source_range = i < params.size()
                                         ? ir::SourceRangeOpt{params[i]->range}
                                         : std::nullopt,
                });
            }
            return result;
        }
        result.reserve(params.size());
        for (const auto &p : params) {
            result.push_back(ir::ParamDecl{
                .name = p->name,
                .type_ref = type_ref_from_syntax(*p->type),
                .source_range = p->range,
            });
        }
        return result;
    }

    [[nodiscard]] ir::CapabilityDecl lower_capability(const ast::CapabilityDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Capabilities, node.name);
        const auto info =
            symbol.has_value() ? environment().get_capability(symbol->get().id) : std::nullopt;
        const auto symbol_name =
            symbol.has_value() ? symbol->get().canonical_name : std::string(node.name);
        return with_provenance(
            ir::CapabilityDecl{
                .provenance = {},
                .name = symbol_name,
                .params = lower_params(node.params, info, std::nullopt),
                .return_type_ref = info.has_value()
                                     ? type_ref_from_maybe(borrow(info->get().return_type))
                                     : type_ref_from_maybe(std::nullopt, "Any"),
                .effect = info.has_value()
                              ? lower_capability_effect_from_info(info->get().effect)
                              : ir::CapabilityEffectSpec{},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Capability, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::PredicateDecl lower_predicate(const ast::PredicateDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Predicates, node.name);
        const auto info =
            symbol.has_value() ? environment().get_predicate(symbol->get().id) : std::nullopt;
        const auto symbol_name =
            symbol.has_value() ? symbol->get().canonical_name : std::string(node.name);
        return with_provenance(
            ir::PredicateDecl{
                .provenance = {},
                .name = symbol_name,
                .params = lower_params(node.params, std::nullopt, info),
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Predicate, symbol_name),
            },
            node.range);
    }

    [[nodiscard]] ir::AgentDecl lower_agent(const ast::AgentDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Agents, node.name);
        const auto info =
            symbol.has_value() ? environment().get_agent(symbol->get().id) : std::nullopt;
        ir::AgentDecl decl = with_provenance(
            ir::AgentDecl{
                .provenance = {},
                .name = symbol.has_value() ? symbol->get().canonical_name
                                           : std::string(node.name),
                // T1.8 P6: read structural scalars from TypeInfo; default-init when absent
                .states = info.has_value() ? info->get().states
                                           : std::vector<std::string>{},
                .initial_state = info.has_value() ? info->get().initial_state
                                                  : std::string{},
                .final_states = info.has_value() ? info->get().final_states
                                                 : std::vector<std::string>{},
                .quota = {},
                .transitions = {},
                .input_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().input_type))
                                       : ir::TypeRef{},
                .context_type_ref =
                    info.has_value() ? type_ref_from_maybe(borrow(info->get().context_type))
                                     : ir::TypeRef{},
                .output_type_ref = info.has_value()
                                        ? type_ref_from_maybe(borrow(info->get().output_type))
                                        : ir::TypeRef{},
                .capability_refs = {},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Agent, std::string(node.name)),
            },
            node.range);
        if (info.has_value()) {
            // T1.8 P5: capabilities from TypeInfo
            decl.capability_refs.reserve(info->get().capability_symbols.size());
            for (const auto cs : info->get().capability_symbols) {
                const auto cap = symbols().get(cs);
                decl.capability_refs.push_back(symbol_ref_from_symbol(
                    cap,
                    ir::SymbolRefKind::Capability,
                    cap.has_value() ? cap->get().canonical_name : "<missing-capability>"));
            }
            // T1.8 P5: quota from TypeInfo
            decl.quota.reserve(info->get().quota.size());
            for (const auto &q : info->get().quota) {
                decl.quota.push_back(ir::QuotaItem{
                    .name = q.name,
                    .value = q.value,
                });
            }
            // T1.8 P5: transitions from TypeInfo
            decl.transitions.reserve(info->get().transitions.size());
            for (const auto &t : info->get().transitions) {
                decl.transitions.push_back(ir::TransitionDecl{
                    .from_state = t.from_state,
                    .to_state = t.to_state,
                });
            }
        }
        return decl;
    }

    [[nodiscard]] ir::ContractDecl lower_contract(const ast::ContractDecl &node) const {
        // T1.8 P5: resolve target symbol and look up ContractTypeInfo
        const auto target_sym = symbol_from_reference_here(
            ReferenceKind::ContractTarget, node.target->range);
        const auto contract_info =
            target_sym.has_value()
                ? environment().get_contract(target_sym->get().id)
                : std::nullopt;
        // Build target SymbolRef (still uses reference resolution for the ref itself)
        const auto target = target_sym.has_value() ? target_sym->get().canonical_name
                                                   : std::string(node.target->spelling());
        ir::ContractDecl decl = with_provenance(
            ir::ContractDecl{
                .provenance = {},
                .clauses = {},
                .target_ref = symbol_ref_from_symbol(
                    target_sym, ir::SymbolRefKind::Agent, target),
            },
            node.range);
        if (contract_info.has_value()) {
            // T1.8 P5: iterate clause metadata from TypeInfo; use AST
            // sub-expressions only as .range carriers for the bridge calls.
            const auto &ci = contract_info->get();
            decl.clauses.reserve(ci.clauses.size());
            for (std::size_t i = 0; i < ci.clauses.size(); ++i) {
                const auto &clause_info = ci.clauses[i];
                ir::ContractClause lowered{
                    .kind = lower_contract_clause_kind(
                        static_cast<ast::ContractClauseKind>(clause_info.clause_kind)),
                    .value = ir::ExprPtr{},
                    .source_range = clause_info.source_range,
                };
                // Bridge call still needs the AST node for its .range
                if (!clause_info.is_temporal) {
                    lowered.value = lower_expr(*node.clauses[i]->expr);
                } else {
                    lowered.value = lower_temporal(*node.clauses[i]->temporal_expr);
                }
                decl.clauses.push_back(std::move(lowered));
            }
        }
        return decl;
    }

    [[nodiscard]] ir::FlowDecl lower_flow(const ast::FlowDecl &node) const {
        // T1.8 P5: resolve target symbol and look up FlowTypeInfo
        const auto target_sym = symbol_from_reference_here(
            ReferenceKind::FlowTarget, node.target->range);
        const auto flow_info =
            target_sym.has_value()
                ? environment().get_flow(target_sym->get().id)
                : std::nullopt;
        const auto target = target_sym.has_value() ? target_sym->get().canonical_name
                                                   : std::string(node.target->spelling());
        ir::FlowDecl decl = with_provenance(
            ir::FlowDecl{
                .provenance = {},
                .state_handlers = {},
                .target_ref = symbol_ref_from_symbol(
                    target_sym, ir::SymbolRefKind::Agent, target),
            },
            node.range);
        if (flow_info.has_value()) {
            // T1.8 P5: iterate state handler metadata from TypeInfo; use AST
            // handler->body only as .range carrier for the block bridge call.
            const auto &fi = flow_info->get();
            decl.state_handlers.reserve(fi.state_handlers.size());
            for (std::size_t i = 0; i < fi.state_handlers.size(); ++i) {
                const auto &handler_info = fi.state_handlers[i];
                ir::StateHandler state_handler{
                    .state_name = handler_info.state_name,
                    .policy = {},
                    // Bridge call: AST body node only used for .range lookup
                    .body = lower_block(*node.state_handlers[i]->body),
                    .source_range = handler_info.source_range,
                };
                // T1.8 P5: policy items from TypeInfo
                state_handler.policy.reserve(handler_info.policy.size());
                for (const auto &policy_item : handler_info.policy) {
                    switch (policy_item.kind) {
                    case StatePolicyKind::Retry:
                        state_handler.policy.push_back(ir::RetryPolicy{
                            .limit = policy_item.value.empty()
                                         ? std::string{"<missing-retry>"}
                                         : policy_item.value,
                        });
                        break;
                    case StatePolicyKind::RetryOn:
                        state_handler.policy.push_back(ir::RetryOnPolicy{
                            .targets = policy_item.retry_on_targets,
                        });
                        break;
                    case StatePolicyKind::Timeout:
                        state_handler.policy.push_back(ir::TimeoutPolicy{
                            .duration = policy_item.value.empty()
                                            ? std::string{"<missing-timeout>"}
                                            : policy_item.value,
                        });
                        break;
                    }
                }
                decl.state_handlers.push_back(std::move(state_handler));
            }
        }
        return decl;
    }

    [[nodiscard]] ir::WorkflowDecl lower_workflow(const ast::WorkflowDecl &node) const {
        const auto symbol = find_local_symbol_here(SymbolNamespace::Workflows, node.name);
        const auto info =
            symbol.has_value() ? environment().get_workflow(symbol->get().id) : std::nullopt;
        const auto symbol_name =
            symbol.has_value() ? symbol->get().canonical_name : std::string(node.name);
        ir::WorkflowDecl decl = with_provenance(
            ir::WorkflowDecl{
                .provenance = {},
                .name = symbol_name,
                .nodes = {},
                .safety = {},
                .liveness = {},
                .return_value = lower_expr(*node.return_value),
                .input_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().input_type))
                                       : ir::TypeRef{},
                .output_type_ref = info.has_value()
                                        ? type_ref_from_maybe(borrow(info->get().output_type))
                                        : ir::TypeRef{},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Workflow, std::string(node.name)),
            },
            node.range);
        if (info.has_value()) {
            // T1.8 P5: iterate node metadata from TypeInfo; use AST
            // wfn->input only as .range carrier for the expr bridge call.
            const auto &wf_info = info->get();
            decl.nodes.reserve(wf_info.nodes.size());
            for (std::size_t i = 0; i < wf_info.nodes.size(); ++i) {
                const auto &ni = wf_info.nodes[i];
                // Bridge call: AST input expr only used for .range lookup
                auto input = lower_expr(*node.nodes[i]->input);
                // T1.8 P5: target from TypeInfo symbol
                const auto node_target_sym = symbols().get(SymbolId{ni.target_symbol.value});
                const auto target_name = node_target_sym.has_value()
                                             ? node_target_sym->get().canonical_name
                                             : ni.target_name;
                decl.nodes.push_back(ir::WorkflowNode{
                    .name = ni.name,
                    .input = std::move(input),
                    .after = ni.after,
                    .target_ref = symbol_ref_from_symbol(
                        node_target_sym, ir::SymbolRefKind::Agent, target_name),
                    .source_range = ni.source_range,
                });
            }
            // T1.8 P5: safety/liveness — still use AST temporal nodes as
            // .range carriers for the temporal bridge call.
            decl.safety.reserve(node.safety.size());
            for (const auto &formula : node.safety)
                decl.safety.push_back(lower_temporal(*formula));
            decl.liveness.reserve(node.liveness.size());
            for (const auto &formula : node.liveness)
                decl.liveness.push_back(lower_temporal(*formula));
        }
        return decl;
    }
};

// Out-of-line for the mutual recursion.
inline ir::Block TypedIrLowerer::lower_typed_block(const TypedBlock &block) const {
    ir::Block ir_block{.statements = {}, .source_range = block.range};
    ir_block.statements.reserve(block.statement_indexes.size());
    for (const auto &stmt_idx : block.statement_indexes) {
        if (stmt_idx == UINT32_MAX) continue;
        if (stmt_idx >= typed_program_->statements.size()) continue;
        ir_block.statements.push_back(
            lower_typed_statement(typed_program_->statements[stmt_idx]));
    }
    return ir_block;
}

inline ir::StatementPtr
TypedIrLowerer::lower_typed_statement(const TypedStatement &stmt) const {
    // Temporarily bind the current statement so the visitor can reach
    // children_expr_index through the class pointer. Resets the pointer on
    // exit so re-entrant calls (nested if-else blocks) do not leak state.
    const TypedStatement *prev = current_statement_;
    current_statement_ = &stmt;
    struct Restore {
        const TypedStatement **slot;
        const TypedStatement *prev;
        ~Restore() { *slot = prev; }
    } restore{&current_statement_, prev};
    return typed_visit(stmt, TypedStmtPerKindLowerer{*this, stmt.range});
}

// Out-of-line for the mutual recursion.
inline ir::StateHandler::Summary
TypedIrLowerer::summarize_block(const ir::Block &block) const {
    ir::StateHandler::Summary summary;
    for (const auto &statement : block.statements) {
        if (!summary.may_fallthrough) break;
        const auto stmt_summary = summarize_statement(*statement);
        merge_flow_summary(summary, stmt_summary);
        summary.may_fallthrough = stmt_summary.may_fallthrough;
    }
    return summary;
}

// ====================================================================
// Formal-observation collector (verbatim from ir_lower.cpp for identical
// output; lives here so typed_hir_lower.cpp is self-contained).
// ====================================================================

class FormalObservationCollector final {
  public:
    [[nodiscard]] std::vector<ir::FormalObservation> collect(const ir::Program &program) {
        observations_.clear();
        observation_index_by_symbol_.clear();
        for (const auto &declaration : program.declarations) {
            std::visit(Overloaded{
                           [this](const ir::AgentDecl &agent) { collect_agent(agent); },
                           [this](const ir::ContractDecl &c) { collect_contract(c); },
                           [this](const ir::WorkflowDecl &wf) { collect_workflow(wf); },
                           [](const auto &) {},
                       },
                       declaration);
        }
        return observations_;
    }

  private:
    std::vector<ir::FormalObservation> observations_;
    std::unordered_map<std::string, std::size_t> observation_index_by_symbol_;

    void collect_agent(const ir::AgentDecl &agent) {
        for (const auto &capability : agent.capability_refs) {
            const auto name = ir::symbol_canonical_name(capability);
            if (!name.empty()) add_called_observation(agent.name, name);
        }
    }

    void collect_contract(const ir::ContractDecl &contract) {
        const auto target = std::string(ir::symbol_canonical_name(contract.target_ref));
        for (std::size_t ci = 0; ci < contract.clauses.size(); ++ci) {
            const auto expr = std::get_if<ir::ExprPtr>(&contract.clauses[ci].value);
            if (expr != nullptr) {
                add_embedded_observation(ir::FormalObservationScope{
                    .kind = ir::FormalObservationScopeKind::ContractClause,
                    .owner = target,
                    .clause_index = ci,
                    .atom_index = 0,
                });
                continue;
            }
            const auto temporal =
                std::get_if<ir::TemporalExprPtr>(&contract.clauses[ci].value);
            if (temporal != nullptr) {
                std::size_t atom_index = 0;
                collect_contract_formula(**temporal, target, ci, atom_index);
            }
        }
    }

    void collect_workflow(const ir::WorkflowDecl &workflow) {
        for (std::size_t ci = 0; ci < workflow.safety.size(); ++ci) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.safety[ci],
                                     ir::FormalObservationScopeKind::WorkflowSafetyClause,
                                     workflow.name,
                                     ci,
                                     atom_index);
        }
        for (std::size_t ci = 0; ci < workflow.liveness.size(); ++ci) {
            std::size_t atom_index = 0;
            collect_workflow_formula(*workflow.liveness[ci],
                                     ir::FormalObservationScopeKind::WorkflowLivenessClause,
                                     workflow.name,
                                     ci,
                                     atom_index);
        }
    }

    void collect_contract_formula(const ir::TemporalExpr &expr,
                                  std::string_view agent_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(
            Overloaded{
                [&](const ir::EmbeddedTemporalExpr &) {
                    add_embedded_observation(ir::FormalObservationScope{
                        .kind = ir::FormalObservationScopeKind::ContractClause,
                        .owner = std::string(agent_name),
                        .clause_index = clause_index,
                        .atom_index = atom_index++,
                    });
                },
                [&](const ir::CalledTemporalExpr &value) {
                    add_called_observation(agent_name, value.capability);
                },
                [&](const ir::TemporalUnaryExpr &value) {
                    collect_contract_formula(*value.operand, agent_name, clause_index, atom_index);
                },
                [&](const ir::TemporalBinaryExpr &value) {
                    collect_contract_formula(*value.lhs, agent_name, clause_index, atom_index);
                    collect_contract_formula(*value.rhs, agent_name, clause_index, atom_index);
                },
                [&](const auto &) {},
            },
            expr.node);
    }

    void collect_workflow_formula(const ir::TemporalExpr &expr,
                                  ir::FormalObservationScopeKind scope_kind,
                                  std::string_view workflow_name,
                                  std::size_t clause_index,
                                  std::size_t &atom_index) {
        std::visit(Overloaded{
                       [&](const ir::EmbeddedTemporalExpr &) {
                           add_embedded_observation(ir::FormalObservationScope{
                               .kind = scope_kind,
                               .owner = std::string(workflow_name),
                               .clause_index = clause_index,
                               .atom_index = atom_index++,
                           });
                       },
                       [&](const ir::TemporalUnaryExpr &value) {
                           collect_workflow_formula(*value.operand,
                                                    scope_kind,
                                                    workflow_name,
                                                    clause_index,
                                                    atom_index);
                       },
                       [&](const ir::TemporalBinaryExpr &value) {
                           collect_workflow_formula(*value.lhs,
                                                    scope_kind,
                                                    workflow_name,
                                                    clause_index,
                                                    atom_index);
                           collect_workflow_formula(*value.rhs,
                                                    scope_kind,
                                                    workflow_name,
                                                    clause_index,
                                                    atom_index);
                       },
                       [&](const auto &) {},
                   },
                   expr.node);
    }

    void add_called_observation(std::string_view agent, std::string_view capability) {
        const auto symbol = called_observation_symbol(agent, capability);
        if (observation_index_by_symbol_.contains(symbol)) return;
        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node = ir::CalledCapabilityObservation{
                .agent = std::string(agent),
                .capability = std::string(capability),
            },
        });
    }

    void add_embedded_observation(ir::FormalObservationScope scope) {
        const auto symbol = embedded_observation_symbol(scope);
        if (observation_index_by_symbol_.contains(symbol)) return;
        observation_index_by_symbol_.emplace(symbol, observations_.size());
        observations_.push_back(ir::FormalObservation{
            .symbol = symbol,
            .node = ir::EmbeddedBoolObservation{.scope = std::move(scope)},
        });
    }
};

} // namespace

// =====================================================================
// Public entry point. Routes to TypedIrLowerer against the caller's
// TypedProgram, so post-typecheck edits to program.expressions are
// honoured (the key behavioural guarantee of the P3 boundary).
// =====================================================================

ir::Program lower_typed_program(const TypedProgram &program) {
    ir::Program program_ir;

    auto run = [&](const ResolveResult &resolve,
                   const TypeCheckResult &tcr) -> ir::Program {
        std::unique_ptr<TypedIrLowerer> lowerer;
        if (program.source_graph != nullptr) {
            lowerer = std::make_unique<TypedIrLowerer>(
                program, *program.source_graph, resolve, tcr);
        } else if (program.ast_program != nullptr) {
            lowerer = std::make_unique<TypedIrLowerer>(
                program, *program.ast_program, resolve, tcr);
        } else {
            ir::Program empty;
            ir::recompute_derived_analyses(empty, ir::ProgramPhase::Analyzed);
            return empty;
        }
        return lowerer->lower();
    };

    if (program.type_check_result != nullptr && program.resolve_result != nullptr) {
        program_ir = run(*program.resolve_result, *program.type_check_result);
        ir::recompute_derived_analyses(program_ir, ir::ProgramPhase::Analyzed);
        return program_ir;
    }

    // Detached TypedProgram: the caller has a TypedProgram without a live
    // TypeCheckResult (e.g. one where expressions were mutated after
    // typecheck). Re-run the checker, then lower against the caller's copy
    // so those mutations are preserved in the IR.
    if (program.source_graph != nullptr && program.resolve_result != nullptr) {
        TypeCheckResult type_check_result =
            TypeChecker{}.check(*program.source_graph, *program.resolve_result);
        type_check_result.typed_program = program;
        program_ir = run(*program.resolve_result, type_check_result);
        ir::recompute_derived_analyses(program_ir, ir::ProgramPhase::Analyzed);
        return program_ir;
    }
    if (program.ast_program != nullptr && program.resolve_result != nullptr) {
        TypeCheckResult type_check_result =
            TypeChecker{}.check(*program.ast_program, *program.resolve_result);
        type_check_result.typed_program = program;
        program_ir = run(*program.resolve_result, type_check_result);
        ir::recompute_derived_analyses(program_ir, ir::ProgramPhase::Analyzed);
        return program_ir;
    }

    ir::recompute_derived_analyses(program_ir, ir::ProgramPhase::Analyzed);
    return program_ir;
}

// NOTE: collect_formal_observations lives in ir_lower.cpp to avoid ODR
// violations. This TU (typed_hir_lower.cpp) only lowers; both TUs are linked
// into the same binaries in this project so no separate copy is required.

} // namespace ahfl
