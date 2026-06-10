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
// AST-dependency boundary (P3 / T1.4 / T1.6)
//
// Expression 100% typed (T1.3); Decl iteration 100% typed (T1.4), structural
// fields still read AST (intentional); Statement dispatch + block iteration
// 100% typed (T1.6 P4), let type_ref / assign target path / assert message /
// goto AST fields still consult AST via typed->AST bridge; Temporal dispatch
// 100% typed (T1.6 P5), fallback retained; T1.7 follow-up: remove fallbacks
// after typed payloads are complete.
//
// Inside the expression-lowering boundary (100% AST-free):
//   * Dispatch is `typed_visit` on TypedExpr::kind.
//   * Sub-tree relationships come from TypedExpr::children.
//   * Operators, literal spellings, member names come from the new primitive
//     payload mirrors on TypedExpr.
//   * Call / struct / qualified-value canonical names come from the typed
//     node's `resolved_symbol` + SymbolTable.
//   * The only direction of AST contact is a one-way bridge:
//     `ast_expr` -> `typed_expr_for(node_id)` used to re-root the walk into
//     the typed tree when the caller hands us an ast syntactic node (e.g. a
//     statement's payload expression).
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

[[nodiscard]] ir::PathRootKind lower_path_root_kind(ast::PathRootKind kind) {
    switch (kind) {
    case ast::PathRootKind::Identifier: return ir::PathRootKind::Identifier;
    case ast::PathRootKind::Input:      return ir::PathRootKind::Input;
    case ast::PathRootKind::Output:     return ir::PathRootKind::Output;
    }
    return ir::PathRootKind::Identifier;
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

[[nodiscard]] ir::TemporalUnaryOp lower_temporal_unary_op(ast::TemporalUnaryOp op) {
    switch (op) {
    case ast::TemporalUnaryOp::Always:    return ir::TemporalUnaryOp::Always;
    case ast::TemporalUnaryOp::Eventually:return ir::TemporalUnaryOp::Eventually;
    case ast::TemporalUnaryOp::Next:      return ir::TemporalUnaryOp::Next;
    case ast::TemporalUnaryOp::Not:       return ir::TemporalUnaryOp::Not;
    }
    return ir::TemporalUnaryOp::Always;
}

[[nodiscard]] ir::TemporalBinaryOp lower_temporal_binary_op(ast::TemporalBinaryOp op) {
    switch (op) {
    case ast::TemporalBinaryOp::Implies: return ir::TemporalBinaryOp::Implies;
    case ast::TemporalBinaryOp::Or:      return ir::TemporalBinaryOp::Or;
    case ast::TemporalBinaryOp::And:     return ir::TemporalBinaryOp::And;
    case ast::TemporalBinaryOp::Until:   return ir::TemporalBinaryOp::Until;
    }
    return ir::TemporalBinaryOp::Implies;
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

[[nodiscard]] std::string quota_item_name(ast::AgentQuotaItemKind kind) {
    switch (kind) {
    case ast::AgentQuotaItemKind::MaxToolCalls:      return "max_tool_calls";
    case ast::AgentQuotaItemKind::MaxExecutionTime:  return "max_execution_time";
    }
    return "<invalid-quota-item>";
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

    [[nodiscard]] std::vector<std::string>
    lower_retry_targets(const std::vector<Owned<ast::QualifiedName>> &targets) const {
        std::vector<std::string> names;
        names.reserve(targets.size());
        for (const auto &target : targets) names.push_back(target->spelling());
        return names;
    }

    [[nodiscard]] ir::Path lower_path_syntax(const ast::PathSyntax &path) const {
        return ir::Path{
            .root_kind = lower_path_root_kind(path.root_kind),
            .root_name = path.root_name,
            .members = path.members,
        };
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
    // Temporal lowering (AST -> typed bridge first, AST walk fallback)
    // =====================================================================

    // AST->typed bridge: consult the typed temporal store by exact range
    // match first. When present, dispatch through lower_typed_temporal so
    // any post-typecheck edits (rewrites, de-Morgan, synthetic-atom
    // injection) made to TypedProgram::temporal_exprs are honoured. Falls
    // back to the AST-only lower_temporal_ast_fallback when the temporal
    // typed store is empty (legacy / detached test cases).
    [[nodiscard]] ir::TemporalExprPtr
    lower_temporal(const ast::TemporalExprSyntax &expr) const {
        const TypedTemporalExpr *tte = find_typed_temporal_by_range(expr.range, current_source_id_);
        if (tte != nullptr) return lower_typed_temporal(*tte);
        return lower_temporal_ast_fallback(expr);
    }

    [[nodiscard]] ir::TemporalExprPtr
    lower_temporal_ast_fallback(const ast::TemporalExprSyntax &expr) const {
        const auto make = [this, &expr](auto node) {
            return make_temporal(std::move(node), expr.range);
        };
        switch (expr.kind) {
        case ast::TemporalExprSyntaxKind::EmbeddedExpr:
            return make(ir::EmbeddedTemporalExpr{.expr = lower_expr(*expr.expr)});
        case ast::TemporalExprSyntaxKind::Called:
            return make(ir::CalledTemporalExpr{
                .capability = canonical_name_from_reference_here(
                    ReferenceKind::TemporalCapability, expr.range, expr.name),
            });
        case ast::TemporalExprSyntaxKind::InState:
            return make(ir::InStateTemporalExpr{.state = expr.name});
        case ast::TemporalExprSyntaxKind::Running:
            return make(ir::RunningTemporalExpr{.node = expr.name});
        case ast::TemporalExprSyntaxKind::Completed:
            return make(ir::CompletedTemporalExpr{
                .node = expr.name, .state_name = expr.state_name,
            });
        case ast::TemporalExprSyntaxKind::Unary:
            return make(ir::TemporalUnaryExpr{
                .op = lower_temporal_unary_op(expr.unary_op),
                .operand = lower_temporal_ast_fallback(*expr.first),
            });
        case ast::TemporalExprSyntaxKind::Binary:
            return make(ir::TemporalBinaryExpr{
                .op = lower_temporal_binary_op(expr.binary_op),
                .lhs = lower_temporal_ast_fallback(*expr.first),
                .rhs = lower_temporal_ast_fallback(*expr.second),
            });
        }
        return make(ir::CalledTemporalExpr{.capability = "<invalid-temporal-expr>"});
    }

    // =====================================================================
    // Statement / block lowering (AST for structure per boundary)
    // =====================================================================

    [[nodiscard]] ir::TypeRef
    inferred_statement_type_ref(const ast::LetStmtSyntax &statement) const {
        if (statement.type) return type_ref_from_syntax(*statement.type);
        if (statement.initializer) {
            if (const TypedExpr *typed = typed_expr_for(*statement.initializer);
                typed != nullptr && typed->type != nullptr) {
                return type_ref_from_type(*typed->type);
            }
        }
        return type_ref_from_maybe(std::nullopt, "Any");
    }

    [[nodiscard]] ir::Block lower_block(const ast::BlockSyntax &block) const {
        // AST->typed bridge: if a matching TypedBlock is present, dispatch
        // through the typed-store path so any post-typecheck statement
        // mutations are honoured. Falls back to the AST-only path when the
        // typed block store is empty (legacy / detached test cases).
        const TypedBlock *tb = find_typed_block_by_range(block.range, current_source_id_);
        if (tb != nullptr) return lower_typed_block(*tb);
        return lower_block_ast_fallback(block);
    }

    [[nodiscard]] ir::Block
    lower_block_ast_fallback(const ast::BlockSyntax &block) const {
        ir::Block ir_block{.statements = {}, .source_range = block.range};
        ir_block.statements.reserve(block.statements.size());
        for (const auto &statement : block.statements)
            ir_block.statements.push_back(lower_statement(*statement));
        return ir_block;
    }

    [[nodiscard]] ir::StatementPtr
    lower_statement(const ast::StatementSyntax &statement) const {
        // AST->typed bridge: identical shape to the block bridge above.
        const TypedStatement *ts = typed_stmt_for(statement);
        if (ts != nullptr) return lower_typed_statement(*ts);
        return lower_statement_ast_fallback(statement);
    }

    [[nodiscard]] ir::StatementPtr
    lower_statement_ast_fallback(const ast::StatementSyntax &statement) const {
        const auto make = [this, &statement](auto node) {
            return make_statement(std::move(node), statement.range);
        };
        switch (statement.kind) {
        case ast::StatementSyntaxKind::Let:
            return make(ir::LetStatement{
                .name = statement.let_stmt->name,
                .type_ref = inferred_statement_type_ref(*statement.let_stmt),
                .initializer = lower_expr(*statement.let_stmt->initializer),
            });
        case ast::StatementSyntaxKind::Assign:
            return make(ir::AssignStatement{
                .target = lower_path_syntax(*statement.assign_stmt->target),
                .value = lower_expr(*statement.assign_stmt->value),
            });
        case ast::StatementSyntaxKind::If: {
            auto else_block = statement.if_stmt->else_block
                                  ? make_owned<ir::Block>(lower_block(*statement.if_stmt->else_block))
                                  : nullptr;
            return make(ir::IfStatement{
                .condition = lower_expr(*statement.if_stmt->condition),
                .then_block = make_owned<ir::Block>(lower_block(*statement.if_stmt->then_block)),
                .else_block = std::move(else_block),
            });
        }
        case ast::StatementSyntaxKind::Goto:
            return make(ir::GotoStatement{.target_state = statement.goto_stmt->target_state});
        case ast::StatementSyntaxKind::Return:
            return make(ir::ReturnStatement{
                .value = statement.return_stmt->value
                             ? lower_expr(*statement.return_stmt->value)
                             : nullptr,
            });
        case ast::StatementSyntaxKind::Assert:
            return make(ir::AssertStatement{
                .condition = lower_expr(*statement.assert_stmt->condition)});
        case ast::StatementSyntaxKind::Expr:
            return make(ir::ExprStatement{
                .expr = lower_expr(*statement.expr_stmt->expr),
            });
        }
        return make(ir::ExprStatement{
            .expr = make_expr(ir::QualifiedValueExpr{.value = "<invalid-statement>"},
                              statement.range),
        });
    }

    // =====================================================================
    // Typed-store statement / block lowering (T1.6)
    //
    // The dispatch chain below walks TypedProgram::statements /
    // TypedProgram::blocks (the flat typed-store) instead of the AST.
    // Expression payloads are pulled from TypedStatement::children_expr_index
    // via lower_typed_expr; then/else blocks come from TypedStatement block
    // indexes. For typed records that do not carry enough primitive detail to
    // construct the full IR (let type annotations, assign target path root
    // kind, assert messages, goto provenance), we consult the originating
    // AST via a (range, kind) range lookup – see find_orig_ast_stmt below.
    // =====================================================================

    [[nodiscard]] static TypedStmtKind
    typed_stmt_kind_from_ast(ast::StatementSyntaxKind kind) noexcept {
        switch (kind) {
        case ast::StatementSyntaxKind::Let:    return TypedStmtKind::Let;
        case ast::StatementSyntaxKind::Assign: return TypedStmtKind::Assign;
        case ast::StatementSyntaxKind::If:     return TypedStmtKind::If;
        case ast::StatementSyntaxKind::Goto:   return TypedStmtKind::Goto;
        case ast::StatementSyntaxKind::Return: return TypedStmtKind::Return;
        case ast::StatementSyntaxKind::Assert: return TypedStmtKind::Assert;
        case ast::StatementSyntaxKind::Expr:   return TypedStmtKind::ExprStatement;
        }
        return TypedStmtKind::None;
    }

    // One-way bridge: AST StatementSyntax -> TypedStatement.
    // Uses range+kind lookup directly. Unlike TypedExpr, StatementSyntax does
    // not carry a node_id field, so the fast path is skipped and we always
    // consult the exact range+kind match. Falls back to nullptr when the
    // typed statement store is empty (legacy / detached test cases) so the
    // caller can route through lower_statement_ast_fallback.
    [[nodiscard]] const TypedStatement *
    typed_stmt_for(const ast::StatementSyntax &stmt) const {
        const auto kind = typed_stmt_kind_from_ast(stmt.kind);
        return typed_program_->find_statement_by_range(stmt.range, kind, current_source_id_);
    }

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
    // (legacy / detached test cases) so the caller can transparently fall
    // back to the AST-based lower_temporal path.
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
                // remainder == "node_name|optional_state_name"
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
            // Unknown prefix: fall through to safe default.
            return make(ir::CalledTemporalExpr{.capability = std::string(remainder)});
        }
        case TypedTemporalKind::StateLiteral: {
            const auto [prefix, remainder] = split_payload(te.payload_spelling);
            std::string state = (prefix == "state") ? std::string(remainder)
                                                    : std::string(te.payload_spelling);
            return make(ir::InStateTemporalExpr{.state = std::move(state)});
        }
        case TypedTemporalKind::Unary: {
            ir::TemporalExprPtr operand = nullptr;
            if (!te.children_index.empty() && te.children_index[0] != UINT32_MAX &&
                te.children_index[0] < typed_program_->temporal_exprs.size()) {
                operand = lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[0]]);
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
                    lhs = lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[0]]);
                }
                if (te.children_index[1] != UINT32_MAX &&
                    te.children_index[1] < typed_program_->temporal_exprs.size()) {
                    rhs = lower_typed_temporal(typed_program_->temporal_exprs[te.children_index[1]]);
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

    // Recursive helper: walk the owning AST source's block/statement tree to
    // locate a StatementSyntax with matching (range, kind). Used from the
    // typed-store lowering path when a typed record lacks the primitive
    // payload (let type syntax, assign target path shape, assert message)
    // needed to build the full IR. Returns nullptr when no match is found –
    // the caller degrades gracefully (Any type_ref, empty path, etc.).
    [[nodiscard]] const ast::StatementSyntax *
    find_orig_ast_stmt(SourceRange range, ast::StatementSyntaxKind kind) const {
        // Prefer the active source (SourceGraph path); fall back to the raw
        // program_ pointer when we were constructed from a single Program.
        const ast::Program *ast_program =
            current_source_ ? current_source_->program.get() : program_;
        if (ast_program == nullptr) return nullptr;

        struct Walker {
            const ast::Program &program;
            SourceRange range;
            ast::StatementSyntaxKind kind;
            const ast::StatementSyntax *found{nullptr};

            void walk_block(const ast::BlockSyntax &block) {
                for (const auto &s : block.statements) walk_stmt(*s);
            }
            void walk_stmt(const ast::StatementSyntax &s) {
                if (found) return;
                if (s.range.begin_offset == range.begin_offset &&
                    s.range.end_offset == range.end_offset && s.kind == kind) {
                    found = &s;
                    return;
                }
                if (s.kind == ast::StatementSyntaxKind::If) {
                    walk_block(*s.if_stmt->then_block);
                    if (s.if_stmt->else_block) walk_block(*s.if_stmt->else_block);
                }
            }
            void walk_decl(const ast::Decl &d) {
                if (found) return;
                if (d.kind == ast::NodeKind::FlowDecl) {
                    const auto &flow = static_cast<const ast::FlowDecl &>(d);
                    for (const auto &h : flow.state_handlers)
                        walk_block(*h->body);
                }
            }
        };
        Walker w{*ast_program, range, kind, nullptr};
        for (const auto &d : ast_program->declarations) w.walk_decl(*d);
        return w.found;
    }

    // Let type_ref construction for the typed-store path. Strategy mirrors
    // inferred_statement_type_ref exactly so the two paths produce identical
    // output for the T1.2 equivalence test:
    //   1. AST syntax type annotation (requires find_orig_ast_stmt fallback)
    //   2. Initializer TypedExpr's resolved type
    //   3. Any / unresolved sentinel
    [[nodiscard]] ir::TypeRef
    inferred_typed_let_type_ref(const TypedStatement &stmt,
                                const TypedExpr *initializer) const {
        if (!stmt.children_expr_index.empty()) {
            const ast::StatementSyntax *orig =
                find_orig_ast_stmt(stmt.range, ast::StatementSyntaxKind::Let);
            if (orig != nullptr && orig->let_stmt && orig->let_stmt->type) {
                return type_ref_from_syntax(*orig->let_stmt->type);
            }
        }
        if (initializer != nullptr && initializer->type != nullptr) {
            return type_ref_from_type(*initializer->type);
        }
        return type_ref_from_maybe(std::nullopt, "Any");
    }

    // Assign target path construction for the typed-store path. The typed
    // record stores the full path string in target_name but does not mirror
    // PathSyntax::root_kind; when the origin AST is available we pull the
    // structured path directly. As a backstop we parse the spelling into a
    // single-identifier root path.
    [[nodiscard]] ir::Path
    typed_assign_target_path(const TypedStatement &stmt) const {
        const ast::StatementSyntax *orig =
            find_orig_ast_stmt(stmt.range, ast::StatementSyntaxKind::Assign);
        if (orig != nullptr && orig->assign_stmt && orig->assign_stmt->target) {
            return lower_path_syntax(*orig->assign_stmt->target);
        }
        // Backstop: target_name is the source-level spelling. We don't know
        // root_kind, but identifier paths are the dominant case and the
        // fallback preserves byte-identical output for the common case.
        return ir::Path{
            .root_kind = ir::PathRootKind::Identifier,
            .root_name = stmt.target_name,
            .members = {},
        };
    }

    // Typed-store statement lowering visitor. Each method builds the same
    // ir::Statement variant as the AST fallback path (see
    // lower_statement_ast_fallback) – the two paths are kept byte-identical
    // by the T1.2 test. The unknown_stmt fallback gracefully degrades when a
    // new TypedStmtKind is introduced but the visitor hasn't been updated.
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
            // goto_target_state is the primary source; the AST fallback only
            // runs when the typed record carries an empty string (legacy
            // store). Both variants produce identical IR.
            std::string target = stmt.goto_target_state;
            if (target.empty()) {
                const ast::StatementSyntax *orig = self.find_orig_ast_stmt(
                    stmt.range, ast::StatementSyntaxKind::Goto);
                if (orig != nullptr && orig->goto_stmt) {
                    target = orig->goto_stmt->target_state;
                }
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
            auto field_type_ref = type_ref_from_syntax(*field->type);
            if (info.has_value() && i < info->get().fields.size() &&
                info->get().fields[i].type) {
                field_type_ref = type_ref_from_type(*info->get().fields[i].type);
            }
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
        decl.variants.reserve(node.variants.size());
        for (const auto &variant : node.variants)
            decl.variants.push_back(variant->name);
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

    [[nodiscard]] ir::CapabilityEffectSpec
    lower_capability_effect(const ast::CapabilityEffectSyntax *syntax) const {
        ir::CapabilityEffectSpec effect;
        if (syntax == nullptr) return effect;
        effect.declared = true;
        effect.kind = lower_capability_effect_kind(syntax->effect_kind);
        effect.receipt_mode = lower_capability_receipt_mode(syntax->receipt_mode);
        effect.retry_mode = lower_capability_retry_mode(syntax->retry_mode);
        effect.source_range = syntax->range;
        if (syntax->domain) effect.domain = syntax->domain->spelling();
        if (syntax->idempotency_key) effect.idempotency_key = syntax->idempotency_key->spelling();
        if (syntax->timeout) effect.timeout = syntax->timeout->spelling;
        if (syntax->compensation) effect.compensation = syntax->compensation->spelling();
        effect.policies.reserve(syntax->policies.size());
        for (const auto &policy : syntax->policies)
            effect.policies.push_back(policy->spelling());
        return effect;
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
                                     : type_ref_from_syntax(*node.return_type),
                .effect = lower_capability_effect(node.effect.get()),
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
                .states = node.states,
                .initial_state = node.initial_state,
                .final_states = node.final_states,
                .quota = {},
                .transitions = {},
                .input_type_ref = info.has_value()
                                       ? type_ref_from_maybe(borrow(info->get().input_type))
                                       : type_ref_from_syntax(*node.input_type),
                .context_type_ref =
                    info.has_value() ? type_ref_from_maybe(borrow(info->get().context_type))
                                     : type_ref_from_syntax(*node.context_type),
                .output_type_ref = info.has_value()
                                        ? type_ref_from_maybe(borrow(info->get().output_type))
                                        : type_ref_from_syntax(*node.output_type),
                .capability_refs = {},
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Agent, std::string(node.name)),
            },
            node.range);
        if (info.has_value()) {
            decl.capability_refs.reserve(info->get().capability_symbols.size());
            for (const auto cs : info->get().capability_symbols) {
                const auto cap = symbols().get(cs);
                decl.capability_refs.push_back(symbol_ref_from_symbol(
                    cap,
                    ir::SymbolRefKind::Capability,
                    cap.has_value() ? cap->get().canonical_name : "<missing-capability>"));
            }
        } else {
            decl.capability_refs.reserve(node.capabilities.size());
            for (const auto &cap_name : node.capabilities) {
                const auto cap =
                    find_local_symbol_here(SymbolNamespace::Capabilities, cap_name);
                const auto name =
                    cap.has_value() ? cap->get().canonical_name : std::string(cap_name);
                decl.capability_refs.push_back(
                    symbol_ref_from_symbol(cap, ir::SymbolRefKind::Capability, name));
            }
        }
        if (node.quota) {
            decl.quota.reserve(node.quota->items.size());
            for (const auto &item : node.quota->items) {
                std::string value;
                if (item->integer_value) value = item->integer_value->spelling;
                else if (item->duration_value) value = item->duration_value->spelling;
                else value = "<missing-quota-value>";
                decl.quota.push_back(ir::QuotaItem{
                    .name = quota_item_name(item->kind),
                    .value = std::move(value),
                });
            }
        }
        decl.transitions.reserve(node.transitions.size());
        for (const auto &t : node.transitions) {
            decl.transitions.push_back(ir::TransitionDecl{
                .from_state = t->from_state,
                .to_state = t->to_state,
            });
        }
        return decl;
    }

    [[nodiscard]] ir::ContractDecl lower_contract(const ast::ContractDecl &node) const {
        const auto target = canonical_name_from_reference_here(
            ReferenceKind::ContractTarget, node.target->range, node.target->spelling());
        ir::ContractDecl decl = with_provenance(
            ir::ContractDecl{
                .provenance = {},
                .clauses = {},
                .target_ref = symbol_ref_from_reference_here(ReferenceKind::ContractTarget,
                                                              node.target->range,
                                                              ir::SymbolRefKind::Agent,
                                                              target),
            },
            node.range);
        decl.clauses.reserve(node.clauses.size());
        for (const auto &clause : node.clauses) {
            ir::ContractClause lowered{
                .kind = lower_contract_clause_kind(clause->kind),
                .value = ir::ExprPtr{},
                .source_range = clause->range,
            };
            if (clause->expr) lowered.value = lower_expr(*clause->expr);
            else lowered.value = lower_temporal(*clause->temporal_expr);
            decl.clauses.push_back(std::move(lowered));
        }
        return decl;
    }

    [[nodiscard]] ir::FlowDecl lower_flow(const ast::FlowDecl &node) const {
        const auto target = canonical_name_from_reference_here(
            ReferenceKind::FlowTarget, node.target->range, node.target->spelling());
        ir::FlowDecl decl = with_provenance(
            ir::FlowDecl{
                .provenance = {},
                .state_handlers = {},
                .target_ref = symbol_ref_from_reference_here(ReferenceKind::FlowTarget,
                                                              node.target->range,
                                                              ir::SymbolRefKind::Agent,
                                                              target),
            },
            node.range);
        decl.state_handlers.reserve(node.state_handlers.size());
        for (const auto &handler : node.state_handlers) {
            ir::StateHandler state_handler{
                .state_name = handler->state_name,
                .policy = {},
                .body = lower_block(*handler->body),
                .source_range = handler->range,
            };
            if (handler->policy) {
                state_handler.policy.reserve(handler->policy->items.size());
                for (const auto &item : handler->policy->items) {
                    switch (item->kind) {
                    case ast::StatePolicyItemKind::Retry:
                        state_handler.policy.push_back(ir::RetryPolicy{
                            .limit = item->retry_limit ? item->retry_limit->spelling
                                                       : std::string{"<missing-retry>"},
                        });
                        break;
                    case ast::StatePolicyItemKind::RetryOn:
                        state_handler.policy.push_back(ir::RetryOnPolicy{
                            .targets = lower_retry_targets(item->retry_on),
                        });
                        break;
                    case ast::StatePolicyItemKind::Timeout:
                        state_handler.policy.push_back(ir::TimeoutPolicy{
                            .duration = item->timeout ? item->timeout->spelling
                                                      : std::string{"<missing-timeout>"},
                        });
                        break;
                    }
                }
            }
            decl.state_handlers.push_back(std::move(state_handler));
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
                                       : type_ref_from_syntax(*node.input_type),
                .output_type_ref = info.has_value()
                                        ? type_ref_from_maybe(borrow(info->get().output_type))
                                        : type_ref_from_syntax(*node.output_type),
                .symbol_ref = symbol_ref_from_symbol(
                    symbol, ir::SymbolRefKind::Workflow, std::string(node.name)),
            },
            node.range);
        decl.nodes.reserve(node.nodes.size());
        for (const auto &wfn : node.nodes) {
            auto input = lower_expr(*wfn->input);
            const auto target = canonical_name_from_reference_here(
                ReferenceKind::WorkflowNodeTarget, wfn->target->range, wfn->target->spelling());
            decl.nodes.push_back(ir::WorkflowNode{
                .name = wfn->name,
                .input = std::move(input),
                .after = wfn->after,
                .target_ref = symbol_ref_from_reference_here(
                    ReferenceKind::WorkflowNodeTarget,
                    wfn->target->range,
                    ir::SymbolRefKind::Agent,
                    target),
                .source_range = wfn->range,
            });
        }
        decl.safety.reserve(node.safety.size());
        for (const auto &formula : node.safety)
            decl.safety.push_back(lower_temporal(*formula));
        decl.liveness.reserve(node.liveness.size());
        for (const auto &formula : node.liveness)
            decl.liveness.push_back(lower_temporal(*formula));
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
