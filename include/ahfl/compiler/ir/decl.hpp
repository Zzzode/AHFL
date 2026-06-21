#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir {

// ----------------------------------------------------------------------------
// Declaration Provenance
// ----------------------------------------------------------------------------

/// Declaration provenance info (used for diagnostics and debugging)
struct DeclarationProvenance {
    std::string module_name; // Owning module name
    std::string source_path; // Source file path
    SourceRangeOpt source_range;
    std::uint32_t id{0}; // Monotonic declaration ID assigned during lowering
};

// ----------------------------------------------------------------------------
// Top-Level Declarations
// ----------------------------------------------------------------------------

/// Module declaration: module a::b::c;
struct ModuleDecl {
    DeclarationProvenance provenance;
    std::string name;
};

/// Import declaration: import a::b::c [as alias];
struct ImportDecl {
    DeclarationProvenance provenance;
    std::string path;
    std::optional<std::string> alias;
};

/// Constant declaration: const NAME: Type = value;
struct ConstDecl {
    DeclarationProvenance provenance;
    std::string name;
    ExprRef value;
    TypeRef type_ref;
    SymbolRef symbol_ref;
};

/// Type alias: type NewName = ExistingType;
struct TypeAliasDecl {
    DeclarationProvenance provenance;
    std::string name;
    TypeRef aliased_type_ref;
    SymbolRef symbol_ref;
};

/// Struct field declaration
struct FieldDecl {
    std::string name;
    ExprRef default_value; // Optional default value
    TypeRef type_ref;
    SourceRangeOpt source_range;
};

/// Struct declaration: struct Name { field1: Type1; ... }
struct StructDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<FieldDecl> fields;
    SymbolRef symbol_ref;
};

/// Enum declaration: enum Name { Variant1; Variant2; }
struct EnumDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<std::string> variants;
    SymbolRef symbol_ref;
};

/// Parameter declaration
struct ParamDecl {
    std::string name;
    TypeRef type_ref;
    SourceRangeOpt source_range;
};

/// Capability side-effect category. The IR keeps a structured value; the backend maps it to a target artifact enum.
enum class CapabilityEffectKind {
    Unknown,
    Read,
    ExternalSideEffect,
    DurableWrite,
    FinancialWrite,
};

/// Capability execution receipt requirement
enum class CapabilityReceiptMode {
    None,
    Optional,
    Required,
};

/// Capability retry-safety declaration
enum class CapabilityRetryMode {
    Unsafe,
    SafeIfIdempotent,
    Safe,
};

/// Input facts for capability effect analysis, explicitly declared by the DSL and propagated with the IR.
struct CapabilityEffectSpec {
    bool declared{false};
    CapabilityEffectKind kind{CapabilityEffectKind::Unknown};
    std::optional<std::string> domain;
    std::optional<std::string> idempotency_key;
    CapabilityReceiptMode receipt_mode{CapabilityReceiptMode::None};
    CapabilityRetryMode retry_mode{CapabilityRetryMode::Unsafe};
    std::optional<std::string> timeout;
    std::optional<std::string> compensation;
    std::vector<std::string> policies;
    SourceRangeOpt source_range;
};

/// Capability declaration: capability Name(param1: Type1, ...) -> ReturnType;
struct CapabilityDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    TypeRef return_type_ref;
    CapabilityEffectSpec effect;
    SymbolRef symbol_ref;
};

/// Predicate declaration: predicate Name(param1: Type1, ...);
struct PredicateDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    SymbolRef symbol_ref;
};

/// Quota item
struct QuotaItem {
    std::string name;  // e.g. "max_tool_calls"
    std::string value; // e.g. "10"
};

/// State transition declaration
struct TransitionDecl {
    std::string from_state;
    std::string to_state;
};

/// Agent declaration — the core entity of AHFL
struct AgentDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<std::string> states;         // State set
    std::string initial_state;               // Initial state
    std::vector<std::string> final_states;   // Final state set
    std::vector<QuotaItem> quota;            // Resource quota
    std::vector<TransitionDecl> transitions; // Legal state transitions
    TypeRef input_type_ref;
    TypeRef context_type_ref;
    TypeRef output_type_ref;
    std::vector<SymbolRef> capability_refs;
    SymbolRef symbol_ref;
};

/// Contract clause
struct ContractClause {
    ContractClauseKind kind{ContractClauseKind::Requires};
    std::variant<ExprRef, TemporalExprPtr> value; // Plain expression or temporal-logic expression
    SourceRangeOpt source_range;
};

/// Contract declaration: contract for AgentName { requires ...; ensures ...; }
struct ContractDecl {
    DeclarationProvenance provenance;
    std::vector<ContractClause> clauses; // List of contract clauses
    SymbolRef target_ref;
};

// ----------------------------------------------------------------------------
// Flow-Related Structures
// ----------------------------------------------------------------------------

/// Retry policy: retry: 3;
struct RetryPolicy {
    std::string limit;
};

/// Error-typed retry policy: retry_on: [Error1, Error2];
struct RetryOnPolicy {
    std::vector<std::string> targets;
};

/// Timeout policy: timeout: 30s;
struct TimeoutPolicy {
    std::string duration;
};

/// State policy item (variant)
using StatePolicyItem = std::variant<RetryPolicy, RetryOnPolicy, TimeoutPolicy>;

