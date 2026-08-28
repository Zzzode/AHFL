#pragma once

// ---------------------------------------------------------------------------
// Core-IR — the execution layer of the RFC 0026 IR tower (slice P3/P4, KR6.4)
// ---------------------------------------------------------------------------
//
// Core-IR (`tower::Layer::CoreIr`) is the execution-only layer produced by
// lowering `AhflIr` (the verification / orchestration layer). Per RFC 0026
// "Core-IR：执行层" it is:
//
//   * MONOMORPHIZED — no source-level generics. Concrete instances are keyed by
//     the canonical monomorphization name from `mangling.hpp`
//     (`mangle::mangle_instance`); Core-IR never re-derives that key.
//   * effect-LOWERED — capability calls become explicit capability-call nodes
//     (the `ahfl_cap` import frame of RFC 0019).
//   * ERASED of temporal / contract / decreases — verification is finished at
//     the `AhflIr` layer, so those constructs DO NOT EXIST at this layer. This
//     is the tower's key invariant: a construct only appears in the layer it
//     belongs to (RFC 0026 §"关键不变式").
//   * structured for WASM control flow, with explicit ADT / closure memory
//     layout.
//
// THIS INCREMENT (KR6.4 sub-slices so far) is a SKELETON. It defines the
// minimal node set needed to carry the simplest orchestration construct
// end-to-end — an agent's state machine — plus the explicit capability-call
// (`ahfl_cap` import) declaration that the effect->capability-call lowering
// produces, plus the scaffolded lower entry `lower_ahfl_to_core`. The rest of
// the node set (monomorphized function bodies, structured control-flow regions,
// value representation / memory layout, capability-call ARGUMENT passing) is
// filled by the later KR6.4 sub-slices and is intentionally NOT present yet.
//
// Nothing consumes `CoreProgram` yet: WASM codegen is KR6.5 and the evaluator is
// untouched. This header + `core_lower.cpp` are purely additive scaffolding
// (zero behavior change to every existing path).
//
// Design conventions (AGENTS.md):
//   * Principle 2 (index-based identity): states are addressed by `CoreStateId`
//     (an index into `CoreAgentDecl::states`); the state name string is
//     display-only. Symbol identity rides on the existing `ir::SymbolRef`.
//   * Principle 3/4 (flat stores + `std::variant`): declarations live in a flat
//     `std::vector<CoreDecl>` and the node set is a `std::variant` grown by
//     later sub-slices.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/types.hpp" // ir::SymbolRef

namespace ahfl::ir::core {

/// Core-IR interchange/format version. Distinct from the `AhflIr`
/// `kFormatVersion` so a layered IR-JSON projection (RFC 0026 P9) can tell the
/// layers apart. Bumped independently of the verification-layer version.
inline constexpr std::string_view kCoreFormatVersion = "ahfl.core.v1";

// ----------------------------------------------------------------------------
// State-machine node set (minimal orchestration skeleton)
// ----------------------------------------------------------------------------

/// Canonical identity of a state within a `CoreAgentDecl` (Principle 2): the
/// index into `CoreAgentDecl::states`. The state's spelling is display-only and
/// is stored once, in `states[value]`.
struct CoreStateId {
    std::uint32_t value{0};

    [[nodiscard]] friend bool operator==(CoreStateId, CoreStateId) noexcept = default;
};

/// A legal state transition, expressed entirely by index-based state identity.
struct CoreTransition {
    CoreStateId from{};
    CoreStateId to{};

