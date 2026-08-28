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
// Core-IR program
// ----------------------------------------------------------------------------

/// A complete Core-IR compilation unit — the execution layer's program.
struct CoreProgram {
    std::string format_version{std::string(kCoreFormatVersion)};
    /// Flat declaration store (Principle 3). Index is the declaration's
    /// canonical position; order mirrors the source `AhflIr` for determinism.
    std::vector<CoreDecl> declarations;
};

// ----------------------------------------------------------------------------
// Lower entry: AhflIr -> Core-IR (scaffold)
// ----------------------------------------------------------------------------

/// Lower the verification / orchestration layer (`AhflIr`) to the execution
/// layer (`CoreProgram`).
///
/// SCOPE (KR6.4 sub-slices so far): lowers each `ir::AgentDecl`'s state machine
/// into a `CoreAgentDecl` (erasing contract / temporal / decreases and quota —
/// verification-layer concerns) and each `ir::CapabilityDecl` into a
/// `CoreCapabilityDecl` (the explicit capability-call = `ahfl_cap` import
/// boundary; the effect spec beyond its kind is erased). All other declaration
/// kinds (structs, enums, contracts, flows, workflows, fns, traits, impls, …)
/// are skipped in this increment — their execution-layer lowering
/// (monomorphized bodies, structured control flow, memory layout) is filled by
/// the later KR6.4 sub-slices.
///
/// Deterministic: declarations are visited in source order and emitted in that
/// order; the instance key comes from the deterministic `mangle_instance`.
[[nodiscard]] CoreProgram lower_ahfl_to_core(const AhflIr &ahfl_ir);

} // namespace ahfl::ir::core
