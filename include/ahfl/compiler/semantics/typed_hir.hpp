#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/semantics/declaration_info.hpp"
#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ahfl {

// ----------------------------------------------------------------------------
// TypedDeclPayload — self-contained structural data for a declaration.
// Populated from TypeEnvironment after declaration analysis so downstream
// consumers can read TypedProgram without borrowing TypeEnvironment storage.
// ----------------------------------------------------------------------------
using TypedDeclPayload = std::variant<std::monostate,
                                      ModuleDeclInfo,
                                      ImportDeclInfo,
                                      ConstDeclInfo,
                                      TypeAliasDeclInfo,
                                      StructTypeInfo,
                                      EnumTypeInfo,
                                      CapabilityTypeInfo,
                                      PredicateTypeInfo,
                                      AgentTypeInfo,
                                      WorkflowTypeInfo,
                                      FlowTypeInfo,
                                      ContractTypeInfo>;

enum class TypedExprChildRole {
    Operand,
    LeftOperand,
    RightOperand,
    Argument,
    StructFieldValue,
    CollectionElement,
    MapKey,
    MapValue,
    Base,
    Index,
    Grouped,
};

enum class TypedCallTargetKind {
    None,
    Capability,
    Predicate,
};

struct TypedExpr;

// ----------------------------------------------------------------------------
// TypedStatement kind enum (decoupled from ast::StatementSyntaxKind)
// ----------------------------------------------------------------------------
enum class TypedStmtKind : std::uint8_t {
    None = 0, // sentinel
    Let,
    Assign,
    If,
    Goto,
    Return,
    Assert,
    ExprStatement,
};

enum class TypedTemporalKind : std::uint8_t {
    None = 0,
    Atom,
    NameLiteral,
    StateLiteral,
    Unary,
    Binary,
};

// ----------------------------------------------------------------------------
// Let type-ref strategy (how the let binding's type_ref was determined).
// ----------------------------------------------------------------------------
// The lowering pass reconstructs an ir::TypeRef from the typed record without
// re-entering the AST by combining this enum with let_type_ref_spelling:
//   * NoAnnotation       -> no type was written; fall back to initializer type.
//   * FromSyntax         -> spelling carries the canonical source text of the
//                           user's TypeSyntax (e.g. "Int", "Optional<Foo>",
//                           "Map<String,BoundedString<5,100>>"); lowering can
//                           re-parse or (preferred) look it up in the type
//                           environment via canonical-name resolution.
//   * FromInitializerType-> no annotation; the resolved initializer type was
//                           captured (spelling unused, kept empty).
//   * AnySentinel        -> error path; type-ref should be Any sentinel.
enum class LetTypeRefStrategy : std::uint32_t {
    NoAnnotation = 0,
    FromSyntax,
    FromInitializerType,
    AnySentinel,
};

// ----------------------------------------------------------------------------
// Assignment target root kind. Mirrors ir::PathRootKind one-to-one so typed-
// tree lowering can build an ir::Path without consulting the originating
// PathSyntax. Local / State / Context / Output are distinguished from plain
// Identifier to keep the mirror faithful to the IR's root_kind space.
// ----------------------------------------------------------------------------
enum class AssignTargetRootKind : std::uint8_t {
    Identifier = 0,
    Input,
    Context,
    Output,
    State,
    Local,
};

// ----------------------------------------------------------------------------
// TypedTemporalOp: a single enum covering every concrete temporal construct.
// ----------------------------------------------------------------------------
// Replaces the ad-hoc payload_spelling string concat for Unary/Binary (and
// removes string-prefix parsing from the NameLiteral/StateLiteral branches of
// lower_typed_temporal). payload_spelling is retained for NameLiteral/
// StateLiteral as a human-readable fallback, but the lowering pass should
// prefer this strongly-typed op enum for dispatch.
enum class TypedTemporalOp : std::uint8_t {
    Atom = 0,
    // NameLiteral variants
    NameLiteralCalled,
    NameLiteralRunning,
    NameLiteralCompleted,
    // StateLiteral
    StateLiteral,
    // Unary
    TemporalNot,
    TemporalNext,
    TemporalAlways,
    TemporalEventually,
    // Binary
    TemporalAnd,
    TemporalOr,
    TemporalImply,
    TemporalUntil,
};

