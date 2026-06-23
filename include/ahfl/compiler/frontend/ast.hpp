#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl::ast {

// ============================================================================
// AHFL Abstract Syntax Tree (AST) definition
// ============================================================================
//
// This file defines the AST node structure for the AHFL compiler frontend. The
// AST is the stable syntactic boundary after parse-tree lowering, consumed
// incrementally by Resolver -> TypeChecker -> IR Lowering.
//
// Design constraints:
//   1. AST nodes do not retain ANTLR parse-tree context or token references
//   2. AST nodes do not carry semantic/type information (built independently in
//      later passes)
//   3. All child nodes are lifetime-managed via Owned<T> (unique_ptr)
//
// Node hierarchy:
//   Node (base)
//   ├── Program         — top-level compilation unit
//   └── Decl (abstract) — base of all declarations
//       ├── ModuleDecl      — module declaration
//       ├── ImportDecl      — import declaration
//       ├── ConstDecl       — constant declaration
//       ├── TypeAliasDecl   — type alias
//       ├── StructDecl      — struct definition
//       ├── EnumDecl        — enum definition
//       ├── CapabilityDecl  — capability declaration (external call interface)
//       ├── PredicateDecl   — predicate declaration (for formal verification)
//       ├── AgentDecl       — Agent definition (state machine)
//       ├── ContractDecl    — contract declaration (preconditions/postconditions)
//       ├── FlowDecl        — flow definition (Agent state-handling logic)
//       └── WorkflowDecl    — workflow definition (multi-Agent DAG orchestration)

class Visitor;

// ----------------------------------------------------------------------------
// Enum type definitions
// ----------------------------------------------------------------------------

/// AST node kind, used for fast type checks
enum class NodeKind {
    Program,
    ModuleDecl,
    ImportDecl,
    ConstDecl,
    TypeAliasDecl,
    StructDecl,
    EnumDecl,
    CapabilityDecl,
    PredicateDecl,
    AgentDecl,
    ContractDecl,
    FlowDecl,
    WorkflowDecl,
    FnDecl,    // P2 (RFC §3.2.2): top-level function declaration
    TraitDecl, // P3 (RFC §3.2.2 / type-system §1.3): trait declaration
    ImplDecl,  // P3 (RFC §3.2.2 / type-system §1.4): impl block
};

[[nodiscard]] std::string_view to_string(NodeKind kind) noexcept;

/// Contract clause kinds
/// - Requires: precondition (must hold before a call)
/// - Ensures: postcondition (guaranteed after execution)
/// - Invariant: invariant (always holds)
/// - Forbid: forbidden condition (must never occur)
/// - Decreases: termination measure (strictly decreases on every recursive/iterative step)
enum class ContractClauseKind {
    Requires,
    Ensures,
    Invariant,
    Forbid,
    Decreases,
};

[[nodiscard]] std::string_view to_string(ContractClauseKind kind) noexcept;

/// Agent quota item kind
enum class AgentQuotaItemKind {
    MaxToolCalls,     // maximum tool call count
    MaxExecutionTime, // maximum execution time
};

/// State policy item kind (execution policy for state handlers in a flow)
enum class StatePolicyItemKind {
    Retry,   // retry count
    RetryOn, // retry on a specific error type
    Timeout, // timeout limit
};

/// Capability effect category, used by assurance analysis to derive control, policy, and recovery obligations
enum class CapabilityEffectKind {
    Unknown,
    Read,
    ExternalSideEffect,
    DurableWrite,
    FinancialWrite,
};

[[nodiscard]] std::string_view to_string(CapabilityEffectKind kind) noexcept;

/// Capability execution receipt requirement
enum class CapabilityReceiptMode {
    None,
    Optional,
    Required,
};

[[nodiscard]] std::string_view to_string(CapabilityReceiptMode mode) noexcept;

/// Capability retry safety declaration
enum class CapabilityRetryMode {
    Unsafe,
    SafeIfIdempotent,
    Safe,
};

[[nodiscard]] std::string_view to_string(CapabilityRetryMode mode) noexcept;

/// Path expression root kind
/// - Identifier: a plain identifier (variable name, node output reference, etc.)
/// - Input: the `input` keyword (input of an agent/workflow)
/// - Output: the `output` keyword (output context of an agent)
enum class PathRootKind {
    Identifier,
    Input,
    Output,
};

/// Effect clause kind (P2, RFC §2). Lifted before TypeSyntax so that FnType
/// can reference the enum without pulling in the full EffectClauseSyntax
/// (which depends on ExprSyntax).
enum class EffectClauseKind {
    Pure,
    Nondet,
    Capability,
};

[[nodiscard]] std::string_view to_string(EffectClauseKind kind) noexcept;

/// Expression syntax kinds (sugar variants removed — 18 kinds)
enum class ExprSyntaxKind {
    BoolLiteral,     // true / false
    IntegerLiteral,  // 42, -1
    FloatLiteral,    // 3.14
    DecimalLiteral,  // 3.14d
    StringLiteral,   // "hello"
    DurationLiteral, // 30s, 5m
    Path,            // input.field, ctx.field, node_name.field
    QualifiedValue,  // Priority::High (fully qualified enum value)
    Call,            // capability_name(args...)
    MethodCall,      // receiver.method(args...)
    StructLiteral,   // TypeName { field: value, ... }
    Unary,           // !expr, -expr
    Binary,          // a + b, a == b, a && b
    MemberAccess,    // expr.member
    IndexAccess,     // expr[index]
    Group,           // (expr) parenthesized grouping
    Match,           // match scrutinee { arm* } (P1 ADT, RFC §1.6)
    Lambda,          // \ params -> expr  (P2 closures, RFC §6)
};

/// Unary operators
enum class ExprUnaryOp {
    Not,      // logical not !
    Negate,   // negation -
    Positive, // unary plus +
};

/// Binary operators (ordered by precedence, low to high)
enum class ExprBinaryOp {
    Implies,      // =>  implication (used for contract/temporal logic)
    Or,           // ||
    And,          // &&
    Equal,        // ==
    NotEqual,     // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=
    Add,          // +
    Subtract,     // -
    Multiply,     // *
    Divide,       // /
    Modulo,       // %
};

/// Statement kinds
enum class StatementSyntaxKind {
    Let,    // let x: Type = expr;
    Assign, // ctx.field = expr;
    If,     // if (cond) { ... } else { ... }
    Goto,   // goto StateName;
    Return, // return expr;
    Assert, // assert(cond);
    Expr,   // expr; (expression statement, typically a capability call)
};

/// Temporal expression kinds (for formal verification / SMV generation)
enum class TemporalExprSyntaxKind {
    EmbeddedExpr, // embeds a plain expression
    Called,       // called(capability_name)
    InState,      // in_state(state_name)
    Running,      // running(node_name)
    Completed,    // completed(node_name[, state_name])
    Unary,        // always/eventually/next/not prefix
    Binary,       // implies/or/and/until infix
};

/// Temporal unary operators
enum class TemporalUnaryOp {
    Always,     // [] (always) — always holds on all paths
    Eventually, // <> (eventually) — eventually holds
    Next,       // O (next) — holds at the next step
    Not,        // ! (not) — negation
};

/// Temporal binary operators
enum class TemporalBinaryOp {
    Implies, // =>
    Or,      // ||
    And,     // &&
    Until,   // U (until) — holds until a condition holds
};

// ----------------------------------------------------------------------------
// Syntax fragment structures
// ----------------------------------------------------------------------------