    [[nodiscard]] friend bool operator==(const CoreTransition &,
                                         const CoreTransition &) noexcept = default;
};

/// The execution-layer projection of an `ir::AgentDecl`'s state machine.
///
/// Only the orchestration skeleton is carried: states, the initial state, the
/// final-state set, and the legal transitions. The agent's quota, contract, and
/// any temporal properties are ERASED here — they are verification-layer
/// concerns consumed at the `AhflIr` layer (RFC 0026). Handler bodies and their
/// capability-calls / control-flow regions are filled by the later KR6.4
/// sub-slices.
struct CoreAgentDecl {
    /// Display name (diagnostic only). `symbol_ref` / `instance_key` are the
    /// canonical identities.
    std::string name;
    /// Resolved nominal symbol of the originating agent (reused as-is from the
    /// verification layer; strings on it are display/diagnostic only).
    ir::SymbolRef symbol_ref;
    /// Monomorphization instance key from `mangle::mangle_instance` — the
    /// canonical execution identity of this (agent, type-args) instance. For a
    /// non-generic agent the type-arg list is empty; when full monomorphization
    /// lands (a later KR6.4 sub-slice) generic agents produce one CoreAgentDecl
    /// per concrete instantiation, each with its own key. Core-IR REUSES the
    /// mangling facility rather than reinventing the key.
    std::string instance_key;
    /// State-name table; index == `CoreStateId::value`.
    std::vector<std::string> states;
    /// Initial state.
    CoreStateId initial{};
    /// Final (accepting) states.
    std::vector<CoreStateId> finals;
    /// Legal transitions between states.
    std::vector<CoreTransition> transitions;
};

// ----------------------------------------------------------------------------
// Capability-call node set (effect -> explicit capability-call)
// ----------------------------------------------------------------------------

/// The execution-layer projection of an `ir::CapabilityDecl`.
///
/// This is where RFC 0026's "effect 降为显式 capability-call" step lands: a
/// source capability (which carried a graded `CapabilityEffectSpec`) becomes an
/// explicit capability-call declaration — the `ahfl_cap` IMPORT BOUNDARY of
/// RFC 0019 (`(ptr,len)->ptr`). It is the concrete landing point of "计算留宿主"
/// (computation stays host-side, RFC 0020): the host, in any language, binds
/// each of these declarations as an `ahfl_cap` import that WASM codegen (KR6.5)
/// will call. Core-IR itself never runs the capability — it only names the
/// import and its marshalling signature.
///
/// What is CARRIED (execution-relevant shape):
///   * `symbol_ref` — the canonical identity (Principle 2), reused verbatim from
///     the verification layer; the strings on it are display/diagnostic only.
///   * `name` — display spelling for diagnostics.
///   * `effect_kind` — the capability's effect CATEGORY (`ir::CapabilityEffectKind`,
///     reused, not redefined). The kind is the one piece of the source
///     `CapabilityEffectSpec` the execution layer keeps, so a host can classify
///     the import (e.g. read vs. financial-write) at bind time.
///   * `param_types` / `return_type_ref` — the marshalling signature (arity +
///     concrete types) the `ahfl_cap` frame needs. Source-level param NAMES are
///     display-only and are intentionally dropped here.
///
/// What is ERASED: the rest of the `CapabilityEffectSpec` (domain, idempotency
/// key, receipt/retry mode, timeout, compensation, policies) is verification /
/// orchestration metadata consumed above Core-IR; it does not exist at this
/// layer (RFC 0026 erasure invariant). Full argument-passing / value layout for
/// the call site is filled by later KR6.4 sub-slices.
struct CoreCapabilityDecl {
    /// Display name (diagnostic only). `symbol_ref` is the canonical identity.
    std::string name;
    /// Resolved nominal symbol of the originating capability (reused as-is from
    /// the verification layer; strings on it are display/diagnostic only).
    ir::SymbolRef symbol_ref;
    /// Effect category of the capability (reused `ir::CapabilityEffectKind`).
    ir::CapabilityEffectKind effect_kind{ir::CapabilityEffectKind::Unknown};
    /// Import-signature parameter types in declaration order (arity + types the
    /// `ahfl_cap` frame marshals). Param names are source-level, so they are not
    /// carried here.
    std::vector<ir::TypeRef> param_types;
    /// Import-signature return type.
    ir::TypeRef return_type_ref;
};

/// Core-IR declaration node set. Minimal by design: this skeleton represents
/// agent state machines and explicit capability-call (import) declarations.
/// Later KR6.4 sub-slices grow the variant with monomorphized function bodies
/// and the remaining orchestration constructs (flow / workflow). Kept a
/// `std::variant` (Principle 4) so those additions are additive alternatives,
/// not a class hierarchy.
using CoreDecl = std::variant<CoreAgentDecl, CoreCapabilityDecl>;

// ----------------------------------------------------------------------------
// Execution body: A-normalized (ANF) expressions, statements, regions
// ----------------------------------------------------------------------------
//
// The flow-handler execution body is lowered into A-normal form (as in Rust MIR
// / Swift SIL): every intermediate result is a named `CoreValueId`, and every
// EFFECT (a capability call) is a top-level ordered STATEMENT, never a nested
// sub-expression. This makes eval order, data dependency, capability
// pending/suspend, resume checkpoints, and "a completed inner call is never
// replayed" STRUCTURAL FACTS rather than traversal conventions.
//
//   ctx.ticket_id = Option::Some(TicketCreate(id, reason));
// lowers to (schematically):
//   %t0 = capability_call TicketCreate(%id, %reason)   // CoreStmt::CapabilityCall
//   %t1 = construct Option::Some(%t0)                  // CoreStmt::Let (pure)
//   store ctx.ticket_id <- %t1                         // CoreStmt::Store
//
// Principle 2 (index identity): values are `CoreValueId`, capabilities
// `CoreCapabilityId`, states `CoreStateId`, expressions `CoreExprId` — never
// strings. Principle 3 (flat stores): expressions live in a per-flow arena.

/// SSA-like value produced by a pure `CoreExpr` or a capability call result.
struct CoreValueId {
    std::uint32_t value{0};
    [[nodiscard]] friend bool operator==(CoreValueId, CoreValueId) noexcept = default;
};

/// Index into a flow's pure-expression arena (`CoreFlowDecl::exprs`).
struct CoreExprId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreExprId, CoreExprId) noexcept = default;
};