/// State handler: state StateName [policy {...}] { statements... }
struct StateHandler {
    /// State handler summary (computed during IR lowering)
    struct Summary {
        std::vector<std::string> goto_targets;   // Target states it may jump to
        bool may_return{false};                  // Whether it may execute a return
        bool may_fallthrough{true};              // Whether it may fall through to the end
        std::vector<Path> assigned_paths;        // Paths that get assigned
        std::vector<std::string> called_targets; // Capabilities called
        std::size_t assert_count{0};             // Number of assert statements
    };

    std::string state_name;              // Which state is handled
    std::vector<StatePolicyItem> policy; // Execution policy
    Block body;                          // Handling logic (statement block)
    SourceRangeOpt source_range;
};

/// Flow declaration: flow for AgentName { state Init { ... } ... }
struct FlowDecl {
    DeclarationProvenance provenance;
    std::vector<StateHandler> state_handlers; // Handler for each state
    SymbolRef target_ref;
};

// ----------------------------------------------------------------------------
// Workflow-Related Structures
// ----------------------------------------------------------------------------

/// Source of value references within a workflow
struct WorkflowValueRead {
    WorkflowValueSourceKind kind{WorkflowValueSourceKind::WorkflowInput};
    std::string root_name;            // Input or node name
    std::vector<std::string> members; // Member access chain
};

/// Workflow expression summary (analyzes the value sources it depends on)
struct WorkflowExprSummary {
    std::vector<WorkflowValueRead> reads; // All value references
};

/// Workflow node: node name: AgentType(input_expr) [after [dep1, dep2]];
struct WorkflowNode {
    std::string name;               // Node name
    ExprRef input;                  // Input expression passed to the agent
    std::vector<std::string> after; // DAG dependencies (predecessor node names)
    SymbolRef target_ref;
    SourceRangeOpt source_range;
};

/// Workflow declaration — multi-agent DAG orchestration
struct WorkflowDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<WorkflowNode> nodes;       // DAG node list
    std::vector<TemporalExprPtr> safety;   // Safety properties
    std::vector<TemporalExprPtr> liveness; // Liveness properties
    ExprRef return_value;                  // Final return value expression
    TypeRef input_type_ref;
    TypeRef output_type_ref;
    SymbolRef symbol_ref;
};

// ----------------------------------------------------------------------------
// P2 (RFC §3.2.2 / §3.2.3 / §6): first-class fn declaration
// ----------------------------------------------------------------------------
//
// The IR representation of a top-level `fn`. The IR keeps the resolved
// structural signature (params, return type, effect clause) plus the declared
// generic type-parameter names so a downstream monomorphization consumer can
// read the instantiation surface without re-entering the typed tree.
//
// The body is intentionally NOT lowered into the IR here: a `fn` body is a
// statement block whose lowering machinery (typed-statement → IR statement
// translation) is part of the typed-tree → IR body lowering that follows the
// existing capability/agent handler shape. P2c wires the declaration surface
// (signature + symbol + provenance + generics + effect); body lowering rides
// on the same typed-block infrastructure once a typed fn body is indexed into
// the typed program (currently the body is type-checked structurally but not
// stored as a TypedBlock, matching the P2b scope).
/// Effect clause kind on a fn, mirroring ast::EffectClauseKind (0=Pure,
/// 1=Nondet, 2=Capability) without pulling the AST into the IR layer.
enum class FnEffectKind : std::int32_t {
    Pure = 0,
    Nondet = 1,
    Capability = 2,
};

struct FnEffectClause {
    FnEffectKind kind{FnEffectKind::Pure};
    // Resolved symbol ids of the capabilities referenced by a Capability
    // clause. Empty for Pure / Nondet.
    std::vector<SymbolRef> capabilities;
    SourceRangeOpt source_range;
};

struct FnDecl {
    DeclarationProvenance provenance;
    std::string name;
    std::vector<ParamDecl> params;
    TypeRef return_type_ref; // Optional: unit-returning fns leave this empty
    bool has_return_type{false};
    FnEffectClause effect;
    // Generic type-parameter names in declaration order. Substituted by the
    // monomorphization pass (RFC §5) at each call site; empty for a
    // non-generic fn.
    std::vector<std::string> type_param_names;
    bool has_body{false};
    SymbolRef symbol_ref;
};

// ----------------------------------------------------------------------------
// Formal Observations
// ----------------------------------------------------------------------------

/// Scope kind of a formal observation
enum class FormalObservationScopeKind {
    ContractClause,         // Contract clause
    WorkflowSafetyClause,   // Workflow safety property
    WorkflowLivenessClause, // Workflow liveness property
};

/// Scope of a formal observation
struct FormalObservationScope {
    FormalObservationScopeKind kind{FormalObservationScopeKind::ContractClause};
    std::string owner;           // Owning Agent or Workflow
    std::size_t clause_index{0}; // Clause index
    std::size_t atom_index{0};   // Atom index
};

/// called(capability) observation
struct CalledCapabilityObservation {
    std::string agent;
    std::string capability;
};

/// Embedded boolean-expression observation
struct EmbeddedBoolObservation {
    FormalObservationScope scope;
};

/// Formal observation node
using FormalObservationNode = std::variant<CalledCapabilityObservation, EmbeddedBoolObservation>;

/// Formal observation (used for SMV model generation)
struct FormalObservation {
    std::string symbol;         // Observation symbol
    FormalObservationNode node; // Observation content
};

} // namespace ahfl::ir