/// A qualified name (e.g. module::path::Name)
struct QualifiedName {
    ahfl::SourceRange range;
    std::vector<std::string> segments; // ["module", "path", "Name"]

    [[nodiscard]] std::string spelling() const;
};

// Forward declaration (Owned<TypeSyntax> is used inside the Type variant)
struct TypeSyntax;

// ----------------------------------------------------------------------------
// Type variant alternatives (std::variant-style type syntax nodes)
// ----------------------------------------------------------------------------
// Each type maps to a dedicated struct, carried uniformly by the
// TypeSyntaxNode variant.

/// Unit type: ()
struct UnitType {};

/// Bool type: bool
struct BoolType {};

/// Int type: int
struct IntType {};

/// Float type: float
struct FloatType {};

/// String type: string
struct StringType {};

/// Bounded string type: string(min..max)
struct BoundedStringType {
    std::uint64_t min_length;
    std::uint64_t max_length;
};

/// UUID type: uuid
struct UuidType {};

/// Timestamp type: timestamp
struct TimestampType {};

/// Duration type: duration
struct DurationType {};

/// Decimal type: decimal(scale)
struct DecimalType {
    std::uint8_t scale;
};

/// Named type: references a struct/enum/type alias or parameterised
/// built-in such as Optional<T>, List<T>, Set<T>, Map<K, V>. When
/// `type_args` is empty the type is a simple named reference.
struct NamedType {
    Owned<QualifiedName> name;
    std::vector<Owned<TypeSyntax>> type_args;
};

/// Function type: Fn(A1, A2, ...) -> Ret [effect Spec]
struct FnType {
    std::vector<Owned<TypeSyntax>> params;
    Owned<TypeSyntax> return_type; // null if omitted (defaults to Unit)
    bool has_effect_clause{false};
    EffectClauseKind effect_kind{EffectClauseKind::Pure};
    std::vector<Owned<QualifiedName>>
        effect_capabilities; // populated when effect_kind == Capability
};

/// Nominal type application: Vec<Int>, std::collections::Map<String, Int>
struct AppType {
    Owned<QualifiedName> name;
    std::vector<Owned<TypeSyntax>> arguments;
    SourceRange range;
};

/// Variant alias for the type syntax node
using TypeSyntaxNode = std::variant<UnitType,
                                    BoolType,
                                    IntType,
                                    FloatType,
                                    StringType,
                                    BoundedStringType,
                                    UuidType,
                                    TimestampType,
                                    DurationType,
                                    DecimalType,
                                    NamedType,
                                    FnType,
                                    AppType>;

/// Type syntax node
struct TypeSyntax {
    ahfl::SourceRange range;
    TypeSyntaxNode node;

    /// Test whether the current node is of the given type
    template <typename T> [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(node);
    }

    /// Read the node as the given type (const overload)
    template <typename T> [[nodiscard]] const T &as() const {
        return std::get<T>(node);
    }

    /// Read the node as the given type (non-const overload)
    template <typename T> [[nodiscard]] T &as() {
        return std::get<T>(node);
    }

    [[nodiscard]] std::string spelling() const;
};

// Forward declarations
struct ExprSyntax;
struct StatementSyntax;
struct IntegerSyntax;
struct DurationSyntax;

/// Path expression syntax (e.g. input.category, ctx.resolved, classify.confidence)
struct PathSyntax {
    ahfl::SourceRange range;
    PathRootKind root_kind{PathRootKind::Identifier}; // root kind
    std::string root_name;            // root name ("input", "ctx", or an identifier)
    std::vector<std::string> members; // member access chain

    [[nodiscard]] std::string spelling() const;
};

/// A single key-value pair within a map literal
struct MapEntrySyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> key;
    Owned<ExprSyntax> value;
};

/// A single field initializer within a struct literal
struct StructInitSyntax {
    ahfl::SourceRange range;
    std::string field_name;  // field name
    Owned<ExprSyntax> value; // initializer expression
};

// ----------------------------------------------------------------------------
// Expr variant alternatives (std::variant-style expression syntax nodes)
// ----------------------------------------------------------------------------
// Each expression maps to a dedicated struct, carried uniformly by the
// ExprSyntaxNode variant.
// Field names are semantic; they correspond to the generic fields of the legacy
// ExprSyntax (first/second/name/text, etc.).

/// none literal

/// bool literal: true / false
struct BoolLiteralExpr {
    bool value;
};

/// Integer literal: 42, -1
struct IntegerLiteralExpr {
    Owned<IntegerSyntax> literal;
};

/// Float literal: 3.14
struct FloatLiteralExpr {
    std::string spelling;
};

/// Decimal literal: 3.14d
struct DecimalLiteralExpr {
    std::string spelling;
};

/// String literal: "hello"
struct StringLiteralExpr {
    std::string spelling;
};

/// Duration literal: 30s, 5m
struct DurationLiteralExpr {
    Owned<DurationSyntax> literal;
};

/// some expression: some(expr)

/// Path expression: input.field, ctx.field
struct PathExpr {
    Owned<PathSyntax> path;
};

/// Qualified value expression: Priority::High
struct QualifiedValueExpr {
    Owned<QualifiedName> name;
};

/// Function call expression: callee(args...)
struct CallExpr {
    Owned<QualifiedName> callee;
    std::vector<Owned<TypeSyntax>> type_args;
    std::vector<Owned<ExprSyntax>> arguments;
};

/// Method call expression: receiver.method(args...)
struct MethodCallExpr {
    Owned<ExprSyntax> receiver;
    std::string method;
    std::vector<Owned<TypeSyntax>> type_args;
    std::vector<Owned<ExprSyntax>> arguments;
};

/// Struct literal: TypeName { field: value, ... }
struct StructLiteralExpr {
    Owned<QualifiedName> type_name;
    std::vector<Owned<StructInitSyntax>> fields;
};

/// List literal: [a, b, c]

/// Set literal: {a, b, c}

/// Map literal: {k1: v1, k2: v2}

/// Unary expression: !expr, -expr
struct UnaryExpr {
    ExprUnaryOp op;
    Owned<ExprSyntax> operand;
};

/// Binary expression: a + b, a == b
struct BinaryExpr {
    ExprBinaryOp op;
    Owned<ExprSyntax> lhs;
    Owned<ExprSyntax> rhs;
};

/// Member access expression: expr.member
struct MemberAccessExpr {
    Owned<ExprSyntax> base;
    std::string member;
};

/// Index access expression: expr[index]
struct IndexAccessExpr {
    Owned<ExprSyntax> base;
    Owned<ExprSyntax> index;
};

/// Grouping expression: (expr)
struct GroupExpr {
    Owned<ExprSyntax> inner;
};

// ----------------------------------------------------------------------------
// P1 (ADT) pattern syntax (RFC §1.6)
// ----------------------------------------------------------------------------
//
// Patterns form a tagged-union analogous to ExprSyntaxNode. Each concrete
// pattern form maps to a dedicated struct carried uniformly by the
// PatternSyntaxNode variant. Match arms reference PatternSyntax, and the
// match expression references MatchArmSyntax.

// Forward declaration (Owned<PatternSyntax> appears inside pattern variants)
struct PatternSyntax;

/// Literal pattern: true / false / integer / float / string / none (RFC §1.6).
/// The literal spelling is stored verbatim; typecheck narrows the scrutinee
/// accordingly. `none` is sugar for Option::None.
struct LiteralPattern {
    std::string spelling; // original literal text ("true", "42", "\"s\"", "none")
};

