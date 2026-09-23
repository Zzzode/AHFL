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
// The completed KR6.4 slices provide structured flow/workflow regions,
// monomorphized value types, coercions, member templates, and the P4-D layout
// projection. Target-specific physical layout deliberately is not a CoreProgram
// field: P4-D projects it into the pure side artifact declared by
// core_layout.hpp. KR6.5 E1 consumes the verified orchestration subset; E2 adds
// capability execution incrementally. Core retains the materialized capability
// signature and agent authorization facts needed for that consumer to be
// standalone and fail-closed.
//
// Design conventions (AGENTS.md):
//   * Principle 2 (index-based identity): states are addressed by `CoreStateId`
//     (an index into `CoreAgentDecl::states`); the state name string is
//     display-only. Symbol identity rides on the existing `ir::SymbolRef`.
//   * Principle 3/4 (flat stores + `std::variant`): declarations live in a flat
//     `std::vector<CoreDecl>` and the node set is a `std::variant` grown by
//     later sub-slices.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/types.hpp" // ir::SymbolRef

namespace ahfl::ir::core {

/// Core-IR interchange/format version. Distinct from the `AhflIr`
/// `kFormatVersion` so a layered IR-JSON projection (RFC 0026 P9) can tell the
/// layers apart. Bumped independently of the verification-layer version.
inline constexpr std::string_view kCoreFormatVersion = "ahfl.core.v1";

// ----------------------------------------------------------------------------
// Typed identities (Principle 2: index-based, never strings). Defined up front
// because agent / type / projection decls below reference them.
// ----------------------------------------------------------------------------

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

/// Nominal type identity (Principle 2): index into `CoreProgram::types`. A
/// variant index is only meaningful together with the enum's `CoreTypeId`, so
/// `Option::Some#0` and `Result::Ok#0` are distinguishable by type id, not by a
/// display string.
struct CoreTypeId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreTypeId, CoreTypeId) noexcept = default;
};

/// Index into the program's agent table (`CoreProgram::agents`).
struct CoreAgentId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreAgentId, CoreAgentId) noexcept = default;
};

/// Declaration-order index of a field WITHIN its owning struct `CoreTypeId`.
/// A distinct type so a field index cannot be confused with a variant index or
/// a raw integer (Principle 2: no bare uint32 identity).
struct CoreFieldId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreFieldId, CoreFieldId) noexcept = default;
};

/// Declaration-order index of a variant WITHIN its owning enum `CoreTypeId`.
struct CoreVariantId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreVariantId, CoreVariantId) noexcept = default;
};

/// Index into a flow's pattern arena (`CoreFlowDecl::patterns`). A match arm's
/// pattern tree lives flat (Principle 3): a variant/or/binding pattern refers to
/// its children by `CorePatternId`, never by owning pointer.
struct CorePatternId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CorePatternId, CorePatternId) noexcept = default;
};

/// Index into a match arm's binding list (`CoreMatchArm::bindings`). A binding
/// pattern refers to the arm binding it names by this index, so an or-pattern's
/// alternatives (`A(x) | B(x)`) share ONE arm binding rather than each defining
/// its own (which would violate flow-global SSA single-definition).
struct CorePatternBindingId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CorePatternBindingId,
                                         CorePatternBindingId) noexcept = default;
};

/// Canonical identity of a workflow within `CoreProgram::workflows` (Principle 2):
/// the index into that flat store. The workflow's name is display-only.
struct CoreWorkflowId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreWorkflowId, CoreWorkflowId) noexcept = default;
};

/// Canonical identity of a node within a `CoreWorkflowDecl` (Principle 2): the
/// index into `CoreWorkflowDecl::nodes`. Dependency edges (`after`) and node-
/// output value roots refer to a node by this id, NEVER by its source name.
struct CoreWorkflowNodeId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreWorkflowNodeId, CoreWorkflowNodeId) noexcept = default;
};

/// Canonical identity of a monomorphized fn BODY within `CoreProgram::fns`
/// (CORE-FNBODY-DESIGN §1/§2.1, Principle 2): the index into that flat
/// per-program store. The fn table is 1:1 with the Fn-kind instance registry
/// entries that own a body (codegen unit vs monomorphization registration);
/// `CoreFnInstance::body` is the reverse link. The mangled instance key stays
/// a dispatch label, never the identity.
struct CoreFnId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreFnId, CoreFnId) noexcept = default;
};

/// Canonical identity of a monomorphized INSTANCE within `CoreProgram::instances`
/// (Principle 2): the index into that flat store. A workflow node's invocation
/// target and (later) a fn call resolve to a `CoreInstanceId`, never a mangled
/// name string. The mangled `instance_key` on the decl is an execution-dispatch
/// label + global-uniqueness key, NOT the in-IR identity.
struct CoreInstanceId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreInstanceId, CoreInstanceId) noexcept = default;
};

/// Canonical identity of a LOGICAL value type within `CoreProgram::value_types`
/// (RFC 0026 P4, Principle 2/3): the index into that program-global, hash-consed
/// arena. Because the arena is interned, index equality IS structural equality —
/// but ONLY within the SAME owning arena (`CoreProgram::value_types`). A
/// `CoreValueTypeId` from one program is meaningless against another program's
/// arena; comparisons that carry these ids (e.g. `CoreInstanceDecl::operator==`)
/// are therefore SAME-OWNER-ARENA equality. A future cross-program semantic diff
/// must go through a dedicated arena-aware comparator, never a bare `operator==`.
/// Target-INDEPENDENT: physical layout is a separate P4-D projection keyed by
/// this id, never stored in the value type itself.
struct CoreValueTypeId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreValueTypeId, CoreValueTypeId) noexcept = default;
};

/// Index into a nominal declaration's member-type template arena. The arena is
/// declaration-owned and postordered: every child id is strictly smaller than
/// its parent id. These templates describe logical member types before an
/// owning nominal's type arguments are known; physical layout remains P4-D.
struct CoreMemberTypeTemplateNodeId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreMemberTypeTemplateNodeId,
                                         CoreMemberTypeTemplateNodeId) noexcept = default;
};

/// Index into a body's normalized coercion-plan arena. A plan id is meaningful
/// only within its owning CoreFlowDecl/CoreWorkflowDecl.
struct CoreCoercionPlanId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreCoercionPlanId, CoreCoercionPlanId) noexcept = default;
};

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
    /// Display name (diagnostic only). `symbol_ref` is the canonical identity.
    std::string name;
    /// Resolved nominal symbol of the originating agent (reused as-is from the
    /// verification layer; strings on it are display/diagnostic only).
    ir::SymbolRef symbol_ref;
    /// State-name table; index == `CoreStateId::value`.
    std::vector<std::string> states;
    /// Initial state.
    CoreStateId initial{};
    /// Final (accepting) states.
    std::vector<CoreStateId> finals;
    /// Legal transitions between states.
    std::vector<CoreTransition> transitions;
    /// Typed identities of the agent's input / context / output nominal types
    /// (Principle 2). These make a path projection self-contained: a root of
    /// `input`/`ctx` resolves to `input_type`/`context_type` here, so a backend
    /// never re-queries AHFL-IR. Sema's schema boundary REQUIRES input/output to
    /// be Struct types; context is either an explicit Struct or the default Unit
    /// context (a stateless agent). `context_kind` records which — a `Unit`
    /// context has `context_type == kInvalid` (no struct to project through); a
    /// `Struct` context has a valid Struct `context_type`. Both an omitted
    /// `context` and an explicit `context: Unit;` lower to Unit. A `Struct`
    /// context whose id fails to resolve (or an enum/primitive context, which
    /// Sema forbids) is recorded as `Struct` with a broken id so the verifier
    /// rejects it — a non-struct is NEVER silently folded into Unit. A required
    /// shell left `kInvalid`, or pointing at a non-struct, is a verifier error.
    CoreTypeId input_type{};
    CoreTypeId context_type{};
    CoreTypeId output_type{};
    /// Whether the agent's context is the Unit default or an explicit Struct.
    enum class ContextKind { Unit, Struct };
    ContextKind context_kind{ContextKind::Unit};
    /// Declaration-order capability whitelist, resolved to the flat Core
    /// capability table. This persists Sema's authorization fact so a
    /// standalone Core verifier/backend never needs the discarded frontend
    /// environment or a display-name lookup.
    std::vector<CoreCapabilityId> capabilities;
    /// Source declaration provenance for standalone verifier/backend errors.
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CoreAgentDecl &,
                                         const CoreAgentDecl &) noexcept = default;
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
///   * `param_types` / `return_type` -- the fully materialized logical signature
///     in the program-global CoreValueType arena. Source-level parameter names
///     and AHFL TypeRefs are intentionally dropped: retaining both would create
///     two signature SSOTs.
///
/// What is ERASED: the rest of the `CapabilityEffectSpec` (domain, idempotency
/// key, receipt/retry mode, timeout, compensation, policies) is verification /
/// orchestration metadata consumed above Core-IR; it does not exist at this
/// layer (RFC 0026 erasure invariant). The public ahfl_cap wire frame remains
/// RFC 0021 value_json; these logical ids do not redefine that byte encoding.
struct CoreCapabilityDecl {
    /// Display name (diagnostic only). `symbol_ref` is the canonical identity.
    std::string name;
    /// Resolved nominal symbol of the originating capability (reused as-is from
    /// the verification layer; strings on it are display/diagnostic only).
    ir::SymbolRef symbol_ref;
    /// Effect category of the capability (reused `ir::CapabilityEffectKind`).
    ir::CapabilityEffectKind effect_kind{ir::CapabilityEffectKind::Unknown};
    /// Import-signature parameter value types in declaration order. Each id is
    /// interned in CoreProgram::value_types by the same arena body SSA uses.
    std::vector<CoreValueTypeId> param_types;
    /// Import-signature return value type in the same arena.
    CoreValueTypeId return_type{};
    /// Source declaration provenance for signature/ABI diagnostics.
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CoreCapabilityDecl &,
                                         const CoreCapabilityDecl &) noexcept = default;
};

/// Core-IR declaration node set. Minimal by design: this skeleton represents
/// agent state machines and explicit capability-call (import) declarations.
/// Later KR6.4 sub-slices grow the variant with monomorphized function bodies
/// and the remaining orchestration constructs (flow / workflow). Kept a
/// `std::variant` (Principle 4) so those additions are additive alternatives,
/// not a class hierarchy.
using CoreDecl = std::variant<CoreAgentDecl, CoreCapabilityDecl>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate. Adding or removing a
// Core node alternative without updating every exhaustive visitor MUST fail
// the build; keep this pin adjacent to the declaration.
static_assert(std::variant_size_v<CoreDecl> == 2,
              "ahfl::ir::core::CoreDecl cardinality drift (RFC 0027 P8 IR "
              "SSOT): update every exhaustive visitor and this pin together "
              "with the alternative list.");

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

// (Typed-ID definitions — CoreValueId / CoreExprId / CoreCapabilityId /
// CoreTypeId / CoreAgentId / CoreFieldId / CoreVariantId — are defined near the
// top of this header, before CoreAgentDecl, since several decls now reference
// them.)

/// Root of a path read, mirroring `ir::PathRootKind` structurally (Principle 2:
/// we keep the kind, we do NOT flatten the path to a dotted string).
///
/// `WorkflowInput` / `WorkflowNodeOutput` are the two workflow value roots (RFC
/// 0026 KR6.4 workflow lower): inside a workflow node input / return region, a
/// path roots at the workflow input struct, or at an upstream node's output
/// (identified by a typed `CoreWorkflowNodeId`, NEVER the node's source name).
enum class CorePathRoot { Input, Context, Local, Identifier, WorkflowInput, WorkflowNodeOutput };

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

/// One resolved step of a member projection (`.field`), fully self-contained:
/// the `owner_type` the field belongs to, the typed `field` id within it, and
/// the field's own `result_type` when it is a struct (so the next step's
/// `owner_type` equals this `result_type`). `result_type` is invalid (kInvalid)
/// for a primitive/collection/P4 field, which may only appear as the LAST step.
struct CoreProjectionStep {
    CoreTypeId owner_type{}; // struct that declares `field`
    CoreFieldId field{};     // typed field index within `owner_type`
    CoreTypeId result_type{}; // field's own struct type, or kInvalid (last step only)
    [[nodiscard]] friend bool operator==(const CoreProjectionStep &,
                                         const CoreProjectionStep &) noexcept = default;
};