/// Index into the program's capability table (`CoreProgram::capabilities`).
struct CoreCapabilityId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreCapabilityId, CoreCapabilityId) noexcept = default;
};

/// Root of a path read, mirroring `ir::PathRootKind` structurally (Principle 2:
/// we keep the kind, we do NOT flatten the path to a dotted string).
enum class CorePathRoot { Input, Context, Local, Identifier };

// --- pure expressions (NO effects; a capability call is never a CoreExpr) ---

/// Literal categories carried structurally (physical i32/i64/decimal encoding is
/// deferred to P4; here we keep the kind + spelling so nothing is lost).
enum class CoreLiteralKind { Bool, Integer, Float, Decimal, String, Duration, Unit };

struct CoreLiteralExpr {
    CoreLiteralKind kind{CoreLiteralKind::Unit};
    std::string spelling; // original spelling (empty for Unit)
    [[nodiscard]] friend bool operator==(const CoreLiteralExpr &,
                                         const CoreLiteralExpr &) noexcept = default;
};

/// Reference to a previously-bound value (SSA use).
struct CoreValueRefExpr {
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CoreValueRefExpr &,
                                         const CoreValueRefExpr &) noexcept = default;
};

/// A path read (`input.x`, `ctx.y`, a local, or a free identifier). The member
/// chain is kept structured; a `Local` root additionally resolves to the
/// binding's `CoreValueId` when the reader is an in-scope let-binding.
struct CorePathExpr {
    CorePathRoot root{CorePathRoot::Identifier};
    std::string root_name;             // display / identifier root
    std::vector<std::string> members;  // structured member chain (not flattened)
    CoreValueId local{};               // valid iff root == Local (resolved binding)
    bool has_local{false};
    [[nodiscard]] friend bool operator==(const CorePathExpr &,
                                         const CorePathExpr &) noexcept = default;
};

/// A qualified value (e.g. an enum variant with no payload: `Priority::High`).
struct CoreQualifiedExpr {
    std::string name;
    [[nodiscard]] friend bool operator==(const CoreQualifiedExpr &,
                                         const CoreQualifiedExpr &) noexcept = default;
};

enum class CoreUnaryOp { Not, Neg };
struct CoreUnaryExpr {
    CoreUnaryOp op{CoreUnaryOp::Not};
    CoreExprId operand{};
    [[nodiscard]] friend bool operator==(const CoreUnaryExpr &,
                                         const CoreUnaryExpr &) noexcept = default;
};

/// Binary op tag mirrors the AHFL-IR set structurally (kept as-is; physical
/// semantics are unchanged, only re-tagged at the execution layer).
enum class CoreBinaryOp {
    Add, Sub, Mul, Div, Mod,
    Eq, Ne, Lt, Le, Gt, Ge,
    And, Or,
};
struct CoreBinaryExpr {
    CoreBinaryOp op{CoreBinaryOp::Add};
    CoreExprId lhs{};
    CoreExprId rhs{};
    [[nodiscard]] friend bool operator==(const CoreBinaryExpr &,
                                         const CoreBinaryExpr &) noexcept = default;
};

/// A PURE constructor / aggregate (struct literal, enum variant with payload,
/// e.g. `Option::Some(%t0)`). Its operands are already-bound value ids, so a
/// capability call nested in a source constructor has been hoisted OUT to a
/// preceding `CoreStmt::CapabilityCall` before this node is built. For an enum
/// variant, `variant` carries the typed declaration-order index (Principle 2);
/// `type_name`/`variant_name` are display-only.
struct CoreConstructExpr {
    std::string type_name;                 // constructed nominal / enum name (display)
    std::string variant_name;              // non-empty for an enum variant (display)
    bool is_enum_variant{false};
    std::uint32_t variant{0};              // typed variant index (iff is_enum_variant)
    bool variant_resolved{false};          // false => index not resolved (see diagnostics)
    std::vector<CoreValueId> args;         // payload / field values (ANF operands)
    [[nodiscard]] friend bool operator==(const CoreConstructExpr &,
                                         const CoreConstructExpr &) noexcept = default;
};