/// Variant (ADT constructor) pattern: `Option::Some(x)`, `Some(x)`, `Err(_)`.
///
/// `path` carries the qualified variant name; `subpatterns` carries the
/// optional positional sub-patterns. When `subpatterns` is empty, the variant
/// is matched without inspecting its payload.
struct VariantPattern {
    Owned<QualifiedName> path;
    std::vector<Owned<PatternSyntax>> subpatterns;
};

/// Wildcard pattern: `_`. Always matches; binds nothing.
struct WildcardPattern {};

/// Binding pattern: `mut? name [@ nested]` (RFC §1.6).
///
/// `name` binds the scrutinee (or sub-value) to a local. `nested` is the
/// optional `@`-bound inner pattern (`x @ Some(y)`). `is_mut` is reserved —
/// accepted by the grammar for forward compatibility, but treated as immutable
/// binding by the first version of the type system.
struct BindingPattern {
    std::string name;
    bool is_mut{false};
    Owned<PatternSyntax> nested; // optional `@`-bound sub-pattern
};

/// Tuple pattern: `(p1, p2, ...)`. Empty list matches the unit tuple `()`.
struct TuplePattern {
    std::vector<Owned<PatternSyntax>> elements;
};

/// Or pattern: `p1 | p2 | ...`. Each branch is a non-or `concatPattern`;
/// the typechecker requires all branches to bind equivalent names/types
/// (RFC §1.6).
struct OrPattern {
    std::vector<Owned<PatternSyntax>> branches;
};

/// Variant alias for the pattern syntax node
using PatternSyntaxNode = std::variant<LiteralPattern,
                                       VariantPattern,
                                       WildcardPattern,
                                       BindingPattern,
                                       TuplePattern,
                                       OrPattern>;

/// Pattern syntax node (tagged-union via std::variant).
///
/// Mirrors the ExprSyntax tagged-union pattern: each variant alternative
/// carries semantically named fields, and std::visit enforces full coverage
/// when new alternatives are added.
struct PatternSyntax {
    ahfl::SourceRange range;
    std::string text; // original source text

    PatternSyntaxNode node;

    /// Test whether the current node is of the given pattern type
    template <typename T> [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(node);
    }

    /// Read the node as the given type (const overload)
    template <typename T> [[nodiscard]] const T &as() const {
        return std::get<T>(node);
    }

    /// Read the node as the given type (non-const overload)
    template <typename T> [[nodiscard]] T &as() {
        return std::get<T>(node);
    }
};

/// Pattern kind discriminator (parallels ExprSyntaxKind).
enum class PatternSyntaxKind {
    Literal,
    Variant,
    Wildcard,
    Binding,
    Tuple,
    Or,
};

/// A single `match` arm: `pattern [if guard] => expr` (RFC §1.6).
struct MatchArmSyntax {
    ahfl::SourceRange range;
    Owned<PatternSyntax> pattern; // arm pattern
    Owned<ExprSyntax> guard;      // optional `if` guard
    Owned<ExprSyntax> body;       // arm body expression
};

/// `match` expression: `match scrutinee { arm* }` (RFC §1.6).
///
/// Carried as an ExprSyntaxNode variant alternative so `match` is a regular
/// primaryExpr. The scrutinee and arms are owned child nodes; exhaustiveness,
/// narrowing, and payload typing are deferred to the typecheck pass (P1b).
struct MatchExpr {
    Owned<ExprSyntax> scrutinee;
    std::vector<Owned<MatchArmSyntax>> arms;
};

// ----------------------------------------------------------------------------
// P2 (RFC §3.2.2 / §3.2.3 / §2 / §6) function-declaration syntax fragments
// ----------------------------------------------------------------------------
//
// These fragments model the syntactic surface of top-level function
// declarations and closures. They carry no semantic/type information —
// generic instantiation, effect enforcement, where-clause evaluation, and
// closure capture analysis are all deferred to later passes (P2b/P2c).

/// Effect clause kind (RFC §2). A function's effect clause is one of:
///   - Pure      — declared side-effect free
///   - Nondet    — explicitly non-deterministic
///   - Capability — names one or more capabilities the function may exercise
///
/// The enum is defined earlier in this file (above TypeSyntax) so that FnType
/// can reference it. The comment is repeated here for locality with the rest of
/// the P2 fn-decl fragments.

/// A single generic type parameter: `IDENT [: bound [ '+' bound ]*]`.
///
/// `bounds` carries the optional type-bound list (RFC §3.2.3). It is a list so
/// multiple intersection bounds (`T: A + B`) compose; the typecheck pass
/// enforces that each bound is satisfiable.
struct TypeParamSyntax {
    ahfl::SourceRange range;
    std::string name;
    std::vector<Owned<TypeSyntax>> bounds; // optional type-bound list
};

/// Function effect clause (RFC §2): `Pure | Nondet | cap [, cap]*`.
///
/// `kind` discriminates the three clause shapes. When `kind == Capability`,
/// `capabilities` carries the named capability references; otherwise it is
/// empty.
///
/// P4a (RFC corelib-effect-system.zh.md §3.1): an optional `decreases` measure
/// expression may follow the effect spec — `effect Pure decreases expr` /
/// `effect Nondet decreases expr` / `effect <Cap>+ decreases expr`. `decreases_expr`
/// carries the measure (or is null when the clause has no decreases). Pure
/// functions require a decreases measure (RFC §2.6.4 / §3.4); non-Pure
/// functions may carry one optionally.
struct EffectClauseSyntax {
    ahfl::SourceRange range;
    EffectClauseKind kind{EffectClauseKind::Pure};
    std::vector<Owned<QualifiedName>> capabilities;
    Owned<ExprSyntax> decreases_expr; // optional P4a decreases measure (RFC §3.1)
};

/// A single where-clause constraint (RFC §6). Two shapes share this node:
///   - `Type::Trait(args...)`  — a type predicate (e.g. `T::Addable(U)`)
///   - `Type: Bound[ + Bound]*` — a bound list (e.g. `T: Hashable`)
///
/// `is_predicate` discriminates the two. For predicates, `trait_name` holds
/// the trait identifier and `arguments` the type-argument list. For bounds,
/// `bounds` holds the bound-type list.
struct WhereConstraintSyntax {
    ahfl::SourceRange range;
    bool is_predicate{false};
    Owned<TypeSyntax> subject;                // the constrained type
    std::string trait_name;                   // predicate trait identifier
    std::vector<Owned<TypeSyntax>> arguments; // predicate type arguments
    std::vector<Owned<TypeSyntax>> bounds;    // bound-list bound types
};

/// Where-clause: `where constraint [, constraint]*` (RFC §6).
struct WhereClauseSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<WhereConstraintSyntax>> constraints;
};

/// A single function parameter (reuses ParamDeclSyntax for `name: Type`).
/// `ParamDeclSyntax` is defined later; forward-declared via the existing
/// forward declaration at the top of the type fragment section.

/// Lambda (closure) parameter: `IDENT [: Type]`. The type annotation is
/// optional (RFC §6 allows inferred parameter types).
struct LambdaParamSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type; // optional type annotation
};

/// Lambda (closure) expression: `\ params -> expr` (RFC §6).
///
/// `params` may be empty (zero-arg thunk: `\ -> expr`). The body is a single
/// expression (RFC §6 closures are expression-bodied). Captured-variable
/// resolution and effect inference happen in the typecheck pass (P2b).
struct LambdaExpr {
    std::vector<Owned<LambdaParamSyntax>> params;
    Owned<ExprSyntax> body;
};