/// A path read (`input.x`, `ctx.y.z`, a local, or a free identifier). It is
/// SELF-CONTAINED for a backend: `root_type` is the CoreTypeId the root
/// denotes, and `projection` is the resolved step chain (each step carries its
/// owner/field/result type). Invariants (checked at lowering): `projection[0]
/// .owner_type == root_type`; `projection[i].owner_type == projection[i-1]
/// .result_type`; a step with invalid `result_type` is the last. The `members`
/// strings are DISPLAY-ONLY (diagnostics) — canonical data is `projection`.
/// A `Local` root additionally resolves to the binding's `CoreValueId`.
/// `projection_resolved` is false when a member could not be resolved
/// (fail-closed: the lowering emits a diagnostic and the program is not
/// executable).
struct CorePathExpr {
    CorePathRoot root{CorePathRoot::Identifier};
    std::string root_name;                  // display / identifier root
    std::vector<std::string> members;       // display-only member chain
    CoreTypeId root_type{};                 // typed identity of the root (kInvalid if unknown)
    std::vector<CoreProjectionStep> projection; // resolved typed steps (canonical)
    bool projection_resolved{true};         // false => a member could not be resolved
    CoreValueId local{};                    // valid iff root == Local (resolved binding)
    bool has_local{false};
    /// Valid iff root == WorkflowNodeOutput: the upstream workflow node whose
    /// output this path reads. `root_type` is that node's target agent output
    /// type, so the `projection` chain resolves against it like any struct read.
    CoreWorkflowNodeId workflow_node{};
    [[nodiscard]] friend bool operator==(const CorePathExpr &,
                                         const CorePathExpr &) noexcept = default;
};

/// A qualified value — in this slice, a UNIT enum variant with no payload
/// (e.g. `Option::None`, `AuditResult::Approve`). Carries typed identity: the
/// owning enum `type_id` and the `variant` id. `name` is display-only.
struct CoreQualifiedExpr {
    std::string name;          // display (e.g. "std::option::Option::None")
    CoreTypeId type_id{};      // owning enum type identity
    CoreVariantId variant{};   // variant identity within `type_id`
    bool resolved{false};      // false => not resolved to a typed variant (diagnostic)
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

/// One field/payload operand of a constructor, carrying its typed field
/// identity so a backend never relies on source WRITE order. For a struct
/// literal each `field` is the owning struct's `CoreFieldId`; for an enum
/// variant payload it is the positional payload slot (0-based).
struct CoreConstructArg {
    CoreFieldId field{};   // typed field identity within the owning type
    CoreValueId value{};   // the ANF operand
    [[nodiscard]] friend bool operator==(const CoreConstructArg &,
                                         const CoreConstructArg &) noexcept = default;
};

/// A PURE constructor / aggregate (struct literal, enum variant with payload,
/// e.g. `Option::Some(%t0)`). Its operands are already-bound value ids, so a
/// capability call nested in a source constructor has been hoisted OUT to a
/// preceding `CoreStmt::CapabilityCall` before this node is built. Identity is
/// typed (Principle 2): the owning `type_id`, the `variant` id (for an enum),
/// and each arg's `CoreFieldId`. `type_name`/`variant_name` are display-only.
/// Field IDENTITY (not source write order) determines which value fills which
/// field, so `Pair { b: 2, a: 1 }` cannot be mis-assigned.
struct CoreConstructExpr {
    std::string type_name;                 // constructed nominal / enum name (display)
    std::string variant_name;              // non-empty for an enum variant (display)
    bool is_enum_variant{false};
    CoreTypeId type_id{};                  // owning nominal type identity
    CoreVariantId variant{};               // variant identity WITHIN `type_id` (iff enum)
    bool resolved{false};                  // false => type/variant not resolved (diagnostic)
    std::vector<CoreConstructArg> args;    // field-identified operands (order-independent)
    [[nodiscard]] friend bool operator==(const CoreConstructExpr &,
                                         const CoreConstructExpr &) noexcept = default;
};

/// One orthogonal operation in a normalized coercion proof node. TypeArg carries
/// no variance field: the verifier reads the owning CoreTypeDecl::variances SSOT.
enum class CoreCoercionOpKind {
    IntWiden,
    StringWiden,
    CapacityWiden,
    TypeArg,
    FnParam,
    FnReturn,
};

struct CoreCoercionOp {
    CoreCoercionOpKind kind{CoreCoercionOpKind::IntWiden};
    std::uint32_t arg_index{0};
    CoreCoercionPlanId child{};
    [[nodiscard]] friend bool operator==(const CoreCoercionOp &,
                                         const CoreCoercionOp &) noexcept = default;
};

/// A normalized proof-tree node. source/result are complete logical value types;
/// ops name the dimensions that change and point to child proofs for projections.
struct CoreCoercionPlanNode {
    CoreValueTypeId source{};
    CoreValueTypeId result{};
    std::vector<CoreCoercionOp> ops;
    [[nodiscard]] friend bool operator==(const CoreCoercionPlanNode &,
                                         const CoreCoercionPlanNode &) noexcept = default;
};

/// A pure coercion that consumes an existing SSA value and produces a fresh one.
/// Physical realization is layout-dependent and deferred to P4-D.
struct CoreCoerceExpr {
    CoreValueId operand{};
    CoreCoercionPlanId plan{};
    [[nodiscard]] friend bool operator==(const CoreCoerceExpr &,
                                         const CoreCoerceExpr &) noexcept = default;
};

/// One operation on a BOUNDED collection value (RFC 0026 P6-5). The value model
/// has no 2-word handle: a collection's P6 representation is the i32 ADDRESS of
/// its inline `(ptr,len)` header, exactly like an aggregate's address, and every
/// fact about its backing store (element layout, stride, capacity, Map value
/// offset, checked `backing_size`) comes from the P4-D `CoreLayoutContainer` —
/// codegen re-derives NOTHING.
///
/// `base` is the SSA value holding that address; `index` / `value` are the other
/// already-bound operands (ANF), valid per op:
///   * `Len`        : neither is valid; the result is the header's `len` word.
///   * `ElementGet` : `index`; the result is element `index` (a Map's VALUE
///                    entry for a Map base, offset by `value_offset`).
///   * `ElementSet` : `index` + `value`; the result is the base address (so the
///                    operation can be chained), and the element slot is written.
///
/// This is a typed node, never a string-keyed builtin call: the op is an enum and
/// the collection identity is the base value's interned `CoreValueTypeId`, so a
/// backend can never mistake one container for another.
enum class CoreCollectionOpKind { Len, ElementGet, ElementSet };

struct CoreCollectionExpr {
    CoreCollectionOpKind op{CoreCollectionOpKind::Len};
    CoreValueId base{};
    CoreValueId index{};
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CoreCollectionExpr &,
                                         const CoreCollectionExpr &) noexcept = default;
};

/// A direct, statically-resolved PURE call to one monomorphized fn /
/// body-bearing method instance (CORE-FNBODY-DESIGN §5.1, RFC 0026 FB-1).
/// Lowers to a plain wasm `call <funcidx>`; there is no indirect dispatch.
/// `callee` MUST be a `CoreInstanceDecl` holding a `CoreFnInstance` with a
/// valid body, `args` are ANF operands left-to-right, and a method call passes
/// its receiver as argument 0. The callee body contains no
/// `CoreCapabilityCallStmt` (effectful callees stay fail-closed until FB-4).
/// Arity / argument / result types are checked against the callee's concrete
/// signature materialized per design §2.1 (never read from the dispatch
/// type-argument vector).
struct CoreCallExpr {
    CoreInstanceId callee{};
    std::vector<CoreValueId> args;
    [[nodiscard]] friend bool operator==(const CoreCallExpr &,
                                         const CoreCallExpr &) noexcept = default;
};

/// Construct a closure value `(fn, env)` (CORE-FNBODY-DESIGN §3.1, RFC 0026
/// FB-3a1). PURE: the environment operands are already-bound ANF SSA values, so
/// constructing a closure never hides an effect. `fn` is the monomorphized,
/// outlined fn body this closure dispatches to (a `CoreFnId` into
/// `CoreProgram::fns`); `env` are the ordered captured SSA operands in canonical
/// env-slot order (design §3.2 — explicit capture list in source order, else
/// first-use DFS order). A zero-`env` closure is how a static fn name passed as a
/// first-class value is represented (`(table_slot, env_ptr=0)`); there is no
/// separate fn-ref node. A node's `result_type` is an interned
/// `CoreVtClosure{ signature, captures }` over the callee fn's concrete
/// `CoreVtFn` signature, with one `CoreClosureCapture` per `env` slot
/// (ByValue only, design §3.4); the verifier checks `fn` bounds and the
/// slot-wise count/type agreement with that declared capture signature. Lambda
/// lifting, which makes the lowerer emit this node, arrives in FB-3a2.
struct CoreClosureExpr {
    CoreFnId fn{};
    std::vector<CoreValueId> env;
    [[nodiscard]] friend bool operator==(const CoreClosureExpr &,
                                         const CoreClosureExpr &) noexcept = default;
};

/// An indirect call through a first-class closure value
/// (CORE-FNBODY-DESIGN §5.2, RFC 0026 FB-3a1). PURE, like `CoreCallExpr`:
/// closures are Pure-only at the type-system layer, so a closure call cannot
/// hide a capability effect and needs no statement form. `callee` is an
/// already-bound SSA value whose logical type is a callable — a construction-
/// site `CoreVtClosure` or a binding-site signature `CoreVtFn` (the D-FNREP
/// compatibility rule, design §3.1.1: a `CoreVtClosure{S,_}` flows at any
/// `CoreVtFn{S}` slot); `args` are the ANF operands in left-to-right eval order.
/// Lowers to `call_indirect`, expecting the shared signature. The node's
/// `result_type` is the callable signature's concrete fn return value type.
/// Emission arrives with the FB-3 codegen slice; this slice lands the model,
/// structural verification, and wire symmetry.
struct CoreCallClosureExpr {
    CoreValueId callee{};
    std::vector<CoreValueId> args;
    [[nodiscard]] friend bool operator==(const CoreCallClosureExpr &,
                                         const CoreCallClosureExpr &) noexcept = default;
};

/// A structurally-preserved but not-yet-lowered PURE expression (e.g. match,
/// lambda, member-access forms deferred to a later sub-slice). It carries
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

namespace core_node_detail {

// Tag tuples for the X-macro-generated Core node variants (KR6.13-P7). Each is
// expanded from its .def list; the variant is reconstructed from the element
// types, so declaration order in the .def IS alternative order. Each tag tuple
// is defined adjacent to the node structs it names.
#define HANDLE_CORE_EXPR_NODE(Name, Wire) ::ahfl::ir::node_detail::node_tag<Name>{},
inline constexpr auto kCoreExprTags = std::tuple{
#include "ahfl/compiler/ir/core_expr_nodes.def"
};
#undef HANDLE_CORE_EXPR_NODE

/// JSON wire name of each CoreExprNode alternative, indexed by
/// `CoreExprNode::index()` (RFC 0026 P9 §3). The ONE table the Core JSON writer
/// and reader resolve the `"kind"` spelling from.
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCoreExprTags)>>>
    kCoreExprWireNames = {
#define HANDLE_CORE_EXPR_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/core_expr_nodes.def"
};

} // namespace core_node_detail

// RFC 0027 P6/P7/P8 (KR6.13-P7): the alternative list is generated from the
// single X-macro node list core_expr_nodes.def — one line per node, declaration
// order preserved. To add a node, edit ONLY that .def; the variant, every
// exhaustive visitor, and (via the negative compile-test) the whole consumer
// set stay in lockstep, and a node that misses a handler is a compile error
// naming the type rather than a silently skipped case.
using CoreExprNode = ::ahfl::ir::node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(core_node_detail::kCoreExprTags)>>;