/// A structurally-preserved but not-yet-lowered PURE expression (e.g. match,
/// lambda, index/member access forms deferred to a later sub-slice). It carries
/// the source expr kind + range so the Core-IR verifier can reject it if a
/// backend reaches it, WITHOUT it ever hiding an effect (effectful unsupported
/// shapes fail-closed in the lowerer and never reach here). This is NOT a
/// lossy string blob: the kind is enumerated and the range is preserved.
struct CoreUnsupportedExpr {
    std::string source_kind; // e.g. "MatchExpr", "LambdaExpr"
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CoreUnsupportedExpr &,
                                         const CoreUnsupportedExpr &) noexcept = default;
};

using CoreExprNode = std::variant<CoreLiteralExpr,
                                  CoreValueRefExpr,
                                  CorePathExpr,
                                  CoreQualifiedExpr,
                                  CoreUnaryExpr,
                                  CoreBinaryExpr,
                                  CoreConstructExpr,
                                  CoreUnsupportedExpr>;

struct CoreExpr {
    CoreExprNode node;
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CoreExpr &, const CoreExpr &) noexcept = default;
};

// --- assignment target (a place) ---

/// A storable place (`ctx.field`, a local, …). Structured, not a dotted string.
struct CorePlace {
    CorePathRoot root{CorePathRoot::Context};
    std::string root_name;
    std::vector<std::string> members;
    [[nodiscard]] friend bool operator==(const CorePlace &, const CorePlace &) noexcept = default;
};

// --- statements (ANF; effects are ordered statements) ---

struct CoreRegion; // forward decl (owns statements)

/// Bind a pure expression's result to a value id.
struct CoreLetStmt {
    CoreValueId result{};
    CoreExprId expr{};
    [[nodiscard]] friend bool operator==(const CoreLetStmt &, const CoreLetStmt &) noexcept = default;
};

/// The EFFECT node: an ordered capability invocation. Args are already-bound
/// value ids (ANF). `result` names the value id the call produces (a resume
/// checkpoint: on replay a completed call's result is reused, not re-invoked).
struct CoreCapabilityCallStmt {
    CoreValueId result{};
    CoreCapabilityId capability{};
    std::string callee_name;         // display only; `capability` is identity
    std::vector<CoreValueId> args;   // ANF operands, in left-to-right eval order
    [[nodiscard]] friend bool operator==(const CoreCapabilityCallStmt &,
                                         const CoreCapabilityCallStmt &) noexcept = default;
};

/// Store a value id into a place.
struct CoreStoreStmt {
    CorePlace place;
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CoreStoreStmt &, const CoreStoreStmt &) noexcept = default;
};

/// Structured conditional — branch mutual exclusion is preserved (NOT flattened
/// into a single statement list). `condition` is an already-bound Bool value id.
struct CoreIfStmt {
    CoreValueId condition{};
    std::unique_ptr<CoreRegion> then_region;
    std::unique_ptr<CoreRegion> else_region; // may be null (no else)
    friend bool operator==(const CoreIfStmt &, const CoreIfStmt &) noexcept;
};

/// State jump — the actual next-state decision from the handler (distinct from
/// the agent's legal-edge set). Target is index identity into the agent states.
struct CoreGotoStmt {
    CoreStateId target{};
    std::string target_name; // display only
    [[nodiscard]] friend bool operator==(const CoreGotoStmt &, const CoreGotoStmt &) noexcept = default;
};

/// Return from the handler; optional value id.
struct CoreReturnStmt {
    bool has_value{false};
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CoreReturnStmt &, const CoreReturnStmt &) noexcept = default;
};

using CoreStmtNode = std::variant<CoreLetStmt,
                                  CoreCapabilityCallStmt,
                                  CoreStoreStmt,
                                  CoreIfStmt,
                                  CoreGotoStmt,
                                  CoreReturnStmt>;

struct CoreStmt {
    CoreStmtNode node;
    SourceRangeOpt source_range;
    friend bool operator==(const CoreStmt &, const CoreStmt &) noexcept;
};

/// An ordered statement region (a block). Statement order == effect order.
struct CoreRegion {
    std::vector<CoreStmt> statements;
    friend bool operator==(const CoreRegion &, const CoreRegion &) noexcept;
};

// --- flow (state handlers with executable bodies) ---