struct TypedExprChild {
    TypedExprChildRole role{TypedExprChildRole::Operand};
    std::string name;
    // Index into TypedProgram::expressions (flat vector). UINT32_MAX means
    // "no child" (equivalent to a null pointer). Storing an index (instead
    // of a pointer / shared_ptr) keeps children valid even if the flat
    // vector reallocates, and guarantees that post-typecheck edits to the
    // expressions flat store are visible to child traversals (T1.3).
    std::uint32_t expr_index{UINT32_MAX};
};

struct TypedProgram;

// Helper: resolve a TypedExprChild to a pointer. Returns nullptr if the
// child's index is unset / out of range.
[[nodiscard]] const TypedExpr *resolve_child(const TypedProgram &program,
                                             const TypedExprChild &child) noexcept;
[[nodiscard]] TypedExpr *resolve_child_mut(TypedProgram &program,
                                           const TypedExprChild &child) noexcept;

struct TypedExpr {
    ast::ExprSyntaxKind kind{ast::ExprSyntaxKind::NoneLiteral};
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id{0};
    TypePtr type{nullptr};
    ExprEffect effect{ExprEffect::Pure};
    bool is_pure{true};
    std::optional<SymbolId> resolved_symbol;
    std::string semantic_name;
    TypedCallTargetKind call_target_kind{TypedCallTargetKind::None};
    std::string path_root;
    AssignTargetRootKind path_root_kind{AssignTargetRootKind::Identifier};
    std::vector<std::string> member_path;
    std::vector<TypedExprChild> children;

    // ------------------------------------------------------------------------
    // Primitive payload mirrors of the originating ast::ExprSyntax.
    // Stored here so typed-tree passes (lowering, rewrites, equivalence) do
    // not need to reach back into the AST. Each field is only meaningful for
    // the `kind`s listed after it.
    // ------------------------------------------------------------------------
    bool bool_value{false};                                  // BoolLiteral
    ast::ExprUnaryOp unary_op{ast::ExprUnaryOp::Not};        // Unary
    ast::ExprBinaryOp binary_op{ast::ExprBinaryOp::Implies}; // Binary
    std::string literal_spelling; // Integer/Float/Decimal/String/Duration literal
    std::string member_name;      // MemberAccess (right-hand field name)
};

struct TypedDecl {
    ast::NodeKind kind{ast::NodeKind::Program};
    SymbolId symbol;
    SourceRange range;
    // Source owning this declaration. Set for SourceGraph-based typechecks;
    // left empty for single-program typechecks. The lowering pass uses this
    // to partition the flat TypedProgram::declarations list back into
    // per-source iteration (T1.4).
    std::optional<SourceId> source_id;
    // For ContractDecl/FlowDecl: SymbolId of the owning agent. For all other
    // declaration kinds (Struct/Enum/Agent/...) left empty: the `symbol` field
    // already records the declaration's own id.
    std::optional<SymbolId> associated_agent_symbol;
    TypePtr type{nullptr};

    // Structural payload copied out of TypeEnvironment. std::monostate is used
    // for declaration kinds with no additional structural data.
    TypedDeclPayload payload;
};

// ----------------------------------------------------------------------------
// TypedBlock / TypedStatement / TypedTemporalExpr
// ----------------------------------------------------------------------------
//
// Flat-store representation of statements, blocks, and temporal expressions.
// P1 introduces the skeletal structure only; wiring from the typechecker and
// visitor coverage for TypedTemporalExpr are deferred to later phases.

struct TypedBlock {
    SourceRange range;
    std::optional<SourceId> source_id;
    // Indexes into TypedProgram::statements.
    std::vector<std::uint32_t> statement_indexes;
};

struct TypedStatement {
    TypedStmtKind kind{TypedStmtKind::None};
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id{0};
    // Children pointing into TypedProgram::expressions (expr_index);
    // UINT32_MAX = none.
    std::vector<std::uint32_t> children_expr_index;
    // Payload mirrors:
    //   Let: local name
    //   Assign: target path spelling
    std::string target_name;
    // Goto: target state spelling
    std::string goto_target_state;
    // If: indexes into TypedProgram::blocks (UINT32_MAX = absent)
    std::uint32_t then_block_index{UINT32_MAX};
    std::uint32_t else_block_index{UINT32_MAX};

    // Let type annotation payload (T1.7 P1).
    //   let_type_ref_strategy describes how the let binding's type_ref should
    //   be reconstructed; let_type_ref_spelling carries the canonical source
    //   text (spelling()) of the user's TypeSyntax when strategy == FromSyntax.
    LetTypeRefStrategy let_type_ref_strategy{LetTypeRefStrategy::NoAnnotation};
    std::string let_type_ref_spelling;