// RFC 0026 P9 §3: the JSON `kind` wire name of a CoreExprNode alternative.
[[nodiscard]] inline constexpr std::string_view
core_expr_node_wire_name(std::size_t variant_index) noexcept {
    return core_node_detail::kCoreExprWireNames[variant_index];
}

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see CoreDecl). The node
// count itself now derives from core_expr_nodes.def; this pin turns "a node was
// added to the .def" into a deliberate review event across every exhaustive
// visitor (verify / lower / json).
static_assert(std::variant_size_v<CoreExprNode> == 13,
              "ahfl::ir::core::CoreExprNode cardinality drift (RFC 0027 P8 "
              "IR SSOT): update every exhaustive visitor, this pin, and "
              "core_expr_nodes.def together.");

struct CoreExpr {
    CoreExprNode node;
    SourceRangeOpt source_range;
    /// RFC 0026 P4-B: the logical value type this pure expression evaluates to
    /// (index into `CoreProgram::value_types`). REQUIRED in a lowering-clean
    /// program: the standalone verifier checks every arena entry, and
    /// `CoreLetStmt` / `CoreValueRefExpr` consistency is proven against it. A
    /// lowering-ERROR partial artifact may leave this `kInvalid` (the verifier is
    /// only run on error-free candidates); no clean program carries a placeholder.
    CoreValueTypeId result_type{};
    [[nodiscard]] friend bool operator==(const CoreExpr &, const CoreExpr &) noexcept = default;
};

// --- match patterns (flat arena; Principle 3/4) ---
//
// A match arm's pattern is a tree stored FLAT in the per-flow pattern arena
// (`CoreFlowDecl::patterns`, addressed by `CorePatternId`): a variant / or /
// binding pattern names its children by id, never by owning pointer. Binding
// patterns do NOT define their own value — they name an arm binding
// (`CorePatternBindingId` into `CoreMatchArm::bindings`), so an or-pattern's
// alternatives (`A(x) | B(x)`) share ONE binding and flow-global SSA
// single-definition is preserved. (The match statement / arm nodes land in the
// next slice; this slice defines the pattern arena + its structural verifier.)

/// `_` — matches anything, binds nothing.
struct CoreWildcardPat {
    [[nodiscard]] friend bool operator==(const CoreWildcardPat &,
                                         const CoreWildcardPat &) noexcept = default;
};

/// A literal pattern (`0`, `"x"`, `true`, `none`). Kind + spelling mirror
/// `CoreLiteralExpr`; physical encoding is deferred to P4. Note: the source
/// literal `none` is NOT a `Unit` literal — it lowers to an `Option::None`
/// variant pattern (typed identity), never a `CoreLiteralPat`.
struct CoreLiteralPat {
    CoreLiteralKind kind{CoreLiteralKind::Unit};
    std::string spelling;
    [[nodiscard]] friend bool operator==(const CoreLiteralPat &,
                                         const CoreLiteralPat &) noexcept = default;
};

/// An integer range pattern (`start..end`). AHFL `..` is a CLOSED interval:
/// it matches an integer `v` iff `start <= v <= end`. The bounds are the
/// source-level integers as typed i64, so a backend never re-parses a range out
/// of a spelling. A reverse range (`start > end`) is rejected by the verifier
/// (PATTERN_SHAPE_INVALID) — physical match lowering is deferred to P4.
struct CoreIntRangePat {
    std::int64_t start{0};
    std::int64_t end{0};
    [[nodiscard]] friend bool operator==(const CoreIntRangePat &,
                                         const CoreIntRangePat &) noexcept = default;
};

/// A binding pattern (`x` or `x @ nested`). `binding` names the arm binding this
/// introduces (shared across or-alternatives). `nested` (optional) further
/// matches the bound value.
struct CoreBindingPat {
    CorePatternBindingId binding{};
    CorePatternId nested{}; // kInvalid when there is no `@ nested`
    bool has_nested{false};
    [[nodiscard]] friend bool operator==(const CoreBindingPat &,
                                         const CoreBindingPat &) noexcept = default;
};

/// One field of a struct-payload variant pattern: the declared payload slot id
/// plus the sub-pattern matched against it. Storing the slot (not relying on
/// write order) keeps `{ owner: _, id }`, field reordering, and `..` (rest)
/// field-identity-correct.
struct CoreVariantPatField {
    CoreFieldId slot{};
    CorePatternId pattern{};
    [[nodiscard]] friend bool operator==(const CoreVariantPatField &,
                                         const CoreVariantPatField &) noexcept = default;
};

/// An enum-variant pattern (`Some(x)`, `Open { id, owner: _ }`, `None`). Typed
/// identity: the owning `owner_enum` + `variant`. A Tuple payload uses
/// `tuple_subpatterns` (positional, in declared slot order); a Struct payload
/// uses `struct_fields` (slot-identified, order-independent) with `has_rest` for
/// a `..` remainder. A Unit variant leaves both empty.
struct CoreVariantPat {
    CoreTypeId owner_enum{};
    CoreVariantId variant{};
    std::vector<CorePatternId> tuple_subpatterns;
    std::vector<CoreVariantPatField> struct_fields;
    bool has_rest{false};
    [[nodiscard]] friend bool operator==(const CoreVariantPat &,
                                         const CoreVariantPat &) noexcept = default;
};

/// An or-pattern (`A | B | …`). Every alternative must bind the SAME arm binding
/// set (checked by the verifier), so a downstream arm body sees one consistent
/// binding regardless of which alternative matched.
struct CoreOrPat {
    std::vector<CorePatternId> alternatives;
    [[nodiscard]] friend bool operator==(const CoreOrPat &, const CoreOrPat &) noexcept = default;
};

/// A tuple pattern (`(a, b, c)`) — positional element sub-patterns. Distinct from
/// a tuple-payload VARIANT pattern (which carries a typed `owner_enum`/`variant`):
/// this is the anonymous structural tuple. Physical shape/arity checking against
/// the scrutinee tuple type is deferred to P4 (Core-IR types have no tuple type
/// yet); the arena verifier only checks each element id is in range.
struct CoreTuplePat {
    std::vector<CorePatternId> elements;
    [[nodiscard]] friend bool operator==(const CoreTuplePat &, const CoreTuplePat &) noexcept = default;
};

namespace core_node_detail {

#define HANDLE_CORE_PATTERN_NODE(Name, Wire) ::ahfl::ir::node_detail::node_tag<Name>{},
inline constexpr auto kCorePatternTags = std::tuple{
#include "ahfl/compiler/ir/core_pattern_nodes.def"
};
#undef HANDLE_CORE_PATTERN_NODE

/// JSON wire name of each CorePatternNode alternative, indexed by
/// `CorePatternNode::index()` (RFC 0026 P9 §3).
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCorePatternTags)>>>
    kCorePatternWireNames = {
#define HANDLE_CORE_PATTERN_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/core_pattern_nodes.def"
};

} // namespace core_node_detail

// RFC 0027 P6/P7/P8 (KR6.13-P7): generated from core_pattern_nodes.def.
using CorePatternNode = ::ahfl::ir::node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(core_node_detail::kCorePatternTags)>>;

// RFC 0026 P9 §3: the JSON `kind` wire name of a CorePatternNode alternative.
[[nodiscard]] inline constexpr std::string_view
core_pattern_node_wire_name(std::size_t variant_index) noexcept {
    return core_node_detail::kCorePatternWireNames[variant_index];
}

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see CoreDecl). The node
// count derives from core_pattern_nodes.def.
static_assert(std::variant_size_v<CorePatternNode> == 7,
              "ahfl::ir::core::CorePatternNode cardinality drift (RFC 0027 "
              "P8 IR SSOT): update every exhaustive visitor, this pin, and "
              "core_pattern_nodes.def together.");

struct CorePattern {
    CorePatternNode node;
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CorePattern &, const CorePattern &) noexcept = default;
};



/// A storable place (`ctx.field`, `ctx.a.b`, a local, …). Self-contained like
/// `CorePathExpr`: `root_type` + resolved `projection` step chain are the
/// canonical data; `members` strings are display-only. `projection_resolved`
/// is false when resolution failed (fail-closed).
struct CorePlace {
    CorePathRoot root{CorePathRoot::Context};
    std::string root_name;
    std::vector<std::string> members;       // display-only
    CoreTypeId root_type{};
    std::vector<CoreProjectionStep> projection; // resolved typed steps (canonical)
    bool projection_resolved{true};
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

/// Yield a value out of a match ARM body or GUARD region (MLIR scf.yield style).
/// A guard region yields the Bool guard result; an arm body yields the arm's
/// value for an expression match (or no value for a statement match). It is the
/// region's terminator INPUT to the enclosing match's result join — it does NOT
/// itself write the match result id. Only legal inside a guard / match-arm
/// region (the verifier rejects it in an ordinary flow region).
struct CoreYieldStmt {
    bool has_value{false};
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CoreYieldStmt &, const CoreYieldStmt &) noexcept = default;
};

/// The kind of an unconditional runtime trap.
enum class CoreTrapKind { NonExhaustiveMatch };

/// An unconditional runtime trap — a diverging terminator. Used as a match's
/// explicit fallback so a non-exhaustive match cannot structurally "fall
/// through" (the verifier trusts this structure, not a mutable exhaustive flag).
struct CoreTrapStmt {
    CoreTrapKind kind{CoreTrapKind::NonExhaustiveMatch};
    [[nodiscard]] friend bool operator==(const CoreTrapStmt &, const CoreTrapStmt &) noexcept = default;
};

/// One binding introduced by a match arm (`x` in `Some(x)`), shared across an
/// or-pattern's alternatives. `value` is the fresh SSA value id it defines
/// (flow-global single definition; visible only in the arm's guard + body). The
/// bound value's logical TYPE lives in the owning body's `value_types[value]`
/// table (RFC 0026 P4-B) — it covers primitives, bounds, and nominals uniformly,
/// so this struct no longer carries a nominal-only `binding_type`.
struct CorePatternBinding {
    CoreValueId value{};
    [[nodiscard]] friend bool operator==(const CorePatternBinding &,
                                         const CorePatternBinding &) noexcept = default;
};

/// One arm of a match. `pattern` is the arena id of the arm's pattern;
/// `bindings` are the arm-scoped SSA values its binding patterns introduce (a
/// `CoreBindingPat` names one by index). `guard_region` (null when absent) runs
/// after the pattern matches, in a scope where `bindings` are visible, and
/// yields a Bool; a false guard falls to the next arm. `body` runs when the
/// pattern matches and any guard passed, and yields the arm's value (expression
/// match) or no value (statement match), or diverges (return/goto/trap).
struct CoreMatchArm {
    CorePatternId pattern{};
    std::vector<CorePatternBinding> bindings;
    std::unique_ptr<CoreRegion> guard_region; // null = no guard
    std::unique_ptr<CoreRegion> body;
    friend bool operator==(const CoreMatchArm &, const CoreMatchArm &) noexcept;
};

/// A structured match. `scrutinee` is the already-bound value matched. Arms are
/// tried in order; the FIRST whose pattern matches (and whose guard, if any,
/// passes) runs. `result` (set => expression match) is defined ONCE by the match
/// in the enclosing scope from the arms' yielded values; unset => statement
/// match. `fallback_region` (never null) runs when no arm matched — a
/// non-exhaustive match lowers it to `CoreTrapStmt{NonExhaustiveMatch}`, an
/// if-let to its `else`. It is a SEPARATE region (not a synthetic arm), so total
/// coverage is a structural fact the verifier trusts — not a mutable flag.
struct CoreMatchStmt {
    CoreValueId scrutinee{};
    bool has_result{false};
    CoreValueId result{};
    std::vector<CoreMatchArm> arms;
    std::unique_ptr<CoreRegion> fallback_region; // never null in a well-formed program
    friend bool operator==(const CoreMatchStmt &, const CoreMatchStmt &) noexcept;
};

