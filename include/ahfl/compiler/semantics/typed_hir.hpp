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
                                      ContractTypeInfo,
                                      FnTypeInfo,
                                      // P3 (RFC §3.2.2 / type-system §1.3 / §1.4)
                                      TraitTypeInfo,
                                      ImplTypeInfo>;

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
    // P1 (ADT): match arm sub-expressions. The scrutinee is tracked as
    // Operand; each arm's guard and body are tracked under these roles so the
    // typed-tree graph covers all match sub-expressions.
    MatchArmGuard,
    MatchArmBody,
};

enum class TypedCallTargetKind {
    None,
    Capability,
    Predicate,
    // P2 (RFC §3.2.2): a call resolving to a top-level `fn`. Mirrors the
    // capability/predicate kinds so the existing switch sites (LSP hover,
    // typed-tree lowering) treat a fn call uniformly.
    Function,
};

enum class ConstValueKind : std::uint8_t {
    NoneLiteral = 0,
    Bool,
    Integer,
    Float,
    Decimal,
    String,
    Duration,
    EnumVariant,
    ConstReference,
    Some,
    Struct,
    List,
    Set,
    Map,
    Unary,
    Binary,
    MemberAccess,
    IndexAccess,
};

struct ConstValue {
    ConstValueKind kind{ConstValueKind::NoneLiteral};
    std::string scalar;
    std::optional<SymbolId> symbol;
    std::vector<std::string> child_names;
    std::vector<ConstValue> children;
};

struct ConstDependencyEdge {
    SymbolId source;
    SymbolId target;
    SourceRange reference_range;
    std::optional<SourceId> source_id;
};

struct TypedExpr;

// ----------------------------------------------------------------------------
// TypedStatement kind enum (decoupled from ast::StatementSyntaxKind)
// ----------------------------------------------------------------------------
enum class TypedStmtKind : std::uint8_t {
    None = 0, // reserved zero value
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
// The lowering pass reconstructs an ir::TypeRef from typed records without
// re-entering the AST or reparsing source spelling:
//   * NoAnnotation       -> no type was written; use the initializer TypedExpr type.
//   * FromSyntax         -> user wrote a type annotation; let_type carries the
//                           resolved annotation type.
//   * FromInitializerType-> no annotation; use the current initializer TypedExpr
//                           type, with let_type as the captured typecheck result.
//   * MissingType        -> error path; lowering rejects missing required type facts.
enum class LetTypeRefStrategy : std::uint32_t {
    NoAnnotation = 0,
    FromSyntax,
    FromInitializerType,
    MissingType,
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
// Replaces ad-hoc payload_spelling parsing for Unary/Binary. Literal payloads
// still carry their domain data in TypedTemporalExpr::payload_spelling.
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
    std::string literal_spelling;           // Integer/Float/Decimal/String/Duration literal
    std::string member_name;                // MemberAccess (right-hand field name)
    std::vector<std::string> lambda_params; // Lambda parameter names in declaration order.
    std::optional<ConstValue> const_value;
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

    // Let type annotation payload. let_type is the canonical semantic input for
    // Typed HIR -> Semantic IR lowering.
    LetTypeRefStrategy let_type_ref_strategy{LetTypeRefStrategy::NoAnnotation};
    TypePtr let_type{nullptr};

    // Assign target path root kind (T1.7 P1).
    // Mirrors ir::PathRootKind so typed-tree lowering builds an ir::Path
    // without reaching back into the originating PathSyntax.
    AssignTargetRootKind assign_target_root_kind{AssignTargetRootKind::Identifier};

    // Assert message (T1.7 P1).
    // Current AssertStmtSyntax has no user-facing message field, so this remains
    // empty and mirrors the current syntax model.
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
    // NameLiteral/StateLiteral payload data.
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

    // P2 (RFC §3.5 / §5): recorded fn call sites for the monomorphization
    // pass. Each entry ties a fn declaration (by its Function symbol id) to
    // the concrete type arguments selected at the call site, whether supplied
    // explicitly or inferred from argument types. Monomorphic calls use an
    // empty type-args list.
    struct FnCallSiteRecord {
        SymbolId fn_symbol{0};
        SourceRange call_range;
        std::optional<SourceId> source_id;
        // Concrete type arguments in declaration order. Empty for monomorphic
        // calls.
        std::vector<TypePtr> type_args;
    };
    std::vector<FnCallSiteRecord> fn_call_sites;

    // Resolver snapshot copied into TypedProgram during typecheck. This makes
    // typed consumers independent of ResolveResult lifetime.
    std::vector<Symbol> symbols;
    std::vector<ResolvedReference> references;
    std::vector<ImportBinding> imports;
    std::vector<ConstDependencyEdge> const_dependencies;
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

    // Range-based exact match used by type-aware tooling and diagnostics.
    [[nodiscard]] const TypedExpr *
    find_expr_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedExpr *find_expr_by_range(SourceRange range,
                                                std::optional<SourceId> source_id) noexcept;

    // Find the most specific (smallest) expression whose range contains the
    // given byte offset. Used by LSP hover for "what type is under the cursor".
    [[nodiscard]] const TypedExpr *
    find_expr_containing(std::size_t offset, std::optional<SourceId> source_id) const noexcept;

    // Range-based exact match for blocks and statements.
    // Used by type-aware consumers that need to map source ranges to flat
    // typed-store records.
    [[nodiscard]] const TypedBlock *
    find_block_by_range(SourceRange range, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedBlock *find_block_by_range(SourceRange range,
                                                  std::optional<SourceId> source_id) noexcept;
    [[nodiscard]] const TypedStatement *find_statement_by_range(
        SourceRange range, TypedStmtKind kind, std::optional<SourceId> source_id) const noexcept;
    [[nodiscard]] TypedStatement *find_statement_by_range(
        SourceRange range, TypedStmtKind kind, std::optional<SourceId> source_id) noexcept;

    // Range-based exact match for temporal expressions.
    // Used by type-aware consumers that need to map source ranges to flat
    // typed temporal records.
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
// Children ordering is guaranteed to match source AST construction semantics so
// typed lowering preserves stable IR fingerprints:
//   * Some           -> [Operand]
//   * Call           -> [Argument, ...]          (each child.name == param name)
//   * MethodCall     -> [Base, Argument, ...]
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
    case ast::ExprSyntaxKind::Path:
        return std::forward<Visitor>(visitor).visit_path(expr);
    case ast::ExprSyntaxKind::QualifiedValue:
        return std::forward<Visitor>(visitor).visit_qualified_value(expr);
    case ast::ExprSyntaxKind::Call:
        return std::forward<Visitor>(visitor).visit_call(expr);
    case ast::ExprSyntaxKind::MethodCall:
        return std::forward<Visitor>(visitor).visit_method_call(expr);
    case ast::ExprSyntaxKind::StructLiteral:
        return std::forward<Visitor>(visitor).visit_struct_literal(expr);
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
    case ast::ExprSyntaxKind::Match:
        return std::forward<Visitor>(visitor).visit_match(expr);
    case ast::ExprSyntaxKind::Lambda:
        return std::forward<Visitor>(visitor).visit_lambda(expr);
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