/// Execution policy carried onto a state handler (retry / retry_on / timeout).
/// Kept structurally so the executor honours it; NOT a verification concern.
struct CoreStatePolicy {
    std::optional<std::string> retry_limit;      // "3"
    std::vector<std::string> retry_on;           // error type names
    std::optional<std::string> timeout;          // "30s"
    [[nodiscard]] friend bool operator==(const CoreStatePolicy &,
                                         const CoreStatePolicy &) noexcept = default;
};

/// One flow state handler with its executable body.
struct CoreFlowState {
    CoreStateId state{};       // index identity into the target agent's states
    std::string state_name;    // display only
    CoreStatePolicy policy;
    CoreRegion body;
    friend bool operator==(const CoreFlowState &, const CoreFlowState &) noexcept;
};

/// The execution-layer projection of an `ir::FlowDecl`. Owns the per-flow pure
/// expression arena (`exprs`, addressed by `CoreExprId`) and the value counter.
struct CoreFlowDecl {
    std::string agent_name;             // display only
    ir::SymbolRef target_ref;           // resolved target agent identity
    std::vector<CoreExpr> exprs;        // pure-expression arena (Principle 3)
    std::uint32_t value_count{0};       // number of CoreValueIds allocated
    std::vector<CoreFlowState> states;
    friend bool operator==(const CoreFlowDecl &, const CoreFlowDecl &) noexcept;
};

// ----------------------------------------------------------------------------
// Core-IR program
// ----------------------------------------------------------------------------

/// A complete Core-IR compilation unit — the execution layer's program.
///
/// Dedicated flat stores (Principle 3) replace the earlier `variant`-of-decls:
/// capabilities are addressed by `CoreCapabilityId` (index into `capabilities`),
/// agents and flows keep source order for determinism.
struct CoreProgram {
    std::string format_version{std::string(kCoreFormatVersion)};
    std::vector<CoreCapabilityDecl> capabilities; // index == CoreCapabilityId
    std::vector<CoreAgentDecl> agents;
    std::vector<CoreFlowDecl> flows;
};

// ----------------------------------------------------------------------------
// Lower entry: AhflIr -> Core-IR
// ----------------------------------------------------------------------------

/// Severity of a lowering diagnostic. Only `Error` makes the program
/// non-executable; `Warning` is retained but does not gate backend consumption.
enum class CoreDiagnosticSeverity { Error, Warning };

/// A structured lowering diagnostic (fail-closed: no throw, no Unknown node).
struct CoreLowerDiagnostic {
    CoreDiagnosticSeverity severity{CoreDiagnosticSeverity::Error};
    std::string code;          // stable code, e.g. "core.UNRESOLVED_CAPABILITY_CALL"
    std::string message;       // human-readable, actionable (Principle 5)
    SourceRangeOpt source_range;
};

/// Result of lowering: the program plus any structured diagnostics. Only ERROR
/// diagnostics make the program non-executable; warnings are retained and the
/// program stays backend-consumable. When not executable the program is a
/// PARTIAL artifact for diagnostics/tests only — never a BackendReady input.
struct CoreLowerResult {
    CoreProgram program;
    std::vector<CoreLowerDiagnostic> diagnostics;
    bool is_executable{true};

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    /// A program is consumable iff it has no ERROR diagnostics.
    /// Invariant: ok() == !has_errors() == is_executable.
    [[nodiscard]] bool ok() const noexcept { return !has_errors(); }
};

/// Lower the verification / orchestration layer (`AhflIr`) to the execution
/// layer (`CoreProgram`), in A-normal form.
///
/// SCOPE (this sub-slice): lowers each `ir::AgentDecl` state machine, each
/// `ir::CapabilityDecl` import, and each `ir::FlowDecl` handler body. Handler
/// bodies are A-normalized: pure computation lands in the per-flow `CoreExpr`
/// arena and every capability invocation is an ordered `CoreCapabilityCallStmt`
/// (nested source calls are recursively hoisted so an inner call's result feeds
/// the outer pure constructor). `if`/`goto`/`return`/`let`/assign are lowered
/// with branch mutual-exclusion preserved (`CoreIfStmt` regions). Unresolved
/// capability callees and effectful-unsupported shapes fail closed with a
/// diagnostic (never a silent drop or an Unknown node).
///
/// Deferred to later KR6.4 sub-slices: match/try/loop regions, workflow
/// lowering, monomorphization, and physical value representation / memory
/// layout (P4). A pure unsupported shape becomes a `CoreUnsupportedExpr`
/// (enumerated kind + range) the verifier can reject; it never hides an effect.
[[nodiscard]] CoreLowerResult lower_ahfl_to_core(const AhflIr &ahfl_ir);

} // namespace ahfl::ir::core