namespace core_node_detail {

#define HANDLE_CORE_STMT_NODE(Name, Wire) ::ahfl::ir::node_detail::node_tag<Name>{},
inline constexpr auto kCoreStmtTags = std::tuple{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
};
#undef HANDLE_CORE_STMT_NODE

/// JSON wire name of each CoreStmtNode alternative, indexed by
/// `CoreStmtNode::index()` (RFC 0026 P9 §3).
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCoreStmtTags)>>>
    kCoreStmtWireNames = {
#define HANDLE_CORE_STMT_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/core_stmt_nodes.def"
};

} // namespace core_node_detail

// RFC 0027 P6/P7/P8 (KR6.13-P7): generated from core_stmt_nodes.def.
using CoreStmtNode = ::ahfl::ir::node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(core_node_detail::kCoreStmtTags)>>;

// RFC 0026 P9 §3: the JSON `kind` wire name of a CoreStmtNode alternative.
[[nodiscard]] inline constexpr std::string_view
core_stmt_node_wire_name(std::size_t variant_index) noexcept {
    return core_node_detail::kCoreStmtWireNames[variant_index];
}

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see CoreDecl). The node
// count derives from core_stmt_nodes.def.
static_assert(std::variant_size_v<CoreStmtNode> == 9,
              "ahfl::ir::core::CoreStmtNode cardinality drift (RFC 0027 P8 "
              "IR SSOT): update every exhaustive visitor, this pin, and "
              "core_stmt_nodes.def together.");

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

// --- structural control-flow exit summary (lowerer side of a lockstep pair) ---
//
// `core_region_exit` computes, purely structurally, HOW a region leaves on its
// paths. It is the LOWERER's control-flow model: the seal
// (`seal_statement_arm`) uses it to decide whether a statement-position region
// needs a trailing unit yield. The Core verifier (`core_verify.cpp`) keeps its
// OWN per-path walk with a richer `RegionExit` (it additionally tracks value
// def/use, yield ARITY, and Flow-vs-arm context), and does NOT literally call
// this function. The two are a LOCKSTEP pair with MIRRORED control-flow
// semantics — the completion / divergence / fallthrough rules below are exactly
// the rules the verifier's walk applies — and they MUST stay in sync by hand:
// P0-2 (fixed in 7a3eff83) was precisely a drift where the lowerer's structural
// helper diverged from the verifier's per-path rule for nested-match yields.
// The shared per-path fixtures in `core_verify.cpp` guard that parity. Three
// exit modes, each "some path leaves this way" (merged across paths):
//   * fallthrough — a path runs off the region END with no explicit exit;
//   * yields      — a path exits via a CoreYieldStmt (arm / guard completion);
//   * diverges    — a path exits via Return / Goto / Trap.
//
// A region COMPLETES (returns control to an enclosing match, letting it
// continue) on any path that falls through OR yields. The subtle, previously
// mis-handled case (fixed here) is a NESTED match: its arms' yields are
// CONSUMED by that match — a nested match whose arms end in `yield` means the
// nested match COMPLETED NORMALLY and control returns to the parent region; it
// does NOT terminate the parent. So a match lets its parent continue iff SOME
// arm body or the fallback completes (fallthrough or yield); only when EVERY
// arm + fallback diverges (Return / Goto / Trap) does the match terminate the
// parent's straight-line path. Per-statement rules:
//   * Return / Goto / Trap                -> diverges (path leaves);
//   * Yield                               -> yields   (path leaves);
//   * CoreIfStmt                          -> the parent continues past it iff
//                                            EITHER branch can fall through (an
//                                            else-less / absent branch is an
//                                            implicit fallthrough);
//   * CoreMatchStmt                       -> the parent continues past it iff
//                                            SOME arm body / fallback completes;
//   * Let / CapabilityCall / Store        -> fall through to the next statement.
struct CoreRegionExit {
    bool fallthrough{false}; // some path reaches the region end (no explicit exit)
    bool yields{false};      // some path exits via a yield (arm / guard completion)
    bool diverges{false};    // some path exits via return / goto / trap
    // The region completes (control returns to an enclosing match) on any path
    // that falls through or yields.
    [[nodiscard]] bool completes() const noexcept { return fallthrough || yields; }
};

[[nodiscard]] CoreRegionExit core_region_exit(const CoreRegion &region) noexcept;

// Whether control can reach the END of a region (i.e. some path runs off the
// end without an explicit exit). Thin projection of `core_region_exit`; this is
// what the lowerer's `seal_statement_arm` asks (append a trailing unit yield
// iff the region can fall through).
[[nodiscard]] inline bool core_region_may_fallthrough(const CoreRegion &region) noexcept {
    return core_region_exit(region).fallthrough;
}

inline CoreRegionExit core_region_exit(const CoreRegion &region) noexcept {
    CoreRegionExit exit;
    bool live = true; // can control reach the NEXT statement on the straight line?
    for (const CoreStmt &stmt : region.statements) {
        if (!live) {
            break; // statements after a terminator are unreachable
        }
        // RFC 0027 P6/P7/P8 (KR6.13-P7): one NAMED handler per CoreStmtNode
        // alternative, generated from core_stmt_nodes.def. The straight-line
        // continuation nodes (Let / CapabilityCall / Store) name the
        // capture-less CORE_REGION_EXIT_CONTINUE no-op; terminators / if / match
        // each get a real handler. With no generic catch-all, a new
        // CoreStmtNode alternative is a COMPILE ERROR here until it is
        // classified (CLAUDE.md Principle 5).
#define CORE_REGION_EXIT_CONTINUE(Name, Wire) [](const Name &) {},
#define CORE_REGION_EXIT_DIVERGE(Name, Wire)                                                             \
    [&](const Name &) {                                                                            \
        exit.diverges = true;                                                                      \
        live = false;                                                                              \
    },
#define CORE_REGION_EXIT_YIELD(Name, Wire)                                                               \
    [&](const Name &) {                                                                            \
        exit.yields = true;                                                                        \
        live = false;                                                                              \
    },
#define CORE_REGION_EXIT_CoreIfStmt(Name, Wire)                                                              \
    [&](const Name &node) {                                                                        \
        CoreRegionExit then_exit;                                                                  \
        if (node.then_region) {                                                                    \
            then_exit = core_region_exit(*node.then_region);                                       \
        } else {                                                                                   \
            then_exit.fallthrough = true; /* absent then => implicit fallthrough */                \
        }                                                                                          \
        CoreRegionExit else_exit;                                                                  \
        if (node.else_region) {                                                                    \
            else_exit = core_region_exit(*node.else_region);                                       \
        } else {                                                                                   \
            else_exit.fallthrough = true; /* else-less => implicit fallthrough */                  \
        }                                                                                          \
        /* Both branches' yields / divergences are possible outcomes. */                           \
        exit.yields |= then_exit.yields || else_exit.yields;                                       \
        exit.diverges |= then_exit.diverges || else_exit.diverges;                                 \
        /* Control continues past the `if` iff EITHER branch can. */                               \
        live = then_exit.fallthrough || else_exit.fallthrough;                                     \
    },
#define CORE_REGION_EXIT_CoreMatchStmt(Name, Wire)                                                           \
    [&](const Name &node) {                                                                        \
        bool any_completes = false;                                                                \
        bool any_diverges = false;                                                                 \
        for (const auto &arm : node.arms) {                                                        \
            if (arm.body) {                                                                        \
                const CoreRegionExit ae = core_region_exit(*arm.body);                             \
                any_completes |= ae.completes();                                                   \
                any_diverges |= ae.diverges;                                                       \
            } else {                                                                               \
                any_completes = true; /* missing body: assume completion (verifier flags) */       \
            }                                                                                      \
        }                                                                                          \
        if (node.fallback_region) {                                                                \
            const CoreRegionExit fe = core_region_exit(*node.fallback_region);                     \
            any_completes |= fe.completes();                                                       \
            any_diverges |= fe.diverges;                                                           \
        } else {                                                                                   \
            any_completes = true; /* missing fallback: assume completion (verifier flags) */       \
        }                                                                                          \
        /* A nested match consumes its arms' yields (they are NOT the parent's yield); only */     \
        /* control / trap divergence propagates. */                                                \
        exit.diverges |= any_diverges;                                                             \
        /* Control continues past the match iff SOME arm / fallback completes; if every arm + */    \
        /* fallback diverges, the match terminates the parent's straight-line path. */             \
        live = any_completes;                                                                      \
    },

#define CORE_REGION_EXIT_CoreLetStmt(Name, Wire) CORE_REGION_EXIT_CONTINUE(Name, Wire)
#define CORE_REGION_EXIT_CoreCapabilityCallStmt(Name, Wire) CORE_REGION_EXIT_CONTINUE(Name, Wire)
#define CORE_REGION_EXIT_CoreStoreStmt(Name, Wire) CORE_REGION_EXIT_CONTINUE(Name, Wire)
#define CORE_REGION_EXIT_CoreGotoStmt(Name, Wire) CORE_REGION_EXIT_DIVERGE(Name, Wire)
#define CORE_REGION_EXIT_CoreReturnStmt(Name, Wire) CORE_REGION_EXIT_DIVERGE(Name, Wire)
#define CORE_REGION_EXIT_CoreTrapStmt(Name, Wire) CORE_REGION_EXIT_DIVERGE(Name, Wire)
#define CORE_REGION_EXIT_CoreYieldStmt(Name, Wire) CORE_REGION_EXIT_YIELD(Name, Wire)
#define HANDLE_CORE_STMT_NODE(Name, Wire) CORE_REGION_EXIT_##Name(Name, Wire)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
            },
            stmt.node);
#undef HANDLE_CORE_STMT_NODE
#undef CORE_REGION_EXIT_CoreLetStmt
#undef CORE_REGION_EXIT_CoreCapabilityCallStmt
#undef CORE_REGION_EXIT_CoreStoreStmt
#undef CORE_REGION_EXIT_CoreGotoStmt
#undef CORE_REGION_EXIT_CoreReturnStmt
#undef CORE_REGION_EXIT_CoreTrapStmt
#undef CORE_REGION_EXIT_CoreYieldStmt
#undef CORE_REGION_EXIT_CONTINUE
#undef CORE_REGION_EXIT_DIVERGE
#undef CORE_REGION_EXIT_YIELD
#undef CORE_REGION_EXIT_CoreIfStmt
#undef CORE_REGION_EXIT_CoreMatchStmt
    }
    if (live) {
        exit.fallthrough = true;
    }
    return exit;
}

// --- shared body storage: one owner per ANF arena (CORE-FNBODY-DESIGN §2.2) --