/// Variant alias for the expression syntax node
using ExprSyntaxNode = std::variant<BoolLiteralExpr,
                                    IntegerLiteralExpr,
                                    FloatLiteralExpr,
                                    DecimalLiteralExpr,
                                    StringLiteralExpr,
                                    DurationLiteralExpr,
                                    PathExpr,
                                    QualifiedValueExpr,
                                    CallExpr,
                                    MethodCallExpr,
                                    StructLiteralExpr,
                                    UnaryExpr,
                                    BinaryExpr,
                                    MemberAccessExpr,
                                    IndexAccessExpr,
                                    GroupExpr,
                                    MatchExpr,
                                    LambdaExpr>;

/// Expression syntax node
///
/// Implements a tagged-union pattern via std::variant. Each variant alternative
/// carries semantically named fields, and the compiler enforces at compile time
/// that only the fields of the active variant alternative are accessed. This
/// eliminates the field-misuse bugs inherent to the "fat struct" pattern.
///
/// Access patterns (ordered by recommendation):
///   1. std::visit(Overloaded{...}, expr.node) — dispatch across all kinds
///   2. expr.is<T>() + expr.as<T>() — direct access when the kind is known
///   3. visit_expr_syntax(expr, visitor) — visitor dispatch by method name
struct ExprSyntax {
    ahfl::SourceRange range;
    // Stable identity assigned at AST construction time. Distinct from
    // SourceRange so that two expressions with overlapping ranges (e.g. the
    // generated synthetic expressions and their parents) can be unambiguously
    // referenced by downstream side tables. 0 means "unassigned".
    std::uint64_t node_id{0};
    std::string text; // original source text (used for literal spellings, etc.)

    ExprSyntaxNode node;

    /// Test whether the current node is of the given expression type
    template <typename T> [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(node);
    }

    /// Read the node as the given type (const overload)
    template <typename T> [[nodiscard]] const T &as() const {
        return std::get<T>(node);
    }

    /// Read the node as the given type (non-const overload)
    template <typename T> [[nodiscard]] T &as() {
        return std::get<T>(node);
    }
};

// ----------------------------------------------------------------------------
// Statement syntax structures
// ----------------------------------------------------------------------------

/// let binding statement: let name: Type = initializer;
struct LetStmtSyntax {
    ahfl::SourceRange range;
    std::string name;              // variable name
    Owned<TypeSyntax> type;        // type annotation (optional)
    Owned<ExprSyntax> initializer; // initializer expression
};

/// Assignment statement: target = value; (only ctx.field may be assigned)
struct AssignStmtSyntax {
    ahfl::SourceRange range;
    Owned<PathSyntax> target; // assignment target path
    Owned<ExprSyntax> value;  // assigned expression
};

/// Statement block: { stmt1; stmt2; ... }
struct BlockSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatementSyntax>> statements;
};

/// Conditional statement: if (condition) { then } else { else }
struct IfStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition;   // condition expression
    Owned<BlockSyntax> then_block; // then branch
    Owned<BlockSyntax> else_block; // else branch (optional)
};

/// State jump statement: goto StateName;
struct GotoStmtSyntax {
    ahfl::SourceRange range;
    std::string target_state; // target state name
};

/// Return statement: return expr;
struct ReturnStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> value; // return value expression
};

/// Assertion statement: assert(condition);
struct AssertStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> condition; // assertion condition
};

/// Expression statement (e.g. a capability call): expr;
struct ExprStmtSyntax {
    ahfl::SourceRange range;
    Owned<ExprSyntax> expr;
};

/// Statement node (tagged-union; kind determines which stmt field is active)
struct StatementSyntax {
    ahfl::SourceRange range;
    StatementSyntaxKind kind{StatementSyntaxKind::Expr};
    Owned<LetStmtSyntax> let_stmt;
    Owned<AssignStmtSyntax> assign_stmt;
    Owned<IfStmtSyntax> if_stmt;
    Owned<GotoStmtSyntax> goto_stmt;
    Owned<ReturnStmtSyntax> return_stmt;
    Owned<AssertStmtSyntax> assert_stmt;
    Owned<ExprStmtSyntax> expr_stmt;
};

// Forward declaration (Owned<TemporalExprSyntax> is used inside the Temporal variant)
struct TemporalExprSyntax;

// ----------------------------------------------------------------------------
// Temporal variant alternatives (std::variant-style temporal expression nodes)
// ----------------------------------------------------------------------------
// Each temporal expression maps to a dedicated struct, carried uniformly by the
// TemporalExprSyntaxNode variant.
// Field names are semantic; they correspond to the generic fields of the legacy
// TemporalExprSyntax (first/second/name, etc.).

/// Embeds a plain expression
struct EmbeddedTemporalExpr {
    Owned<ExprSyntax> expr;
};

/// called(capability_name) — the capability has been called
struct CalledTemporalExpr {
    std::string name;
};

/// in_state(state_name) — currently in some state
struct InStateTemporalExpr {
    std::string name;
};

/// running(node_name) — a node is currently running
struct RunningTemporalExpr {
    std::string name;
};

/// completed(node_name, state_name?) — a node has completed
struct CompletedTemporalExpr {
    std::string name;
    std::optional<std::string> state_name;
};

/// Unary temporal expression: always / eventually / next / not prefix
struct UnaryTemporalExpr {
    TemporalUnaryOp op;
    Owned<TemporalExprSyntax> operand;
};

/// Binary temporal expression: implies / or / and / until infix
struct BinaryTemporalExpr {
    TemporalBinaryOp op;
    Owned<TemporalExprSyntax> lhs;
    Owned<TemporalExprSyntax> rhs;
};

/// Variant alias for the temporal syntax node
using TemporalExprSyntaxNode = std::variant<EmbeddedTemporalExpr,
                                            CalledTemporalExpr,
                                            InStateTemporalExpr,
                                            RunningTemporalExpr,
                                            CompletedTemporalExpr,
                                            UnaryTemporalExpr,
                                            BinaryTemporalExpr>;

/// Temporal logic expression
///
/// Implements a tagged-union pattern via std::variant.
/// Used for safety/liveness property verification, compiled into CTL/LTL
/// formulas consumable by an SMV model checker.
struct TemporalExprSyntax {
    ahfl::SourceRange range;

    TemporalExprSyntaxNode node;

    /// Test whether the current node is of the given temporal expression type
    template <typename T> [[nodiscard]] bool is() const {
        return std::holds_alternative<T>(node);
    }

    /// Read the node as the given type (const overload)
    template <typename T> [[nodiscard]] const T &as() const {
        return std::get<T>(node);
    }

    /// Read the node as the given type (non-const overload)
    template <typename T> [[nodiscard]] T &as() {
        return std::get<T>(node);
    }
};

// ----------------------------------------------------------------------------
// Auxiliary syntax fragments
// ----------------------------------------------------------------------------

/// Integer literal
struct IntegerSyntax {
    ahfl::SourceRange range;
    std::string spelling; // original text
    std::int64_t value{0};
};

/// Duration literal (e.g. "30s", "5m", "1h")
struct DurationSyntax {
    ahfl::SourceRange range;
    std::string spelling;
};