    // Assign target path root kind (T1.7 P1).
    // Mirrors ir::PathRootKind so typed-tree lowering builds an ir::Path
    // without reaching back into the originating PathSyntax.
    AssignTargetRootKind assign_target_root_kind{AssignTargetRootKind::Identifier};

    // Assert message (T1.7 P1).
    // Filled from AST assert_stmt->message when the AST carries one; kept
    // empty for asserts without a message (current AssertStmtSyntax shape) so
    // the field is future-proof for when messages are added to the AST.
    std::string assert_message;
};

struct TypedTemporalExpr {
    TypedTemporalKind kind{TypedTemporalKind::None};
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id{0};
    // Atom:          children_index[0] is expressions index
    // Unary:         children_index[0] is temporal_exprs index
    // Binary:        children_index[0], [1] are temporal_exprs indexes
    std::vector<std::uint32_t> children_index;
    // Strongly-typed op enum (T1.7 P1): covers every concrete temporal
    // construct so lower_typed_temporal can dispatch without string-parsing.
    TypedTemporalOp op{TypedTemporalOp::Atom};
    // NameLiteral/StateLiteral payload (T1.7 P1 – kept for diagnostic
    // readability and as a backstop). For Unary/Binary the lowering pass
    // prefers `op` over parsing payload_spelling.
    //   Called:    canonical capability name (no "called:" prefix)
    //   Running:   node name
    //   Completed: "node_name|optional_state_name"
    //   InState:   state name
    std::string payload_spelling;
};

struct TypedSymbolLocalKey {
    SymbolNamespace name_space{SymbolNamespace::Types};
    std::string name;
    std::string module_name;

    [[nodiscard]] friend bool operator==(const TypedSymbolLocalKey &,
                                         const TypedSymbolLocalKey &) noexcept = default;
};

struct TypedSymbolLocalKeyHash {
    [[nodiscard]] std::size_t operator()(const TypedSymbolLocalKey &key) const noexcept;
};

struct TypedReferenceKey {
    ReferenceKind kind{ReferenceKind::TypeName};
    std::size_t begin_offset{0};
    std::size_t end_offset{0};
    std::size_t source_id_value{0};
    bool has_source_id{false};

    [[nodiscard]] friend bool operator==(const TypedReferenceKey &,
                                         const TypedReferenceKey &) noexcept = default;
};

struct TypedReferenceKeyHash {
    [[nodiscard]] std::size_t operator()(const TypedReferenceKey &key) const noexcept;
};

struct TypedReferenceNoSourceKey {
    ReferenceKind kind{ReferenceKind::TypeName};
    std::size_t begin_offset{0};
    std::size_t end_offset{0};

    [[nodiscard]] friend bool operator==(const TypedReferenceNoSourceKey &,
                                         const TypedReferenceNoSourceKey &) noexcept = default;
};

struct TypedReferenceNoSourceKeyHash {
    [[nodiscard]] std::size_t operator()(const TypedReferenceNoSourceKey &key) const noexcept;
};

struct TypedProgram {
    std::vector<TypedDecl> declarations;
    std::vector<TypedExpr> expressions;
    std::vector<TypedBlock> blocks;
    std::vector<TypedStatement> statements;
    std::vector<TypedTemporalExpr> temporal_exprs;

    // Resolver snapshot copied into TypedProgram during typecheck. This makes
    // typed consumers independent of ResolveResult lifetime.
    std::vector<Symbol> symbols;
    std::vector<ResolvedReference> references;
    std::vector<ImportBinding> imports;
    std::unordered_map<std::size_t, std::uint32_t> symbol_id_index_;
    std::unordered_map<TypedSymbolLocalKey, std::uint32_t, TypedSymbolLocalKeyHash>
        symbol_local_index_;
    std::unordered_map<TypedReferenceKey, std::uint32_t, TypedReferenceKeyHash> reference_index_;
    std::unordered_map<TypedReferenceNoSourceKey, std::uint32_t, TypedReferenceNoSourceKeyHash>
        reference_no_source_index_;

    void rebuild_resolver_indices();
    void rebuild_indices();
    [[nodiscard]] MaybeCRef<Symbol> find_symbol(SymbolId id) const;
    [[nodiscard]] MaybeCRef<Symbol> find_local_symbol(SymbolNamespace name_space,
                                                      std::string_view name,
                                                      std::string_view module_name = "") const;
    [[nodiscard]] MaybeCRef<ResolvedReference>
    find_reference(ReferenceKind kind,
                   SourceRange range,
                   std::optional<SourceId> source_id = std::nullopt) const;

