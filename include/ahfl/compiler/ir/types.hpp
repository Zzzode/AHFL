#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"

namespace ahfl::ir {

// ============================================================================
// AHFL Intermediate Representation (IR) definitions
// ============================================================================
//
// This file defines the intermediate representation of the AHFL compiler. The IR
// is produced by the Resolver -> TypeChecker -> IR Lowering pipeline and serves
// as the input to subsequent Emitters (SMV/Native/other backends).
//
// Key differences between IR and AST:
//   1. Uses std::variant to implement a tagged union (unlike the AST's tagged struct)
//   2. Types and cross-declaration symbols use TypeRef/SymbolRef as the sole
//      structured source of truth
//   3. Declarations and major executable nodes carry a structured source range
//      (origin tracking)
//   4. Inferred analysis information is centralized in Program::analyses
//
// IR node hierarchy:
//   Program
//   ├── declarations: vector<Decl>
//   │   ├── ModuleDecl      — module declaration
//   │   ├── ImportDecl      — import declaration
//   │   ├── ConstDecl       — constant declaration
//   │   ├── TypeAliasDecl   — type alias
//   │   ├── StructDecl      — struct definition
//   │   ├── EnumDecl        — enum definition
//   │   ├── CapabilityDecl  — capability declaration
//   │   ├── PredicateDecl   — predicate declaration
//   │   ├── AgentDecl       — agent definition
//   │   ├── ContractDecl    — contract declaration
//   │   ├── FlowDecl        — flow definition
//   │   └── WorkflowDecl    — workflow definition
//   └── analyses: AnalysisBundle

inline constexpr std::string_view kFormatVersion = "ahfl.ir.v1";

// ----------------------------------------------------------------------------
// Enum type definitions
// ----------------------------------------------------------------------------

/// Root node kind of a path expression
enum class PathRootKind {
    Identifier, // Plain identifier (variable name, node output reference, etc.)
    Input,      // Keyword `input`
    Context,    // Keyword/convention root `ctx`
    Output,     // Keyword `output`
    State,      // Convention root `state`
    Local,      // Flow-local let binding
};

/// Expression unary operators
enum class ExprUnaryOp {
    Not,      // Logical NOT !
    Negate,   // Negation -
    Positive, // Unary plus +
};

/// Expression binary operators
enum class ExprBinaryOp {
    Implies,      // => implies
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

/// Temporal logic unary operators
enum class TemporalUnaryOp {
    Always,     // □ (always)
    Eventually, // ◇ (eventually)
    Next,       // ○ (next)
    Not,        // ¬ (not)
};

/// Temporal logic binary operators
enum class TemporalBinaryOp {
    Implies, // =>
    Or,      // ||
    And,     // &&
    Until,   // U (until)
};

/// Contract clause kinds
enum class ContractClauseKind {
    Requires,  // Precondition
    Ensures,   // Postcondition
    Invariant, // Invariant
    Forbid,    // Forbidden condition
    Decreases, // Termination metric
};

/// Source kinds of value references inside a workflow
enum class WorkflowValueSourceKind {
    WorkflowInput,      // Reference to the workflow's input
    WorkflowNodeOutput, // Reference to a node's output
};

/// Symbol reference kinds
enum class SymbolRefKind {
    Unknown,
    Type,
    Const,
    Capability,
    Predicate,
    Agent,
    Workflow,
    // P2c (RFC §3.2.2): a top-level `fn` symbol reference. Lets the lowering
    // and verify paths treat a fn symbol uniformly instead of collapsing to
    // Unknown.
    Function,
};

/// Type reference kinds
enum class TypeRefKind {
    Unresolved,
    Any,
    Never,
    Unit,
    Bool,
    Int,
    Float,
    String,
    BoundedString,
    UUID,
    Timestamp,
    Duration,
    Decimal,
    Struct,
    Enum,
    Optional,
    List,
    Set,
    Map,
    Fn,
};

/// Resolved symbol reference. canonical_name is the stable backend identity;
/// local/module are used for diagnostics and display.
struct SymbolRef {
    SymbolRefKind kind{SymbolRefKind::Unknown};
    std::string canonical_name{};
    std::string local_name{};
    std::string module_name{};
    std::optional<std::size_t> id{}; // Numeric symbol ID for O(1) cross-declaration lookup
};

/// Resolved or structured type reference.
struct TypeRef;
using TypeRefPtr = Owned<TypeRef>;

struct TypeRef {
    TypeRefKind kind{TypeRefKind::Unresolved};
    std::string display_name{};
    std::string canonical_name{};
    std::string variant_name{};
    std::optional<std::pair<std::int64_t, std::int64_t>> string_bounds{};
    std::optional<std::int64_t> decimal_scale{};
    std::optional<SourceRange> source_range{};
    TypeRefPtr first{};
    TypeRefPtr second{};
    std::vector<TypeRefPtr> params{};
};

} // namespace ahfl::ir