/// Parameter declaration (for capability parameters / function parameters /
/// trait method parameters / impl method parameters).
///
/// P3c.S1: when `is_self` is true the parameter is a `self` / `mut self` /
/// `self: Type` / `mut self: Type` receiver used inside a trait or impl
/// method. For a bare `self` / `mut self` (no explicit type), `type` is
/// null and the type is inferred as the implementing type by the semantic
/// layer (P3b). `is_self_mut` records whether the receiver was declared
/// mutable. `name` is always "self" for self receivers (so downstream code
/// that inspects param names still works uniformly).
struct ParamDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;       // null for bare `self` / `mut self`
    bool is_self{false};          // true for any self receiver shape
    bool is_self_mut{false};      // true only when `mut self` was written
};

/// Struct field declaration
struct StructFieldDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> default_value; // optional default value
};

/// Enum variant alternative declaration.
///
/// P1 (ADT, RFC §1.5): a variant optionally carries a positional tuple
/// payload (`Some(T)`, `Err(E)`). `payload` is empty for payload-less
/// variants, preserving full backward compatibility with the legacy
/// `enumDecl: IDENT` grammar.
struct EnumVariantDeclSyntax {
    ahfl::SourceRange range;
    std::string name;
    std::vector<Owned<TypeSyntax>> payload; // optional positional tuple payload
};

/// State transition declaration: from_state -> to_state
struct TransitionSyntax {
    ahfl::SourceRange range;
    std::string from_state;
    std::string to_state;
};

/// Agent quota item (resource limit)
struct AgentQuotaItemSyntax {
    ahfl::SourceRange range;
    AgentQuotaItemKind kind{AgentQuotaItemKind::MaxToolCalls};
    Owned<IntegerSyntax> integer_value;   // value for MaxToolCalls
    Owned<DurationSyntax> duration_value; // value for MaxExecutionTime
};

/// Agent quota declaration: quota { max_tool_calls: 10; max_execution_time: 30s; }
struct AgentQuotaSyntax {
    ahfl::SourceRange range;
    std::vector<Owned<AgentQuotaItemSyntax>> items;
};

/// Decreases clause attached to a contract clause:
///   * `decreases *`        -> decreases_is_wildcard = true
///   * `decreases (a, b, c)` -> decreases_exprs populated with owned exprs
/// The default-constructed state (empty + !wildcard) means the user did not
/// write a decreases clause; ContractClauseInfo::has_decreases reflects that
/// after typecheck plumbing.
struct ContractDecreasesSyntax {
    ahfl::SourceRange range;
    bool decreases_is_wildcard{false};
    std::vector<Owned<ExprSyntax>> decreases_exprs;
};

/// Contract clause: requires/ensures/invariant/forbid expr
struct ContractClauseSyntax {
    ahfl::SourceRange range;
    ContractClauseKind kind{ContractClauseKind::Requires};
    Owned<ExprSyntax> expr;                  // plain expression condition
    Owned<TemporalExprSyntax> temporal_expr; // temporal logic condition
    // P4.S3: optional decreases clause attached to the contract clause.
    // Owned<T> (nullptr) means no decreases was written.
    Owned<ContractDecreasesSyntax> decreases;
};

/// Decreases / termination-measure clause.
///
/// Plain syntax fragment (does NOT inherit from Decl / Node; not carried by
/// DeclKind / NodeKind).  Consumers dispatch on the concrete type directly.
///
/// Either:
///   * is_wildcard == true  → the clause is the wildcard form `decreases *;`
///   * is_wildcard == false → `terms` is the lexicographic tuple of
///     expressions that form the termination measure.
struct DecreasesClauseSyntax {
    ahfl::SourceRange range;
    /// Lexicographic tuple of measure expressions.  When `is_wildcard` is true
    /// this is typically empty, but both fields are independent so a consumer
    /// can always iterate `terms` without checking the flag first.
    std::vector<Owned<ExprSyntax>> terms;
    /// True for the wildcard / "don't verify" form.
    bool is_wildcard{false};
};

/// State policy item (execution policy within a flow handler)
struct StatePolicyItemSyntax {
    ahfl::SourceRange range;
    StatePolicyItemKind kind{StatePolicyItemKind::Retry};
    Owned<IntegerSyntax> retry_limit;           // retry count for Retry
    std::vector<Owned<QualifiedName>> retry_on; // error type list for RetryOn
    Owned<DurationSyntax> timeout;              // time limit for Timeout
};

/// State policy declaration: policy { retry: 3; timeout: 30s; }
struct StatePolicySyntax {
    ahfl::SourceRange range;
    std::vector<Owned<StatePolicyItemSyntax>> items;
};

/// Capability side-effect declaration block:
/// capability ChargeCard(request: Payment) -> Receipt {
///     effect: financial_write;
///     domain: payments;
///     idempotency: request.idempotency_key;
///     receipt: required;
///     retry: safe_if_idempotent;
///     timeout: 30s;
///     compensation: RefundCard;
///     policy: [payments::approval_required];
/// }
struct CapabilityEffectSyntax {
    ahfl::SourceRange range;
    CapabilityEffectKind effect_kind{CapabilityEffectKind::Unknown};
    Owned<QualifiedName> domain;
    Owned<PathSyntax> idempotency_key;
    CapabilityReceiptMode receipt_mode{CapabilityReceiptMode::None};
    CapabilityRetryMode retry_mode{CapabilityRetryMode::Unsafe};
    Owned<DurationSyntax> timeout;
    Owned<QualifiedName> compensation;
    std::vector<Owned<QualifiedName>> policies;
};

/// State handler: state StateName [policy {...}] { statements... }
struct StateHandlerSyntax {
    ahfl::SourceRange range;
    std::string state_name;          // which state is handled
    Owned<StatePolicySyntax> policy; // optional execution policy
    Owned<BlockSyntax> body;         // handling logic (statement block)
};

/// Workflow node declaration: node name: AgentType(input_expr) [after [dep1, dep2]];
struct WorkflowNodeDeclSyntax {
    ahfl::SourceRange range;
    std::string name;               // node name
    Owned<QualifiedName> target;    // the bound Agent type
    Owned<ExprSyntax> input;        // input expression passed to the agent
    std::vector<std::string> after; // DAG dependencies (predecessor node names)
};

// ----------------------------------------------------------------------------
// Core AST node class hierarchy
// ----------------------------------------------------------------------------

/// AST node base class (common interface for all nodes)
struct Node {
    NodeKind kind;
    ahfl::SourceRange range;

    explicit Node(NodeKind kind, ahfl::SourceRange range = {});
    virtual ~Node() = default;

    virtual void accept(Visitor &visitor) = 0;
};

/// Declaration base class (common base for all top-level declarations)
struct Decl : Node {
    Decl(NodeKind kind, ahfl::SourceRange range = {});
    ~Decl() override = default;

    /// Return a one-line summary of the declaration (for diagnostic output)
    [[nodiscard]] virtual std::string headline() const = 0;
};

/// Compilation unit (one .ahfl file maps to one Program)
struct Program final : Node {
    std::string source_name;               // source file name
    std::vector<Owned<Decl>> declarations; // all top-level declarations

    explicit Program(std::string source_name, ahfl::SourceRange range = {});

    void accept(Visitor &visitor) override;
};

/// AST structural invariant violation. Captures mismatches between a tagged
/// struct's kind and its payload fields.
struct AstInvariantViolation {
    ahfl::SourceRange range;
    std::string message;
};

[[nodiscard]] std::vector<AstInvariantViolation>
validate_program_invariants(const Program &program);

/// Module declaration: module a::b::c;
struct ModuleDecl final : Decl {
    Owned<QualifiedName> name;