    // Reverse index: (node_id, source_id) → index into `expressions`.
    // Maintained by the typechecker on every push_back to `expressions`,
    // eliminating O(n) linear scans in append_typed_child and find_expr.
    struct ExprIndexKey {
        std::uint64_t node_id{0};
        std::size_t source_id_value{0}; // SourceId::value (0 when source_id is nullopt)
        bool has_source_id{false};

        [[nodiscard]] friend bool operator==(const ExprIndexKey &,
                                             const ExprIndexKey &) noexcept = default;
    };
    struct ExprIndexKeyHash {
        [[nodiscard]] std::size_t operator()(const ExprIndexKey &k) const noexcept {
            // FNV-1a–style combine; good enough for this index.
            std::size_t h = k.node_id;
            h ^= k.source_id_value * 0x9e3779b97f4a7c15ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(k.has_source_id) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<ExprIndexKey, std::uint32_t, ExprIndexKeyHash> expr_index_;

    [[nodiscard]] const TypedExpr *find_expr(std::uint64_t node_id,
                                             std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedExpr *find_expr(std::uint64_t node_id,
                                       std::optional<SourceId> source_id) noexcept;

    // Range-based exact match. Equivalent to the legacy
    // TypeCheckResult::find_expression_type(range, source_id).
    [[nodiscard]] const TypedExpr *
    find_expr_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedExpr *find_expr_by_range(SourceRange range,
                                                std::optional<SourceId> source_id) noexcept;

    // Find the most specific (smallest) expression whose range contains the
    // given byte offset. Used by LSP hover for "what type is under the cursor".
    [[nodiscard]] const TypedExpr *
    find_expr_containing(std::size_t offset, std::optional<SourceId> source_id) const noexcept;

    // Range-based exact match for blocks and statements.
    // Used by the typed-tree IR lowering pass to bridge from AST structures
    // (BlockSyntax / StatementSyntax) back into their flat typed-store
    // counterparts. Falls back to nullptr when the typed record has not been
    // recorded (e.g. legacy tests that build a TypedProgram without
    // statement/block vectors) so the caller can transparently switch between
    // the AST-fallback and typed-store paths.
    [[nodiscard]] const TypedBlock *
    find_block_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedBlock *find_block_by_range(SourceRange range,
                                                  std::optional<SourceId> source_id) noexcept;
    [[nodiscard]] const TypedStatement *find_statement_by_range(
        SourceRange range, TypedStmtKind kind, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedStatement *find_statement_by_range(
        SourceRange range, TypedStmtKind kind, std::optional<SourceId> source_id) noexcept;

    // Range-based exact match for temporal expressions.
    // Used by the typed-tree IR lowering pass to bridge from
    // ast::TemporalExprSyntax* (handed out by contract/flow/workflow
    // declaration structural walks) into the flat typed temporal store.
    // Falls back to nullptr when the typed record has not been recorded
    // so the caller can transparently fall back to the AST-based
    // lower_temporal path.
    [[nodiscard]] const TypedTemporalExpr *
    find_temporal_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedTemporalExpr *
    find_temporal_by_range(SourceRange range, std::optional<SourceId> source_id) noexcept;
};

// ----------------------------------------------------------------------------
// TypedExpr visitor (AST-independent dispatch)
// ----------------------------------------------------------------------------
//
// `typed_visit(expr, visitor)` dispatches a TypedExpr to the matching
// `Visitor::visit_*(expr)` overload. Unlike `ast::visit_expr_syntax` this
// dispatcher never inspects `ast::ExprSyntax`; it reads the node kind,
// children, and payload from the TypedExpr itself so passes (typed-tree IR
// lowering, post-typecheck rewrites, equality checks) can walk the typed
// representation without pulling in the AST.
//
// Children ordering is guaranteed to match `ast::visit_expr_syntax` dispatch
// semantics so lowering produces byte-identical IR to the legacy path:
//   * Some           -> [Operand]
//   * Call           -> [Argument, ...]          (each child.name == param name)
//   * StructLiteral  -> [StructFieldValue, ...]  (each child.name == field name)
//   * List/Set       -> [CollectionElement, ...]
//   * Map            -> [MapKey, MapValue, ...]  (interleaved pairs)
//   * Unary          -> [Operand]
//   * Binary         -> [LeftOperand, RightOperand]
//   * MemberAccess   -> [Base]                   (child.name == member name)
//   * IndexAccess    -> [Base, Index]
//   * Group          -> [Grouped]
//   * Literals/Path/QualifiedValue -> no children
//
// The project's -Wswitch -Werror guard catches missing cases when
// `ast::ExprSyntaxKind` / `TypedExpr` coverage grows.
template <typename Visitor> decltype(auto) typed_visit(const TypedExpr &expr, Visitor &&visitor) {
    switch (expr.kind) {
    case ast::ExprSyntaxKind::BoolLiteral:
        return std::forward<Visitor>(visitor).visit_bool_literal(expr);
    case ast::ExprSyntaxKind::IntegerLiteral:
        return std::forward<Visitor>(visitor).visit_integer_literal(expr);
    case ast::ExprSyntaxKind::FloatLiteral:
        return std::forward<Visitor>(visitor).visit_float_literal(expr);
    case ast::ExprSyntaxKind::DecimalLiteral:
        return std::forward<Visitor>(visitor).visit_decimal_literal(expr);
    case ast::ExprSyntaxKind::StringLiteral:
        return std::forward<Visitor>(visitor).visit_string_literal(expr);
    case ast::ExprSyntaxKind::DurationLiteral:
        return std::forward<Visitor>(visitor).visit_duration_literal(expr);
    case ast::ExprSyntaxKind::NoneLiteral:
        return std::forward<Visitor>(visitor).visit_none_literal(expr);
    case ast::ExprSyntaxKind::Some:
        return std::forward<Visitor>(visitor).visit_some(expr);
    case ast::ExprSyntaxKind::Path:
        return std::forward<Visitor>(visitor).visit_path(expr);
    case ast::ExprSyntaxKind::QualifiedValue:
        return std::forward<Visitor>(visitor).visit_qualified_value(expr);
    case ast::ExprSyntaxKind::Call:
        return std::forward<Visitor>(visitor).visit_call(expr);
    case ast::ExprSyntaxKind::StructLiteral:
        return std::forward<Visitor>(visitor).visit_struct_literal(expr);
    case ast::ExprSyntaxKind::ListLiteral:
        return std::forward<Visitor>(visitor).visit_list_literal(expr);
    case ast::ExprSyntaxKind::SetLiteral:
        return std::forward<Visitor>(visitor).visit_set_literal(expr);
    case ast::ExprSyntaxKind::MapLiteral:
        return std::forward<Visitor>(visitor).visit_map_literal(expr);
    case ast::ExprSyntaxKind::Unary:
        return std::forward<Visitor>(visitor).visit_unary(expr);
    case ast::ExprSyntaxKind::Binary:
        return std::forward<Visitor>(visitor).visit_binary(expr);
    case ast::ExprSyntaxKind::MemberAccess:
        return std::forward<Visitor>(visitor).visit_member_access(expr);
    case ast::ExprSyntaxKind::IndexAccess:
        return std::forward<Visitor>(visitor).visit_index_access(expr);
    case ast::ExprSyntaxKind::Group:
        return std::forward<Visitor>(visitor).visit_group(expr);
    }

    return std::forward<Visitor>(visitor).visit_unknown(expr);
}

// ----------------------------------------------------------------------------
// TypedStatement visitor
// ----------------------------------------------------------------------------
//
// `typed_visit(stmt, visitor)` dispatches a TypedStatement to the matching
// `Visitor::visit_*_stmt(stmt)` overload based on TypedStmtKind. Coverage is
// enforced by the project's -Wswitch guard.
template <typename Visitor>
decltype(auto) typed_visit(const TypedStatement &stmt, Visitor &&visitor) {
    switch (stmt.kind) {
    case TypedStmtKind::Let:
        return std::forward<Visitor>(visitor).visit_let_stmt(stmt);
    case TypedStmtKind::Assign:
        return std::forward<Visitor>(visitor).visit_assign_stmt(stmt);
    case TypedStmtKind::If:
        return std::forward<Visitor>(visitor).visit_if_stmt(stmt);
    case TypedStmtKind::Goto:
        return std::forward<Visitor>(visitor).visit_goto_stmt(stmt);
    case TypedStmtKind::Return:
        return std::forward<Visitor>(visitor).visit_return_stmt(stmt);
    case TypedStmtKind::Assert:
        return std::forward<Visitor>(visitor).visit_assert_stmt(stmt);
    case TypedStmtKind::ExprStatement:
        return std::forward<Visitor>(visitor).visit_expr_stmt(stmt);
    case TypedStmtKind::None:
        break;
    }

    return std::forward<Visitor>(visitor).visit_unknown_stmt(stmt);
}

} // namespace ahfl