/// The five ANF assets a lowered executable body owns — the pure-expression
/// arena, the SSA value counter, the dense per-value logical-type table, the
/// normalized coercion-plan arena, and the match-pattern arena. Flow handlers,
/// workflow regions, and (RFC 0026 FB-1) outlined fn bodies each own exactly one
/// of these, so the lowerer's non-owning `CoreBodyStorageRef` view and the
/// `ExprLowerer<RootPolicy>` template are reused across all three body kinds —
/// no second expr/stmt model (Principle 3, Principle 1: single SSOT).
struct CoreBodyStorage {
    std::vector<CoreExpr> exprs;              // pure-expression arena (CoreExprId)
    std::uint32_t value_count{0};             // number of CoreValueIds allocated
    /// RFC 0026 P4-B: logical type per CoreValueId (index == value); DENSE:
    /// value_types.size() == value_count in a lowering-clean body.
    std::vector<CoreValueTypeId> value_types;
    std::vector<CoreCoercionPlanNode> coercion_plans; // normalized proof arena
    std::vector<CorePattern> patterns;        // match-pattern arena (Principle 3)
    [[nodiscard]] friend bool operator==(const CoreBodyStorage &,
                                         const CoreBodyStorage &) noexcept = default;
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

/// The execution-layer projection of an `ir::FlowDecl`. Owns its executable
/// body's ANF assets (expr arena, value counter, value-type table, coercion
/// plans, patterns) in one `CoreBodyStorage` — shared with workflow regions
/// and outlined fn bodies (CORE-FNBODY-DESIGN §2.2).
struct CoreFlowDecl {
    CoreAgentId target{};               // typed target-agent identity (Principle 2)
    std::string agent_name;             // display / provenance only
    ir::SymbolRef target_ref;           // provenance / display only
    CoreBodyStorage storage;            // the handler bodies' shared ANF arenas
    std::vector<CoreFlowState> states;
    friend bool operator==(const CoreFlowDecl &, const CoreFlowDecl &) noexcept;
};

// ----------------------------------------------------------------------------
// Workflow (multi-agent DAG orchestration) — RFC 0026 KR6.4
// ----------------------------------------------------------------------------
//
// A workflow is a DAG of agent invocations. Each node targets an agent, depends
// on a set of upstream nodes (`after`), and computes its input from an ANF
// `input_region` that runs AFTER those dependencies are ready. Capability calls
// are a Flow-only language feature: the standalone verifier rejects one in a
// workflow region rather than inventing a workflow authorization source. The
// workflow return is a separate `return_region` evaluated after the DAG
// completes. Node identity is a typed `CoreWorkflowNodeId` (DAG-parallel),
// distinct from a flow's linear CoreValueId SSA domain. Verification-only
// `safety` / `liveness` temporal properties are ERASED (no field here).

/// One node of a workflow DAG. `target_instance` is the monomorphized agent
/// INSTANCE this node invokes (a CoreInstanceId into `CoreProgram::instances`;
/// resolve its nominal agent + output type through the CoreAgentInstance there).
/// `after` are the upstream nodes this node depends on (typed ids, resolved from
/// source names). `input_region` is the node's ANF input computation: it ends by
/// yielding the single value passed to the agent (RegionContext WorkflowNodeInput
/// in the verifier). Its statement subset is workflow-specific; capability calls
/// are rejected as outside Flow even though their signatures are checked.
struct CoreWorkflowNode {
    CoreWorkflowNodeId id{};
    CoreInstanceId target_instance{};        // invoked agent INSTANCE (Principle 2)
    std::string node_name;                   // display / provenance only
    ir::SymbolRef target_ref;                // provenance / display only
    std::vector<CoreWorkflowNodeId> after;   // typed dependency edges (no name strings)
    std::unique_ptr<CoreRegion> input_region; // ANF; ends in Yield(input value)
    friend bool operator==(const CoreWorkflowNode &, const CoreWorkflowNode &) noexcept;
};

/// The execution-layer projection of an `ir::WorkflowDecl`. Owns the per-workflow
/// pure-expression arena + value counter + pattern arena shared by all node input
/// regions and the return region. `input_type` / `output_type` are the workflow's
/// typed shell (both Struct, like an agent). `return_region` computes the workflow
/// output after the DAG completes (RegionContext WorkflowReturn), yielding the
/// output value. safety/liveness are erased.
struct CoreWorkflowDecl {
    CoreWorkflowId id{};
    std::string name;                         // display / provenance only
    ir::SymbolRef symbol_ref;                 // provenance / display only
    CoreTypeId input_type{};                  // workflow input struct
    CoreTypeId output_type{};                 // workflow output struct
    CoreBodyStorage storage;                  // shared node/return ANF arenas
    std::vector<CoreWorkflowNode> nodes;      // DAG nodes; index == CoreWorkflowNodeId
    std::unique_ptr<CoreRegion> return_region; // ANF; ends in Yield(output value)
    friend bool operator==(const CoreWorkflowDecl &, const CoreWorkflowDecl &) noexcept;
};

// ----------------------------------------------------------------------------
// Outlined fn bodies (CORE-FNBODY-DESIGN §2) — RFC 0026 FB-1 (KR6.6)
// ----------------------------------------------------------------------------
//
// A `CoreFnDecl` owns one monomorphized, pure top-level fn / impl-method body.
// The flat `CoreProgram::fns` store is 1:1 with the Fn-kind `CoreInstanceDecl`s
// that carry a body (a codegen unit: "does this id have a wasm body?"); the
// instance registry remains the monomorphization-registration unit (Capability
// imports and bodyless prototypes have an instance but never a fn decl). The
// fn's concrete (params, ret) signature is NOT stored twice: it is materialized
// by substituting the instance dispatch_types (the concrete type ARGUMENTS)
// into the base `FnTypeInfo` generic signature and interning the result into
// the body's pre-bound params / program value-type pool (design §2.1). Effect
// grade / decreases / temporal / contract are ERASED at this layer.

/// The execution-layer projection of one monomorphized `ir::FnDecl` body.
struct CoreFnDecl {
    CoreFnId id{};
    /// 1:1 link to the Fn-kind `CoreProgram::instances` entry this body
    /// instantiates. The instance's dispatch_types are the concrete type
    /// arguments (e.g. `map<Int,Int>` = [Int,Int]), NOT the fn signature.
    CoreInstanceId instance{};
    ir::SymbolRef origin;                 // provenance / display only
    /// Pre-bound body SSA values for the parameters in declaration order. Their
    /// concrete logical types live in `storage.value_types[param]`.
    std::vector<CoreValueId> params;
    /// RFC 0026 FB-3a1 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT / §3.2): the DECLARED
    /// env capture signature of this fn body when it is a lambda-lifted closure
    /// target — one program-global logical value type per environment slot, in
    /// canonical env-slot order (explicit capture list in source order, else
    /// first-use DFS order). The captured SSA values themselves live in the
    /// CONSTRUCTING body's domain (they become a `CoreClosureExpr.env` operand
    /// list there), never in this fn's body table, so only their logical types
    /// are declared here. Empty for an ordinary fn, which may still be wrapped by
    /// a zero-capture `CoreClosureExpr{fn, {}}` (a static fn taken as a
    /// first-class value). The verifier checks a `CoreClosureExpr`'s env count,
    /// per-slot operand types, and `CoreVtClosure` result type against THIS
    /// declared list — not only against the expr's own result type — so a wire
    /// artifact cannot forge a self-consistent capture pair.
    std::vector<CoreValueTypeId> captures;
    /// RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT): the pre-bound body SSA
    /// values for the declared environment slots, parallel to `captures`
    /// (`env_bindings.size() == captures.size()`). These are the in-body values a
    /// captured name reads. The wasm `env` pointer / slot loads are a codegen
    /// concern (FB-3b); at the Core SSA layer a capture is simply another
    /// pre-bound value, so these are deliberately NOT in `params` (the logical
    /// signature): a capture slot is never a caller-supplied argument. An
    /// ordinary fn (and a zero-capture closure) leaves this empty.
    std::vector<CoreValueId> env_bindings;
    CoreBodyStorage storage;              // the fn body's private SSA domain
    CoreRegion body;                      // single-entry region, completes via return
    std::string name;                     // display only (mangled instance name)
    SourceRangeOpt source_range;
    friend bool operator==(const CoreFnDecl &, const CoreFnDecl &) noexcept;
};

// ----------------------------------------------------------------------------
// Monomorphized instances (RFC 0026 KR6.4 slice: instance registry / dispatch
// identity consumption)
// ----------------------------------------------------------------------------
//
// The Typed-HIR -> AHFL-IR lowering already discovers every concrete
// `(nominal symbol, dispatch types)` instance and mangles it into an
// `ir::InstanceDecl` (dedup keyed by symbol + type args). Core-IR CONSUMES those
// decls into a flat `CoreProgram::instances` table — it does NOT re-discover or
// re-mangle. Each instance's canonical execution identity is its byte-exact
// `instance_key` (== `ir::InstanceDecl::name`); the in-IR identity is its
// `CoreInstanceId` index.
//
// `dispatch_types` are the vector `mangle_instance()` was given — a DISPATCH
// descriptor, NOT uniform generic type-args (its source differs per kind: a fn's
// concrete generic args, a capability/method's argument types, an agent's
// [input, context, output] shell). It is kept as concrete structural
// `CoreValueTypeId` in the same program-global P4 arena used by capability
// signatures and body SSA values, so primitive/bounded/container/Fn/
// parameterized types share one canonical representation;
// the verifier rejects any non-concrete (`Unresolved`/`Any`) dispatch type.
//
// The kind of an instance is a STRUCTURAL FACT of its payload variant (no
// separate drift-prone enum). Agent / Workflow payloads carry the concrete shell
// as resolved CoreTypeIds + a `base` back-reference into the nominal table;
// Capability carries its base id; Predicate / Fn keep only the origin symbol
// until their Core base tables land (Fn body lowering is a later slice).

/// A capability instantiated at concrete argument types. `base` is the nominal
/// capability it dispatches to.
struct CoreCapabilityInstance {
    CoreCapabilityId base{};
    [[nodiscard]] friend bool operator==(const CoreCapabilityInstance &,
                                         const CoreCapabilityInstance &) noexcept = default;
};

/// A predicate instance. No Core predicate table exists yet, so the origin
/// SymbolRef on the enclosing CoreInstanceDecl is the only identity carried.
struct CorePredicateInstance {
    [[nodiscard]] friend bool operator==(const CorePredicateInstance &,
                                         const CorePredicateInstance &) noexcept = default;
};

/// A concrete agent invocation instance. `base` is the nominal agent; the shell
/// (input / context / output) is the concrete schema this instance applies.
struct CoreAgentInstance {
    CoreAgentId base{};
    CoreTypeId input_type{};
    CoreAgentDecl::ContextKind context_kind{CoreAgentDecl::ContextKind::Unit};
    CoreTypeId context_type{};
    CoreTypeId output_type{};
    [[nodiscard]] friend bool operator==(const CoreAgentInstance &,
                                         const CoreAgentInstance &) noexcept = default;
};

/// A workflow instantiated as part of a larger composition. `base` is the
/// nominal workflow; input/output are its concrete shell.
struct CoreWorkflowInstance {
    CoreWorkflowId base{};
    CoreTypeId input_type{};
    CoreTypeId output_type{};
    [[nodiscard]] friend bool operator==(const CoreWorkflowInstance &,
                                         const CoreWorkflowInstance &) noexcept = default;
};

/// A top-level `fn` instantiated at concrete type args. When this instance
/// owns a body, `body` is the 1:1 reverse link into `CoreProgram::fns`
/// (CORE-FNBODY-DESIGN §2.1). A bodyless `@builtin` facade / prototype keeps
/// `body == kInvalid`; such an instance never gets a `CoreFnDecl` and stays on
/// the typed-builtin / fail-closed path.
struct CoreFnInstance {
    CoreFnId body{};
    [[nodiscard]] friend bool operator==(const CoreFnInstance &,
                                         const CoreFnInstance &) noexcept = default;
};

namespace core_node_detail {

// RFC 0026 P9 (KR6.9-B1): the instance-payload variant is generated from the
// core_instance_payload.def X-list, so the JSON `payload.kind` discriminator and
// the variant share ONE table (RFC 0026 P9 §3 rule 2).
#define HANDLE_CORE_INSTANCE_PAYLOAD(Name, Wire) ::ahfl::ir::node_detail::node_tag<Core##Name>{},
inline constexpr auto kCoreInstancePayloadTags = std::tuple{
#include "ahfl/compiler/ir/core_instance_payload.def"
};
#undef HANDLE_CORE_INSTANCE_PAYLOAD

/// JSON wire name of each CoreInstancePayload alternative, indexed by
/// `CoreInstancePayload::index()`. The ONE table the Core JSON writer and reader
/// resolve the `"kind"` spelling from.
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCoreInstancePayloadTags)>>>
    kCoreInstancePayloadWireNames = {
#define HANDLE_CORE_INSTANCE_PAYLOAD(Name, Wire) Wire,
#include "ahfl/compiler/ir/core_instance_payload.def"
};

} // namespace core_node_detail

using CoreInstancePayload = ::ahfl::ir::node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(core_node_detail::kCoreInstancePayloadTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see CoreDecl). The
// alternative count derives from core_instance_payload.def.
static_assert(std::variant_size_v<CoreInstancePayload> == 5,
              "ahfl::ir::core::CoreInstancePayload cardinality drift (RFC "
              "0027 P8 IR SSOT): update every exhaustive visitor, this pin, and "
              "core_instance_payload.def together.");

/// JSON wire name of a CoreInstancePayload alternative (e.g. `"capability"`),
/// indexed by `CoreInstancePayload::index()`.
[[nodiscard]] inline constexpr std::string_view
core_instance_payload_wire_name(std::size_t variant_index) noexcept {
    return core_node_detail::kCoreInstancePayloadWireNames[variant_index];
}