    ModuleDecl(Owned<QualifiedName> name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Import declaration: import a::b::c [as alias];
struct ImportDecl final : Decl {
    Owned<QualifiedName> path; // import path
    std::string alias;         // optional alias

    ImportDecl(Owned<QualifiedName> path, std::string alias, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Constant declaration: const NAME: Type = value;
struct ConstDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> type;
    Owned<ExprSyntax> value;

    ConstDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Type alias: type NewName = ExistingType;
struct TypeAliasDecl final : Decl {
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    Owned<TypeSyntax> aliased_type;

    TypeAliasDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Struct declaration: struct Name<T> { field1: Type1; field2: Type2 = default; }
///
/// `type_params` carries the optional generic parameter list. An empty
/// vector means the struct is monomorphic (no type parameters).
struct StructDecl final : Decl {
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    std::vector<Owned<StructFieldDeclSyntax>> fields;
    Owned<WhereClauseSyntax> where_clause; // optional generic constraints

    StructDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Enum declaration: enum Name<T> { Variant1; Variant2(T); Variant3; }
///
/// `type_params` carries the optional generic parameter list. An empty
/// vector means the enum is monomorphic (no type parameters).
struct EnumDecl final : Decl {
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    std::vector<Owned<EnumVariantDeclSyntax>> variants;
    Owned<WhereClauseSyntax> where_clause; // optional generic constraints

    EnumDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Capability declaration: capability Name(param1: Type1, ...) -> ReturnType;
/// Represents an external interface callable by an Agent (e.g. LLM API, database query, etc.)
struct CapabilityDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<TypeSyntax> return_type;
    Owned<CapabilityEffectSyntax> effect;
    Owned<WhereClauseSyntax> where_clause; // optional generic constraints

    CapabilityDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Predicate declaration: predicate Name(param1: Type1, ...);
/// Used for logical assertions in formal verification.
///
/// P4a (RFC corelib-effect-system.zh.md §2.4): predicates are implicitly
/// effect Pure. The `effect_clause` field is always null — the grammar does
/// not allow an explicit effect clause on predicates. The field exists so the
/// semantic check (EffectOnPredicate) has a consistent shape to inspect; if
/// the grammar is ever extended, the diagnostic is already in place.
struct PredicateDecl final : Decl {
    std::string name;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<EffectClauseSyntax> effect_clause; // always null, see struct doc

    PredicateDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Agent declaration — the core entity of AHFL
///
/// Defines a finite-state-machine Agent:
///   agent Name {
///       input: InputType;
///       context: CtxType;
///       output: OutputType;
///       states: [Init, Processing, Done];
///       initial: Init;
///       final: [Done];
///       capabilities: [Cap1, Cap2];
///       transitions { Init -> Processing; Processing -> Done; }
///       quota { max_tool_calls: 10; }
///   }
struct AgentDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;                     // input type
    Owned<TypeSyntax> context_type;                   // context type (mutable state)
    Owned<TypeSyntax> output_type;                    // output type
    std::vector<std::string> states;                  // set of states
    ahfl::SourceRange states_range;                   // range of the states clause
    std::string initial_state;                        // initial state
    ahfl::SourceRange initial_state_range;            // range of the initial clause
    std::vector<std::string> final_states;            // set of final states
    ahfl::SourceRange final_states_range;             // range of the final clause
    std::vector<std::string> capabilities;            // available capability list
    ahfl::SourceRange capabilities_range;             // range of the capabilities clause
    Owned<AgentQuotaSyntax> quota;                    // resource quota
    std::vector<Owned<TransitionSyntax>> transitions; // legal state transitions

    AgentDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Contract declaration: contract for AgentName { requires ...; ensures ...; }
/// Attaches formal constraints to an Agent, compiled into an SMV model for verification
struct ContractDecl final : Decl {
    Owned<QualifiedName> target;                      // target Agent
    std::vector<Owned<ContractClauseSyntax>> clauses; // contract clause list

    ContractDecl(Owned<QualifiedName> target, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Flow declaration: flow for AgentName { state Init { ... } state Done { ... } }
/// Defines the concrete execution logic for each state of an Agent
struct FlowDecl final : Decl {
    Owned<QualifiedName> target;                           // target Agent
    std::vector<Owned<StateHandlerSyntax>> state_handlers; // handlers for each state

    FlowDecl(Owned<QualifiedName> target, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Workflow declaration — multi-Agent DAG orchestration
///
///   workflow Name {
///       input: InputType;
///       output: OutputType;
///       node a: AgentA(input);
///       node b: AgentB(a.result) after [a];
///       safety { always(...); }
///       liveness { eventually(...); }
///       return OutputType { field: b.result };
///   }
struct WorkflowDecl final : Decl {
    std::string name;
    Owned<TypeSyntax> input_type;                     // Workflow input type
    Owned<TypeSyntax> output_type;                    // Workflow output type
    std::vector<Owned<WorkflowNodeDeclSyntax>> nodes; // DAG node list
    std::vector<Owned<TemporalExprSyntax>> safety;    // safety properties ([] always)
    std::vector<Owned<TemporalExprSyntax>> liveness;  // liveness properties (<> eventually)
    Owned<ExprSyntax> return_value;                   // final return value expression

    WorkflowDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Function declaration (RFC §3.2.2 / §3.2.3 / §2 / §6):
///
///   fn name<T: bound, U>(p: Type, ...) -> Ret [effect] [where ...] { ... }
///   fn name(...);   // prototype (no body)
///
/// `type_params` carries the optional generic parameter list; `params` the
/// positional parameter list; `return_type` the optional return annotation;
/// `effect_clause` the optional effect declaration (Pure/Nondet/capabilities);
/// `where_clause` the optional generic constraints; `body` the optional
/// statement block (empty for a prototype). All semantic resolution —
/// generic instantiation, effect enforcement, where-clause evaluation — is
/// deferred to the typecheck pass (P2b).
///
/// P5 (RFC §3.3 / corelib-stdlib-api.zh.md §5): `builtin_name` is set when the
/// fn declaration carries an `@builtin("name")` attribute. Only stdlib modules
/// may declare @builtin functions; the name maps to a compiler/runtime builtin
/// implementation. Empty (nullopt) means the fn is a normal user function.
struct FnDecl final : Decl {
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<TypeSyntax> return_type;
    Owned<EffectClauseSyntax> effect_clause;
    Owned<WhereClauseSyntax> where_clause;
    Owned<BlockSyntax> body;                 // empty for a prototype (`fn name(...);`)
    std::optional<std::string> builtin_name; // P5: @builtin name, nullopt if not a builtin

    FnDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

// ----------------------------------------------------------------------------
// P3 (RFC §3.2.2 / type-system §1.3 / §1.4) trait & impl syntax fragments
// ----------------------------------------------------------------------------
//
// These fragments model the syntactic surface of trait declarations and impl
// blocks. They carry no semantic/type information — trait resolution,
// coherence / orphan-rule enforcement, signature matching, associated-type
// defaulting, and where-clause evaluation are all deferred to the typecheck
// pass (P3b). The grammar accepts both inherent impls (no `TraitRef for`) and
// trait impls; the distinction is recorded via `ImplDeclSyntax::trait_ref`.

/// Trait item kind discriminator (parallels StatementSyntaxKind).
/// Three item kinds are modelled:
///   - Fn         — trait method signature (`fn name(...) -> Ret;`)
///   - AssocType  — associated type declaration (`type Name: Bound = Default;`)
///   - AssocConst — associated constant declaration (`const N: T = value;`)
enum class TraitItemKind {
    Fn,         // trait method signature
    AssocType,  // associated type
    AssocConst, // associated constant (P3c.S1: RFC type-system §1.3)
};

/// A single trait item: one of
///   - a method signature (carried as the same fragments as FnDecl —
///     name, type params, params, return type, effect clause, where clause)
///   - an associated type declaration
///   - an associated constant declaration (P3c.S1)
///
/// `kind` discriminates the three shapes. Only the fields corresponding to
/// the active `kind` are populated.
struct TraitItemSyntax {
    ahfl::SourceRange range;
    TraitItemKind kind{TraitItemKind::Fn};

    // Fn signature (RFC §1.3 TraitFnItem) — body is always absent.
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    std::vector<Owned<ParamDeclSyntax>> params;
    Owned<TypeSyntax> return_type;
    Owned<EffectClauseSyntax> effect_clause;
    Owned<WhereClauseSyntax> where_clause;

    // Associated type declaration (RFC §1.3 AssocTypeItem).
    struct AssocTypeDecl {
        ahfl::SourceRange range;
        std::string name;
        std::vector<Owned<TypeParamSyntax>> type_params;
        std::vector<Owned<TypeSyntax>> bounds; // optional super-bound list
        Owned<TypeSyntax> default_type;        // optional default (`= Type`)
    };
    Owned<AssocTypeDecl> assoc_type;

    // Associated constant declaration (P3c.S1, RFC type-system §1.3 AssocConstItem).
    // `default_value` is optional — when absent, the implementing impl must
    // provide the constant.
    struct AssocConstDecl {
        ahfl::SourceRange range;
        std::string name;
        Owned<TypeSyntax> type;
        Owned<ExprSyntax> default_value; // optional `= const_expr`
    };
    Owned<AssocConstDecl> assoc_const;
};

/// Trait declaration (RFC §3.2.2 / type-system §1.3):
///
///   trait Name<T>: SuperA + SuperB { fn method(...); type Assoc; }
///
/// `super_traits` carries the optional super-trait bound list (RFC §1.3
/// `[ ":" TypeBoundList ]`). `items` carries the (optionally re-ordered) trait
/// method signatures and associated type declarations.
struct TraitDecl final : Decl {
    std::string name;
    std::vector<Owned<TypeParamSyntax>> type_params;
    std::vector<Owned<TypeSyntax>> super_traits; // optional super-trait bounds
    std::vector<Owned<TraitItemSyntax>> items;

    TraitDecl(std::string name, ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

/// Impl item kind discriminator. Parallels `TraitItemKind`; together the two
/// enums cover the six syntactic item kinds that can appear inside a trait or
/// impl body (3 trait + 3 impl = 6 kinds required by P3c.S1).
enum class ImplItemKind {
    Fn,         // method definition (body mandatory)
    AssocType,  // associated type assignment (`type Name = Type;`)
    AssocConst, // associated constant definition (`const N: T = value;`)
};

[[nodiscard]] std::string_view to_string(ImplItemKind kind) noexcept;

/// A single item inside an impl block. Three shapes share the same tagged
/// struct (mirrors TraitItemSyntax):
///   - Fn         — a function definition, points into `methods` bucket
///   - AssocType  — `type Name = Type;`, points into `assoc_items` bucket
///   - AssocConst — `const N: T = value;`, points into `const_items` bucket
///
/// Pointer semantics: the per-kind buckets (`methods` / `assoc_items` /
/// `const_items` on `ImplDecl`) are the unique owners. `ImplItemSyntax` stores
/// non-owning pointers so that the same data can be traversed either via the
/// flat per-kind lists (stable surface for legacy semantic passes) or via the
/// unified `items` vector (visit-style dispatching). No manual deallocation is
/// needed — the buckets outlive any reference through `items`.
struct ImplItemSyntax {
    ahfl::SourceRange range;
    ImplItemKind kind{ImplItemKind::Fn};

    FnDecl* fn_def{nullptr};

    struct AssocTypeDef {
        ahfl::SourceRange range;
        std::string name;
        Owned<TypeSyntax> type;
    };
    AssocTypeDef* assoc_type{nullptr};

    struct AssocConstDef {
        ahfl::SourceRange range;
        std::string name;
        Owned<TypeSyntax> type;
        Owned<ExprSyntax> value;
    };
    AssocConstDef* assoc_const{nullptr};
};

// Compatibility alias: historical consumer sites use `AssocItemDefSyntax`
// for associated-type definitions inside impl blocks. Name is preserved so
// existing semantic passes continue to compile unchanged.
using AssocItemDefSyntax = ImplItemSyntax::AssocTypeDef;

/// Impl block (RFC §3.2.2 / type-system §1.4):
///
///   impl<T> TraitRef for TargetType [where ...] { fn method(...) { ... } type A = T; }
///   impl TargetType { ... }   // inherent impl (trait_ref empty)
///
/// `trait_ref` is empty for an inherent impl (no `TraitRef "for"`). The three
/// per-item fields mirror the three `ImplItemKind` variants so that existing
/// semantic passes (P3b/P3c) that expect flat `methods`/`assoc_items` continue
/// to work without churn; P3c.S1 only adds the previously-missing
/// `const_items` bucket plus the `ImplItemSyntax` tagged struct for consumers
/// that prefer a single dispatcher.
struct ImplDecl final : Decl {
    std::vector<Owned<TypeParamSyntax>> type_params;
    Owned<TypeSyntax> trait_ref; // optional (empty for inherent impl)
    Owned<TypeSyntax> target_type;
    Owned<WhereClauseSyntax> where_clause;
    // Per-kind buckets (stable surface, semantic passes read these directly).
    std::vector<Owned<FnDecl>> methods;            // ImplItemKind::Fn
    std::vector<Owned<ImplItemSyntax::AssocTypeDef>> assoc_items; // ImplItemKind::AssocType
    std::vector<Owned<ImplItemSyntax::AssocConstDef>> const_items; // ImplItemKind::AssocConst (P3c.S1)
    // Unified dispatcher (parallel of items; populated alongside buckets so
    // consumers can iterate a single list when they prefer visit-style dispatch)
    std::vector<Owned<ImplItemSyntax>> items;

    ImplDecl(ahfl::SourceRange range = {});
    void accept(Visitor &visitor) override;
    [[nodiscard]] std::string headline() const override;
};

// ----------------------------------------------------------------------------
// Visitor pattern
// ----------------------------------------------------------------------------

/// AST visitor interface (double dispatch)
/// Every pass that traverses the AST (Resolver, TypeChecker, Emitter) implements this interface
class Visitor {
  public:
    virtual ~Visitor() = default;

    virtual void visit(Program &node) = 0;
    virtual void visit(ModuleDecl &node) = 0;
    virtual void visit(ImportDecl &node) = 0;
    virtual void visit(ConstDecl &node) = 0;
    virtual void visit(TypeAliasDecl &node) = 0;
    virtual void visit(StructDecl &node) = 0;
    virtual void visit(EnumDecl &node) = 0;
    virtual void visit(CapabilityDecl &node) = 0;
    virtual void visit(PredicateDecl &node) = 0;
    virtual void visit(AgentDecl &node) = 0;
    virtual void visit(ContractDecl &node) = 0;
    virtual void visit(FlowDecl &node) = 0;
    virtual void visit(WorkflowDecl &node) = 0;
    virtual void visit(FnDecl &node) = 0;
    virtual void visit(TraitDecl &node) = 0;
    virtual void visit(ImplDecl &node) = 0;
};

/// Recursive visitor (provides default empty implementations; subclasses only
/// override the nodes they care about)
class RecursiveVisitor : public Visitor {
  public:
    void visit(Program &node) override;
    void visit(ModuleDecl &node) override;
    void visit(ImportDecl &node) override;
    void visit(ConstDecl &node) override;
    void visit(TypeAliasDecl &node) override;
    void visit(StructDecl &node) override;
    void visit(EnumDecl &node) override;
    void visit(CapabilityDecl &node) override;
    void visit(PredicateDecl &node) override;
    void visit(AgentDecl &node) override;
    void visit(ContractDecl &node) override;
    void visit(FlowDecl &node) override;
    void visit(WorkflowDecl &node) override;
    void visit(FnDecl &node) override;
    void visit(TraitDecl &node) override;
    void visit(ImplDecl &node) override;
};

// ============================================================================
// ExprSyntax visitor
// ============================================================================
//
// `visit_expr_syntax(expr, visitor)` dispatches an ExprSyntax to the matching
// `Visitor::visit_*(expr)` overload. Dispatch is done via std::visit on the
// variant node so the compiler catches missing alternatives when new variants
// are added. Lives in ahfl::ast (public header) so any pass can opt in without
// pulling in semantics-private internals.
//
// Each visitor method receives the full `const ExprSyntax &` wrapper. Use
// `expr.as<T>()` to access the variant alternative's semantic fields.

template <typename Visitor>
decltype(auto) visit_expr_syntax(const ExprSyntax &expr, Visitor &&visitor) {
    return std::visit(
        Overloaded{
            [&](const BoolLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_bool_literal(expr);
            },
            [&](const IntegerLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_integer_literal(expr);
            },
            [&](const FloatLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_float_literal(expr);
            },
            [&](const DecimalLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_decimal_literal(expr);
            },
            [&](const StringLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_string_literal(expr);
            },
            [&](const DurationLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_duration_literal(expr);
            },
            [&](const PathExpr &) { return std::forward<Visitor>(visitor).visit_path(expr); },
            [&](const QualifiedValueExpr &) {
                return std::forward<Visitor>(visitor).visit_qualified_value(expr);
            },
            [&](const CallExpr &) { return std::forward<Visitor>(visitor).visit_call(expr); },
            [&](const MethodCallExpr &) {
                return std::forward<Visitor>(visitor).visit_unknown(expr);
            },
            [&](const StructLiteralExpr &) {
                return std::forward<Visitor>(visitor).visit_struct_literal(expr);
            },
            [&](const UnaryExpr &) { return std::forward<Visitor>(visitor).visit_unary(expr); },
            [&](const BinaryExpr &) { return std::forward<Visitor>(visitor).visit_binary(expr); },
            [&](const MemberAccessExpr &) {
                return std::forward<Visitor>(visitor).visit_member_access(expr);
            },
            [&](const IndexAccessExpr &) {
                return std::forward<Visitor>(visitor).visit_index_access(expr);
            },
            [&](const GroupExpr &) { return std::forward<Visitor>(visitor).visit_group(expr); },
            [&](const MatchExpr &) { return std::forward<Visitor>(visitor).visit_match(expr); },
            // P2 (RFC §6): lambda expressions are routed through the generic
            // `visit_unknown` fallback rather than a dedicated `visit_lambda`.
            // Not every consumer that opts into the ExprSyntax dispatcher
            // cares about closures, so forcing a per-kind hook would require
            // touching every visitor; `visit_unknown` is the shared escape
            // hatch consumers already provide for unspecialised kinds.
            [&](const LambdaExpr &) { return std::forward<Visitor>(visitor).visit_unknown(expr); },
        },
        expr.node);
}

/// Derive ExprSyntaxKind from the variant (used by downstream consumers such
/// as TypedExpr that need a kind)
[[nodiscard]] inline ExprSyntaxKind expr_syntax_kind(const ExprSyntax &expr) {
    return std::visit(
        Overloaded{
            [](const BoolLiteralExpr &) { return ExprSyntaxKind::BoolLiteral; },
            [](const IntegerLiteralExpr &) { return ExprSyntaxKind::IntegerLiteral; },
            [](const FloatLiteralExpr &) { return ExprSyntaxKind::FloatLiteral; },
            [](const DecimalLiteralExpr &) { return ExprSyntaxKind::DecimalLiteral; },
            [](const StringLiteralExpr &) { return ExprSyntaxKind::StringLiteral; },
            [](const DurationLiteralExpr &) { return ExprSyntaxKind::DurationLiteral; },
            [](const PathExpr &) { return ExprSyntaxKind::Path; },
            [](const QualifiedValueExpr &) { return ExprSyntaxKind::QualifiedValue; },
            [](const CallExpr &) { return ExprSyntaxKind::Call; },
            [](const MethodCallExpr &) { return ExprSyntaxKind::MethodCall; },
            [](const StructLiteralExpr &) { return ExprSyntaxKind::StructLiteral; },
            [](const UnaryExpr &) { return ExprSyntaxKind::Unary; },
            [](const BinaryExpr &) { return ExprSyntaxKind::Binary; },
            [](const MemberAccessExpr &) { return ExprSyntaxKind::MemberAccess; },
            [](const IndexAccessExpr &) { return ExprSyntaxKind::IndexAccess; },
            [](const GroupExpr &) { return ExprSyntaxKind::Group; },
            [](const MatchExpr &) { return ExprSyntaxKind::Match; },
            [](const LambdaExpr &) { return ExprSyntaxKind::Lambda; },
        },
        expr.node);
}

/// Derive TemporalExprSyntaxKind from the variant
[[nodiscard]] inline TemporalExprSyntaxKind
temporal_expr_syntax_kind(const TemporalExprSyntax &expr) {
    return std::visit(
        Overloaded{
            [](const EmbeddedTemporalExpr &) { return TemporalExprSyntaxKind::EmbeddedExpr; },
            [](const CalledTemporalExpr &) { return TemporalExprSyntaxKind::Called; },
            [](const InStateTemporalExpr &) { return TemporalExprSyntaxKind::InState; },
            [](const RunningTemporalExpr &) { return TemporalExprSyntaxKind::Running; },
            [](const CompletedTemporalExpr &) { return TemporalExprSyntaxKind::Completed; },
            [](const UnaryTemporalExpr &) { return TemporalExprSyntaxKind::Unary; },
            [](const BinaryTemporalExpr &) { return TemporalExprSyntaxKind::Binary; },
        },
        expr.node);
}

/// Derive PatternSyntaxKind from the variant (P1 ADT, parallels expr/temporal)
[[nodiscard]] inline PatternSyntaxKind pattern_syntax_kind(const PatternSyntax &pattern) {
    return std::visit(Overloaded{
                          [](const LiteralPattern &) { return PatternSyntaxKind::Literal; },
                          [](const VariantPattern &) { return PatternSyntaxKind::Variant; },
                          [](const WildcardPattern &) { return PatternSyntaxKind::Wildcard; },
                          [](const BindingPattern &) { return PatternSyntaxKind::Binding; },
                          [](const TuplePattern &) { return PatternSyntaxKind::Tuple; },
                          [](const OrPattern &) { return PatternSyntaxKind::Or; },
                      },
                      pattern.node);
}

} // namespace ahfl::ast