// ----------------------------------------------------------------------------
// Logical value types (RFC 0026 P4). A program-global, hash-consed arena of
// TARGET-INDEPENDENT value types — the "what a value logically is" layer,
// distinct from nominal declaration identity (`CoreTypeId`) and from physical
// layout (a separate P4-D projection). Covers `types::Type` MINUS TypeVar / Any
// / Error (those cannot survive monomorphization; `lower_value_type` fails
// closed if it sees one). NOT a 1:1 mirror: an enum VARIANT normalizes into its
// parent `CoreVtNominal` (a value's type is the enum, not a per-variant type),
// and `CoreVtClosure` is a Core-only execution type with no `types::Type`
// counterpart. Interned: index equality IS structural equality within the SAME
// arena (Principle 3).
// ----------------------------------------------------------------------------

/// The nominal ROLE of a `CoreTypeDecl`, so the verifier judges collection-only
/// facts (e.g. a bounded `capacity`) by a typed enum, NEVER by parsing a
/// canonical name. `Ordinary` = a user struct/enum with no collection semantics;
/// the rest are the well-known stdlib nominal generics. Filled from the single
/// builtin nominal descriptor SSOT (`builtin_nominal_table()`); a user nominal is
/// always `Ordinary`.
enum class CoreNominalRole { Ordinary, Option, Result, List, Set, Map };

/// RFC 0026 P4 (coercion): declaration-order variance of a nominal's type
/// parameter, materialized from the semantics layer's inferred variance and
/// carried across the AHFL-IR bridge. A `TypeArg` coercion step is legal ONLY at
/// a Covariant / Contravariant position; an Invariant position requires the
/// argument to be structurally equal. This is the SINGLE source of truth the
/// coercion verifier reads to pick a child's proof direction (F3) — the op
/// itself carries no variance.
enum class CoreVariance { Invariant, Covariant, Contravariant };

enum class CoreMemberTypeTemplateKind {
    Concrete,
    Param,
    Nominal,
    Fn
};

/// One node in a CoreTypeDecl's flat, compositional member-type template arena.
/// Field usage is kind-specific and fail-closed by the Core verifier:
/// Concrete uses `concrete`; Param uses `param_index`; Nominal uses
/// `nominal`/`capacity`/`children`; Fn uses `children`/`fn_return`.
struct CoreMemberTypeTemplateNode {
    CoreMemberTypeTemplateKind kind{CoreMemberTypeTemplateKind::Concrete};
    CoreValueTypeId concrete{};
    std::uint32_t param_index{0};
    CoreTypeId nominal{};
    std::optional<std::uint64_t> capacity;
    std::vector<CoreMemberTypeTemplateNodeId> children;
    CoreMemberTypeTemplateNodeId fn_return{};
    [[nodiscard]] friend bool operator==(const CoreMemberTypeTemplateNode &,
                                         const CoreMemberTypeTemplateNode &) noexcept = default;
};

/// `true` iff a nominal of this role may carry a bounded-collection `capacity`.
/// The SINGLE decision point shared by `lower_value_type` and
/// `verify_value_types` (Codex ruling c): capacity is legal ONLY on List/Set/Map;
/// Ordinary/Option/Result carrying a capacity is fail-closed at BOTH layers.
/// NOTE: "allowed" is not "required" — a `List<T>` with no capacity is a legal
/// logical type (unbounded); the P4-D layout pass fail-closes an unbounded
/// collection for a bounded target, not this predicate.
[[nodiscard]] constexpr bool capacity_allowed(CoreNominalRole role) noexcept {
    return role == CoreNominalRole::List || role == CoreNominalRole::Set ||
           role == CoreNominalRole::Map;
}

// scalars & atoms -----------------------------------------------------------
struct CoreVtUnit {
    [[nodiscard]] friend bool operator==(const CoreVtUnit &, const CoreVtUnit &) noexcept = default;
};
/// Uninhabited. Legal as an arena entry (e.g. a diverging expression's type),
/// but a CONSUMER-CONTEXT check rejects it as a materialized value / dispatch
/// type / shell (see `verify_value_types` and the instance dispatch verifier).
struct CoreVtNever {
    [[nodiscard]] friend bool operator==(const CoreVtNever &, const CoreVtNever &) noexcept =
        default;
};
struct CoreVtBool {
    [[nodiscard]] friend bool operator==(const CoreVtBool &, const CoreVtBool &) noexcept = default;
};
/// `Int` (unbounded) or `BoundedInt` (a refinement range). `bounds` present iff
/// the source was a bounded int; `min <= max` enforced by the verifier.
struct CoreVtInt {
    std::optional<std::pair<std::int64_t, std::int64_t>> bounds;
    [[nodiscard]] friend bool operator==(const CoreVtInt &, const CoreVtInt &) noexcept = default;
};
struct CoreVtFloat {
    [[nodiscard]] friend bool operator==(const CoreVtFloat &, const CoreVtFloat &) noexcept =
        default;
};
/// `String` or `BoundedString`. `length_bounds` present iff bounded; both bounds
/// non-negative and `min <= max`, enforced by the verifier.
struct CoreVtString {
    std::optional<std::pair<std::int64_t, std::int64_t>> length_bounds;
    [[nodiscard]] friend bool operator==(const CoreVtString &, const CoreVtString &) noexcept =
        default;
};
struct CoreVtDecimal {
    std::int64_t scale{0};
    [[nodiscard]] friend bool operator==(const CoreVtDecimal &, const CoreVtDecimal &) noexcept =
        default;
};
struct CoreVtDuration {
    [[nodiscard]] friend bool operator==(const CoreVtDuration &, const CoreVtDuration &) noexcept =
        default;
};
struct CoreVtTimestamp {
    [[nodiscard]] friend bool operator==(const CoreVtTimestamp &,
                                         const CoreVtTimestamp &) noexcept = default;
};
struct CoreVtUuid {
    [[nodiscard]] friend bool operator==(const CoreVtUuid &, const CoreVtUuid &) noexcept = default;
};

/// A nominal instance: `base` is the canonical in-Core nominal identity
/// (`CoreTypeId`), `args` are the concrete type arguments BY PARAMETER POSITION
/// (Rust `Substs`). A value type NEVER stores a `SymbolRef` — the resolved
/// nominal identity is resolved to a `CoreTypeId` ONCE in `lower_value_type`
/// (via the `ir::TypeRef::nominal_ref` bridge), so the three-layer identity table
/// cannot give one nominal two canonical representations. `capacity` is legal
/// ONLY on a bounded-collection role (see `capacity_allowed`).
struct CoreVtNominal {
    CoreTypeId base{};
    std::vector<CoreValueTypeId> args;
    std::optional<std::uint64_t> capacity;
    [[nodiscard]] friend bool operator==(const CoreVtNominal &, const CoreVtNominal &) noexcept =
        default;
};

/// An anonymous structural tuple.
struct CoreVtTuple {
    std::vector<CoreValueTypeId> elements;
    [[nodiscard]] friend bool operator==(const CoreVtTuple &, const CoreVtTuple &) noexcept =
        default;
};

/// A callable SIGNATURE. The effect grade of `types::FnT` is ERASED by
/// construction — Core-IR is the effect-lowered layer, so a function's
/// callability/effect is expressed structurally by the capability-call node, not
/// carried on the value type.
struct CoreVtFn {
    std::vector<CoreValueTypeId> params;
    CoreValueTypeId ret{};
    [[nodiscard]] friend bool operator==(const CoreVtFn &, const CoreVtFn &) noexcept = default;
};

/// How a closure captures an environment slot. Only `ByValue` is accepted today;
/// `ByRef` (with lifetime/region rules) is a future addition that does not change
/// the node shape.
enum class CoreCaptureMode { ByValue };

/// One captured environment slot of a closure. The capture's NAME does not enter
/// the logical type identity (only `value_type` + `mode` do) — the name is
/// closure-construction/debug provenance only.
struct CoreClosureCapture {
    CoreValueTypeId value_type{};
    CoreCaptureMode mode{CoreCaptureMode::ByValue};
    [[nodiscard]] friend bool operator==(const CoreClosureCapture &,
                                         const CoreClosureCapture &) noexcept = default;
};

/// A closure: a signature (must resolve to a `CoreVtFn`) PLUS its captured
/// environment slots in canonical env-slot order. NOTE: `lower_value_type` does
/// not YET produce a closure (P4-A has no closure consumer); the node exists so
/// the arena's node set is complete for later slices.
struct CoreVtClosure {
    CoreValueTypeId signature{};
    std::vector<CoreClosureCapture> captures;
    [[nodiscard]] friend bool operator==(const CoreVtClosure &, const CoreVtClosure &) noexcept =
        default;
};

// RFC 0027 Q1 (KR6.13-X): the variant alternative list and the diagnostic name
// table are BOTH generated from the single X-macro node list in
// core_value_types.def — one line per node, declaration order preserved. The
// tuple-tag indirection lives in node_tags.hpp (KR6.13-F), shared by every
// X-macro-generated IR variant.
namespace core_value_type_detail {

#define HANDLE_CORE_VT(Name, Wire) ::ahfl::ir::node_detail::node_tag<CoreVt##Name>{},
inline constexpr auto kCoreValueTypeTags = std::tuple{
#include "ahfl/compiler/ir/core_value_types.def"
};

/// Stable per-node diagnostic name, indexed by `CoreValueTypeNode::index()`.
/// Generated from the SAME .def list as the variant, so the table can never
/// silently miss or reorder a node.
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCoreValueTypeTags)>>>
    kCoreValueTypeNames = {
#define HANDLE_CORE_VT(Name, Wire) "CoreVt" #Name,
#include "ahfl/compiler/ir/core_value_types.def"
};

/// JSON `kind` wire name of each CoreValueTypeNode alternative, indexed by
/// `CoreValueTypeNode::index()` (RFC 0026 P9 §3). The ONE table the Core JSON
/// writer and reader resolve the `"kind"` spelling from.
inline constexpr std::array<
    std::string_view, std::tuple_size_v<std::remove_cvref_t<decltype(kCoreValueTypeTags)>>>
    kCoreValueTypeWireNames = {
#define HANDLE_CORE_VT(Name, Wire) Wire,
#include "ahfl/compiler/ir/core_value_types.def"
};

} // namespace core_value_type_detail

using CoreValueTypeNode = ::ahfl::ir::node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(core_value_type_detail::kCoreValueTypeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate (see CoreDecl). The node
// count itself now derives from core_value_types.def; this pin turns "a node was
// added to the .def" into a deliberate review event across every exhaustive
// visitor (layout / wire / lower / verify).
static_assert(std::variant_size_v<CoreValueTypeNode> == 14,
              "ahfl::ir::core::CoreValueTypeNode cardinality drift (RFC "
              "0027 P8 IR SSOT): update every exhaustive visitor "
              "(layout / wire / lower / verify), this pin, and "
              "core_value_types.def together.");

/// Stable diagnostic name of the value-type alternative at `variant_index`
/// (e.g. `"CoreVtNominal"`). `variant_index` is a `CoreValueTypeNode::index()`
/// value and therefore always in bounds.
[[nodiscard]] inline constexpr std::string_view
core_value_type_name(std::size_t variant_index) noexcept {
    return core_value_type_detail::kCoreValueTypeNames[variant_index];
}

/// Stable diagnostic name of a value-type node (e.g. `"CoreVtClosure"`).
[[nodiscard]] inline std::string_view
core_value_type_name(const CoreValueTypeNode &node) noexcept {
    return core_value_type_name(node.index());
}

/// JSON `kind` wire name of the value-type alternative at `variant_index`
/// (e.g. `"nominal"`); the SSP for the Core JSON writer and reader (RFC 0026 P9
/// §3).
[[nodiscard]] inline constexpr std::string_view
core_value_type_wire_name(std::size_t variant_index) noexcept {
    return core_value_type_detail::kCoreValueTypeWireNames[variant_index];
}

/// An interned logical value type (`CoreProgram::value_types`). Structural
/// equality of the node IS the interning key: two `CoreValueType`s compare equal
/// iff their nodes do, and the hash-cons guarantees at most one arena entry per
/// distinct node. `index == CoreValueTypeId`.
struct CoreValueType {
    CoreValueTypeNode node;
    [[nodiscard]] friend bool operator==(const CoreValueType &, const CoreValueType &) noexcept =
        default;
};

/// Hash for the program-global value-type hash-cons map. Children are already
/// interned to ids, so a node's hash mixes only its own scalar fields + child
/// ids (never a pointer or a string). Exposed so the ONE interner below and the
/// lowerer's arena share a single hash implementation (CLAUDE.md: no parallel
/// SSOT copies). RFC 0027 Q1 (KR6.13-X) generates one handler per value-type
/// node from core_value_types.def; there is no unnamed catch-all, so a 15th node
/// without a routing macro fails to compile.
struct CoreValueTypeNodeHash {
    [[nodiscard]] std::size_t operator()(const CoreValueType &vt) const noexcept;
};

/// The program-global value-type HASH-CONS (RFC 0026 P9 §6). It owns no
/// storage: it indexes a caller-supplied `std::vector<CoreValueType>` and maps a
/// structural node to its interned `CoreValueTypeId`. This is the SINGLE
/// interning decision point shared by the AHFL-IR lowerer (which fills
/// `CoreProgram::value_types` while lowering) and the Core JSON reader (which
/// REBUILDS the arena by interning rather than trusting serialized ids, §6.2).
///
/// Deterministic append-only storage plus structural dedup: two structurally
/// identical nodes always yield the SAME id, and a node is appended at the next
/// free index. The constructor re-indexes any pre-existing entries, so repeated
/// construction over a partially-filled store keeps deduplicating against
/// everything already interned and the arena stays canonical.
class CoreValueTypeArena {
  public:
    explicit CoreValueTypeArena(std::vector<CoreValueType> &store) : store_(store) {
        index_.reserve(store_.size());
        for (std::uint32_t i = 0; i < store_.size(); ++i) {
            index_.emplace(store_[i], CoreValueTypeId{i});
        }
    }

    /// Intern a fully-built node whose children are ALREADY interned. Returns
    /// the canonical id, or `nullopt` with `*error_reason` set when the arena
    /// would exceed the 32-bit id space (fail-closed).
    [[nodiscard]] std::optional<CoreValueTypeId> intern(CoreValueTypeNode node,
                                                        std::string *error_reason);

    [[nodiscard]] std::size_t size() const noexcept { return store_.size(); }

  private:
    std::vector<CoreValueType> &store_;
    std::unordered_map<CoreValueType, CoreValueTypeId, CoreValueTypeNodeHash> index_;
};


/// One monomorphized instance consumed from an `ir::InstanceDecl`. `instance_key`
/// is the byte-exact mangled name (execution dispatch label + global-uniqueness
/// key — NEVER re-derived or string-parsed in Core). `payload` makes kind a
/// structural fact.
struct CoreInstanceDecl {
    CoreInstanceId id{};
    std::string instance_key;                // == ir::InstanceDecl::name (byte-exact)
    ir::SymbolRef origin;                    // nominal symbol that was instantiated
    /// The mangle dispatch descriptor as interned logical value types (RFC 0026
    /// P4). Each id indexes THIS program's `value_types` arena; a
    /// structurally-concrete type by construction (`lower_value_type` fails closed
    /// otherwise). See `operator==`: because these ids are arena-relative, instance
    /// equality is SAME-OWNER-ARENA equality.
    std::vector<CoreValueTypeId> dispatch_types;
    CoreInstancePayload payload;
    /// SAME-OWNER-ARENA equality: `dispatch_types` are compared as raw
    /// `CoreValueTypeId`s, which are only meaningful within one program's
    /// `value_types` arena. Two instances from different programs must NOT be
    /// compared with this operator (use a future arena-aware comparator).
    friend bool operator==(const CoreInstanceDecl &, const CoreInstanceDecl &) noexcept;
};

// ----------------------------------------------------------------------------
// Core-IR program
// ----------------------------------------------------------------------------

/// A nominal type in the execution layer's type table (`CoreProgram::types`),
/// addressed by `CoreTypeId`. Fields (structs) and variants (enums) are keyed by
/// declaration-order index — the canonical `CoreFieldId` / variant index.
struct CoreTypeDecl {
    enum class Kind { Struct, Enum };
    Kind kind{Kind::Struct};
    std::string name;                       // canonical name (display + provenance)
    std::vector<std::string> fields;        // struct field names; index == CoreFieldId
    /// NAVIGATION ONLY (parallel to `fields`): the field's nominal CoreTypeId,
    /// or kInvalid when a member chain cannot advance through it. This is NOT
    /// the field's logical type and MUST NOT be consumed by layout/codegen; use
    /// field_type_template_roots + member_type_templates for that. Populated by
    /// a forward-reference fixup after the complete nominal table is registered.
    std::vector<CoreTypeId> field_nominal_types;
    /// Whether each struct field has a default value (parallel to `fields`). A
    /// field WITHOUT a default is REQUIRED: a struct-literal constructor must
    /// assign it. Lets the verifier prove struct-literal completeness.
    std::vector<bool> field_has_default;
    std::vector<std::string> variants;      // enum variant names; index == variant id
    /// Per-variant payload metadata (parallel to `variants`; empty for a struct).
    /// The verifier bounds a variant constructor's / pattern's payload against
    /// the declared arity, and a backend reads the payload types for layout.
    struct VariantPayload {
        enum class Kind { Unit, Tuple, Struct };
        Kind kind{Kind::Unit};
        /// Logical payload slot template roots in declaration order. For a
        /// Struct payload this is parallel to `field_names`; Unit has none.
        std::vector<CoreMemberTypeTemplateNodeId> slot_type_template_roots;
        /// Struct-payload field names (index == slot); empty for Unit/Tuple.
        std::vector<std::string> field_names;
        [[nodiscard]] friend bool operator==(const VariantPayload &,
                                             const VariantPayload &) noexcept = default;
    };
    std::vector<VariantPayload> variant_payloads;
    /// Declaration-owned, postordered logical member-type template arena.
    std::vector<CoreMemberTypeTemplateNode> member_type_templates;
    /// Parallel to struct `fields`; empty for enums.
    std::vector<CoreMemberTypeTemplateNodeId> field_type_template_roots;
    /// Number of generic type parameters this nominal declares (RFC 0026 P4).
    /// `0` for a non-generic user type; the std generics carry their arity
    /// (Option 1, Result 2, List 1, Set 1, Map 2). A `CoreVtNominal` referencing
    /// this base must supply exactly `type_param_count` args (verifier-enforced).
    std::uint32_t type_param_count{0};
    /// RFC 0026 P4 (coercion): declaration-order variance of each type parameter
    /// (parallel: `variances.size() == type_param_count` in a well-formed decl).
    /// Filled from the AHFL-IR decl's `type_param_variances` for a user nominal
    /// and cross-checked against the builtin descriptor SSOT for a std generic.
    /// Empty for a non-generic nominal. The coercion verifier (F3) reads this to
    /// judge a `TypeArg` step's legality + proof direction.
    std::vector<CoreVariance> variances;
    /// The nominal's role (RFC 0026 P4). `Ordinary` for a user type; the std
    /// generics carry their well-known role so `capacity` legality is judged by a
    /// typed enum, never by parsing `name`. Filled from the builtin nominal
    /// descriptor SSOT.
    CoreNominalRole role{CoreNominalRole::Ordinary};
    /// Resolved-symbol provenance of this nominal (RFC 0026 P4 / Principle 2): the
    /// SymbolRef the decl was registered under, so value-type lowering can resolve
    /// a `nominal_ref` to this CoreTypeId by SYMBOL ID first (not canonical
    /// string), matching the production TypeEnv id-first path. A synthetic std
    /// base that has no user declaration carries a name-only ref (kind=Type,
    /// canonical set, id absent).
    ir::SymbolRef symbol_ref{};
    /// Declaration provenance retained for target-side diagnostics such as a
    /// malformed/infinite member layout. Synthetic builtin shells have no
    /// source range.
    SourceRangeOpt source_range;
    [[nodiscard]] friend bool operator==(const CoreTypeDecl &,
                                         const CoreTypeDecl &) noexcept = default;
};

/// A complete Core-IR compilation unit — the execution layer's program.
///
/// Dedicated flat stores (Principle 3) replace the earlier `variant`-of-decls:
/// types are addressed by `CoreTypeId`, capabilities by `CoreCapabilityId`,
/// agents by `CoreAgentId`; flows keep source order for determinism.
struct CoreProgram {
    std::string format_version{std::string(kCoreFormatVersion)};
    std::vector<CoreTypeDecl> types;              // index == CoreTypeId
    std::vector<CoreValueType> value_types;       // index == CoreValueTypeId (interned, P4)
    std::vector<CoreCapabilityDecl> capabilities; // index == CoreCapabilityId
    std::vector<CoreAgentDecl> agents;            // index == CoreAgentId
    std::vector<CoreFlowDecl> flows;
    std::vector<CoreWorkflowDecl> workflows;      // index == CoreWorkflowId
    std::vector<CoreInstanceDecl> instances;      // index == CoreInstanceId
    /// RFC 0026 FB-1 (CORE-FNBODY-DESIGN §2.1): outlined monomorphized fn
    /// bodies; index == CoreFnId. 1:1 with the Fn-kind `instances` that own a
    /// body (`CoreFnInstance::body` back-links each entry).
    std::vector<CoreFnDecl> fns;
};

// ----------------------------------------------------------------------------
// Lower entry: AhflIr -> Core-IR
// ----------------------------------------------------------------------------

/// Severity of a lowering diagnostic. Only `Error` makes the program
/// non-executable; `Warning` is retained but does not gate backend consumption.
enum class CoreDiagnosticSeverity { Error, Warning };

/// Stable diagnostic codes emitted by the AHFL-IR -> Core-IR lowering. Kept as
/// named constants (the layer's diagnostics catalogue) so the lowerer and tests
/// share one source of truth rather than duplicating call-site string literals.
namespace diag {
inline constexpr std::string_view kUnresolvedCapabilityCall = "core.UNRESOLVED_CAPABILITY_CALL";
inline constexpr std::string_view kUnresolvedCapabilitySignature =
    "core.UNRESOLVED_CAPABILITY_SIGNATURE";
inline constexpr std::string_view kUnresolvedAgentCapability =
    "core.UNRESOLVED_AGENT_CAPABILITY";
inline constexpr std::string_view kDuplicateAgentCapability =
    "core.DUPLICATE_AGENT_CAPABILITY";
inline constexpr std::string_view kUnresolvedType = "core.UNRESOLVED_TYPE";
inline constexpr std::string_view kUnresolvedEnumVariant = "core.UNRESOLVED_ENUM_VARIANT";
inline constexpr std::string_view kUnresolvedStructField = "core.UNRESOLVED_STRUCT_FIELD";
inline constexpr std::string_view kUnresolvedQualifiedValue = "core.UNRESOLVED_QUALIFIED_VALUE";
inline constexpr std::string_view kUnresolvedFlowTarget = "core.UNRESOLVED_FLOW_TARGET";
inline constexpr std::string_view kUnknownHandlerState = "core.UNKNOWN_HANDLER_STATE";
inline constexpr std::string_view kUnknownGotoTarget = "core.UNKNOWN_GOTO_TARGET";
inline constexpr std::string_view kUnloweredStatement = "core.UNLOWERED_STATEMENT";
inline constexpr std::string_view kUnloweredExpression = "core.UNLOWERED_EXPRESSION";
inline constexpr std::string_view kUnloweredFieldProjection = "core.UNLOWERED_FIELD_PROJECTION";
inline constexpr std::string_view kEffectfulUnsupported = "core.EFFECTFUL_UNSUPPORTED";
inline constexpr std::string_view kNullExpr = "core.NULL_EXPR";
inline constexpr std::string_view kUnresolvedWorkflowTarget = "core.UNRESOLVED_WORKFLOW_TARGET";
inline constexpr std::string_view kUnknownWorkflowDependency = "core.UNKNOWN_WORKFLOW_DEPENDENCY";
inline constexpr std::string_view kWorkflowCycle = "core.WORKFLOW_CYCLE";
inline constexpr std::string_view kDuplicateInstanceKey = "core.DUPLICATE_INSTANCE_KEY";
inline constexpr std::string_view kUnresolvedInstanceBase = "core.UNRESOLVED_INSTANCE_BASE";
inline constexpr std::string_view kUnknownInstanceKind = "core.UNKNOWN_INSTANCE_KIND";
inline constexpr std::string_view kUnresolvedWorkflowInvocation = "core.UNRESOLVED_WORKFLOW_INVOCATION";
inline constexpr std::string_view kMissingAdjustment = "core.MISSING_ADJUSTMENT";
inline constexpr std::string_view kInvalidCoercion = "core.INVALID_COERCION";
inline constexpr std::string_view kInvalidMemberTemplate = "core.INVALID_MEMBER_TEMPLATE";
// RFC 0026 FB-1 (CORE-FNBODY-DESIGN §8.2): a fn / method call could not be
// resolved by identity to a body-bearing monomorphized Fn instance (bodyless
// prototype, unresolved symbol, or a shape this slice does not lower).
inline constexpr std::string_view kUnresolvedFnCall = "core.UNRESOLVED_FN_CALL";
// A fn body contains a statement / expression this slice does not yet lower
// (fail-closed: never an Unknown node).
inline constexpr std::string_view kFnBodyUnlowered = "core.FN_BODY_UNLOWERED";
// The resolved callee fn body carries a capability effect; effectful callees
// stay fail-closed until FB-4's ordered CoreCallStmt.
inline constexpr std::string_view kFnEffectfulCallee = "core.FN_EFFECTFUL_CALLEE";
// A Nondet fn value call in the pure computation lane (no value-level
// nondeterminism semantics on the wasm lane yet).
inline constexpr std::string_view kNondetFnValue = "core.NONDET_FN_VALUE";
// A parameter / return crosses the fn boundary as a multi-word value
// (PtrLen String / bytes / f64) before the P6-7 frame decision.
inline constexpr std::string_view kFnCrossBoundaryType = "core.FN_CROSS_BOUNDARY_TYPE";
// RFC 0026 P4 (coercion): a real (non-synthetic) declaration of a well-known
// stdlib generic (Option/Result/List/Set/Map) whose arity or per-parameter
// variance metadata disagrees with the builtin descriptor SSOT. Fail-closed:
// the incoming metadata must match the descriptor exactly (a synthetic base is
// stamped from it; a user nominal is consumed verbatim; only a real std decl is
// cross-checked here).
inline constexpr std::string_view kBuiltinMetadataDrift = "core.BUILTIN_METADATA_DRIFT";
// RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.2): a lambda's implicit-capture
// analysis reached a reference that is neither a local binding at the
// construction site nor a liftable frame projection.
inline constexpr std::string_view kClosureCaptureInvalid = "core.CLOSURE_CAPTURE_INVALID";
// A lambda explicitly captures a name that is not a visible outer binding
// (§3.2: the explicit list is authoritative, in source order).
inline constexpr std::string_view kClosureCaptureUnknown = "core.CLOSURE_CAPTURE_UNKNOWN";
// A closure value flows into a callable slot whose signature differs, or a
// signature-typed callable is narrowed to a concrete closure (§3.1.1 D-FNREP).
inline constexpr std::string_view kFnCallableTypeIncompatible =
    "core.FN_CALLABLE_TYPE_INCOMPATIBLE";
} // namespace diag

/// A structured lowering diagnostic (fail-closed: no throw, no Unknown node).
struct CoreLowerDiagnostic {
    CoreDiagnosticSeverity severity{CoreDiagnosticSeverity::Error};
    std::string code;          // one of the `diag::` codes above
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

/// RFC 0026 KR6.5 E4-B0-C2: the type-table-only projection of an AHFL program's
/// nominal universe, factored out of `lower_ahfl_to_core` so the wire-schema
/// migration projector can seed a private value-type arena WITHOUT lowering any
/// capability / agent / flow / workflow body. `types()` + `value_types()` are
/// exactly the arenas `lower_ahfl_to_core` builds in its type-table pass (same
/// source order, field-nav fixup, and P4-C member-template finalization); the
/// full lowerer consumes the SAME construction path, so this is not a second type
/// registration.
///
/// This is a constructor-guarded, copy-only immutable handle: only
/// `build_core_type_environment` can mint one, and only after the TYPE-LOCAL
/// structural gate passed. There is therefore no post-gate mutable state — a
/// holder of this type ALWAYS carries a verified type environment (a caller
/// cannot rewrite `types` to another self-consistent-but-different definition and
/// keep a stale "ok"). The payload is `shared_ptr<const>`; no move ctor/assignment
/// is declared, so an rvalue copies the cheap pointer (no moved-from state).
class VerifiedCoreTypeEnvironment {
  public:
    VerifiedCoreTypeEnvironment(const VerifiedCoreTypeEnvironment &) = default;
    VerifiedCoreTypeEnvironment &operator=(const VerifiedCoreTypeEnvironment &) = default;
    // Intentionally NO move ctor/assignment: copy-only immutable handle.

    [[nodiscard]] const std::vector<CoreTypeDecl> &types() const noexcept {
        return payload_->types;
    }
    [[nodiscard]] const std::vector<CoreValueType> &value_types() const noexcept {
        return payload_->value_types;
    }

  private:
    friend struct CoreTypeEnvironmentFactory;
    struct Payload {
        std::vector<CoreTypeDecl> types;
        std::vector<CoreValueType> value_types;
    };
    explicit VerifiedCoreTypeEnvironment(std::shared_ptr<const Payload> payload)
        : payload_(std::move(payload)) {}

    std::shared_ptr<const Payload> payload_;
};

/// Result of `build_core_type_environment`: a verified environment iff the
/// type-local gate produced no ERROR, plus the diagnostics either way. A consumer
/// that sees `!ok()` must fail closed (no partial artifact).
struct CoreTypeEnvironmentResult {
    std::optional<VerifiedCoreTypeEnvironment> environment;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        for (const auto &d : diagnostics) {
            if (d.severity == CoreDiagnosticSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool ok() const noexcept {
        return environment.has_value() && !has_errors();
    }
};

/// Build the shared, verified type-table-only environment from a verified AHFL
/// program. Lowers NO body. This is the single registration/fixup/finalize path —
/// `lower_ahfl_to_core` runs the identical steps in-place, so a program that
/// lowers cleanly and this environment agree on `types` / `value_types`
/// byte-for-byte. The TYPE-LOCAL structural gate (`verify_types` +
/// `verify_value_types` over empty body shells) runs before an environment is
/// minted; on any ERROR the result carries diagnostics and no environment.
[[nodiscard]] CoreTypeEnvironmentResult build_core_type_environment(const AhflIr &ahfl_ir);

/// A well-known stdlib enum the lowerer resolves via a builtin variant table
/// (its EnumDecl lives in the sysroot and may not be inlined in a program).
struct BuiltinEnumDescriptor {
    /// One variant: its name plus payload shape. `Option::Some` / `Result::Ok` /
    /// `Result::Err` are Tuple payloads of arity 1; `None` is Unit (arity 0).
    /// Each payload slot names its owning generic parameter position, so kind,
    /// arity, and logical payload template come from one SSOT.
    struct Variant {
        std::string_view name;
        CoreTypeDecl::VariantPayload::Kind payload_kind{CoreTypeDecl::VariantPayload::Kind::Unit};
        /// Declaration-order payload slot -> owning enum type-parameter index.
        /// Its size is the payload arity; this is the builtin template SSOT.
        std::vector<std::uint32_t> payload_type_params;
    };
    std::string_view name;               // unqualified enum name
    std::vector<Variant> variants;       // declaration order
};

/// The SINGLE builtin nominal descriptor SSOT (RFC 0026 P4, Codex invariant 3).
/// Every well-known stdlib nominal generic — Option / Result / List / Set / Map —
/// is described here ONCE: its canonical name, `CoreNominalRole`, Struct-vs-Enum
/// kind, generic arity, and (for enums) its variant metadata. `builtin_enum_table`
/// is a COMPATIBILITY VIEW projected from this table (enum entries only), so there
/// is no separate "IR enum table + IR collection table". The semantics-layer
/// container matcher (`stdlib_bridge`) is kept in sync with this SSOT by a strict
/// sync test rather than being a third independent source of truth.
struct BuiltinNominalDescriptor {
    std::string_view canonical_name;             // fully-qualified, e.g. "std::option::Option"
    std::string_view enum_view_name;             // unqualified name for the enum compat view ("" if not an enum)
    CoreTypeDecl::Kind kind{CoreTypeDecl::Kind::Enum};
    CoreNominalRole role{CoreNominalRole::Ordinary};
    std::uint32_t type_param_count{0};
    std::vector<BuiltinEnumDescriptor::Variant> variants; // enum variants (empty for a struct)
    /// RFC 0026 P4 (coercion): declaration-order variance of each type parameter
    /// (size == type_param_count). Stdlib containers/enums are covariant in their
    /// element/payload positions; Map's key is invariant. This is the authority a
    /// real std decl's `type_param_variances` must match exactly (drift is
    /// fail-closed) and a synthetic base is stamped from.
    std::vector<CoreVariance> variances;
};

/// The builtin nominal descriptor SSOT. Order is deterministic (declaration
/// order used by `add_builtins`). Guarded against sysroot drift by a sync test.
[[nodiscard]] const std::vector<BuiltinNominalDescriptor> &builtin_nominal_table();

/// The production builtin variant order. Exposed so a sync test can compare it
/// against the actual sysroot declaration order (std/option.ahfl,
/// std/result.ahfl) and fail if the two ever drift apart. COMPATIBILITY VIEW:
/// projected from `builtin_nominal_table()` (enum entries only).
[[nodiscard]] const std::vector<BuiltinEnumDescriptor> &builtin_enum_table();

/// Intern an `ir::TypeRef` into a program's logical value-type arena
/// (`CoreProgram::value_types`), resolving nominal bases against the program's
/// own `types` table (id-first via the `nominal_ref` bridge, then canonical
/// name). Returns the interned `CoreValueTypeId`, or `nullopt` with `*reason` set
/// on any fail-closed input (Unresolved/Any/Never, malformed shape, unresolved
/// nominal, arity/capacity violation). This is the SINGLE entry point shared by
/// the dispatch-type lowering (P4-A2) and its tests; repeated calls reuse the
/// arena's hash-cons so a structurally identical type always yields the SAME id.
///
/// Deterministic: for a fixed program, the same input sequence produces the same
/// arena order (required for same-owner-arena `CoreInstanceDecl` equality).
[[nodiscard]] std::optional<CoreValueTypeId>
lower_value_type_into(CoreProgram &program, const ir::TypeRef &type, std::string *reason);

/// Instantiate one declaration-owned member type template with concrete owning
/// nominal arguments. The result is interned in `program.value_types`; repeated
/// instantiation therefore returns the same id. This substitutes only the
/// template tree and never expands nominal fields (recursive layout is P4-D).
[[nodiscard]] std::optional<CoreValueTypeId>
instantiate_member_template(CoreProgram &program,
                            CoreTypeId owner,
                            CoreMemberTypeTemplateNodeId root,
                            const std::vector<CoreValueTypeId> &owner_args,
                            std::string *reason);

/// Supplied-arena form of `instantiate_member_template`. This is the same P4-C
/// evaluator, factored so the P4-D layout side pass can seed a private copy of
/// the logical value-type arena and compute its member closure without mutating
/// CoreProgram. `value_types` must use ids in the same domain as `types` and
/// `owner_args`; successful materialization hash-conses into that supplied
/// vector only.
[[nodiscard]] std::optional<CoreValueTypeId>
instantiate_member_template_into(std::vector<CoreValueType> &value_types,
                                 const std::vector<CoreTypeDecl> &types,
                                 CoreTypeId owner,
                                 CoreMemberTypeTemplateNodeId root,
                                 const std::vector<CoreValueTypeId> &owner_args,
                                 std::string *reason);

} // namespace ahfl::ir::core
