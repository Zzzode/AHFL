#pragma once

// ---------------------------------------------------------------------------
// AHFL IR Tower — layer identity and boundary (RFC 0026, slice P1 / KR6.2)
// ---------------------------------------------------------------------------
//
// RFC 0026 replaces the historical single-layer `ir::Program` with a three-layer
// IR tower. This header establishes the tower's *identity* in the type system —
// a compile-time tag per layer plus the documented membership of each existing
// IR construct — WITHOUT moving any code. It is a zero-behavior-change skeleton:
// nothing consumes these tags yet, and every backend still consumes
// `ir::Program` through the existing path. Later slices (KR6.3+) migrate the
// concrete node sets onto these layers.
//
// The three layers and their consumers (RFC 0026 Design):
//
//   TypedHir  — diagnostics layer. Source-faithful, fully typed, pre-
//               monomorphization. Lives in `ahfl::semantics` (typed_hir.hpp);
//               consumed by diagnostics / LSP / const-eval.
//   AhflIr    — verification / orchestration layer. agent / flow / workflow /
//               contract / effect / temporal / decreases are first-class;
//               generics NOT monomorphized. Consumed by verification backends
//               (SMV / SMT-BMC) and view backends (K8s / Terraform / OpenAPI).
//   CoreIr    — execution layer. Monomorphized; effect lowered to explicit
//               capability-calls; temporal / contract / decreases erased;
//               control flow structured for WASM; explicit ADT / closure memory
//               layout. Consumed by the execution backend (WASM codegen) and,
//               transitionally, the tree-walking evaluator (retired after WASM
//               conformance, RFC 0026 KR6.8).
//
// Verification path consumes AhflIr; execution path consumes CoreIr. The two
// paths fork on the tower (Dafny-style), so temporal/contract never pollute the
// execution layer and monomorphization/memory-layout never pollute the
// verification layer.

#include <cstdint>

namespace ahfl::ir::tower {

/// The three layers of the AHFL IR tower, in lowering order. Index-based
/// identity (AGENTS.md Principle 2): the enum value is the canonical layer id;
/// the name is diagnostic-only.
enum class Layer : std::uint8_t {
    /// Diagnostics layer (source-faithful, fully typed, pre-monomorphization).
    /// Realized today by `ahfl::semantics::TypedProgram`.
    TypedHir = 0,
    /// Verification / orchestration layer. Consumed by verification + view
    /// backends. Realized today by the orchestration + verification subset of
    /// `ir::Program`; purified into a dedicated layer by RFC 0026 KR6.3.
    AhflIr = 1,
    /// Execution layer. Monomorphized, verification constructs erased, memory
    /// layout explicit, control flow WASM-structured. Introduced by RFC 0026
    /// KR6.4 and consumed by WASM codegen (KR6.5/6.6).
    CoreIr = 2,
};

/// Which tower path a layer feeds. The tower forks: AhflIr feeds verification,
/// CoreIr feeds execution (RFC 0026 Design "路径分叉").
enum class Path : std::uint8_t {
    /// Diagnostics/analysis — not an emit path; TypedHir sits above the fork.
    Diagnostics = 0,
    /// Verification + view backends (SMV / SMT-BMC / K8s / Terraform / OpenAPI).
    Verification = 1,
    /// Execution backend (WASM codegen; transitional evaluator).
    Execution = 2,
};

/// The path a given tower layer feeds. Encodes the Dafny-style fork as a pure
/// compile-time function so later slices can static_assert consumer/layer
/// agreement (e.g. "the WASM backend only consumes a CoreIr-tagged program").
[[nodiscard]] constexpr Path path_of(Layer layer) noexcept {
    switch (layer) {
    case Layer::TypedHir:
        return Path::Diagnostics;
    case Layer::AhflIr:
        return Path::Verification;
    case Layer::CoreIr:
        return Path::Execution;
    }
    return Path::Diagnostics; // unreachable; enum is exhaustive above
}

/// Compile-time layer tag. Later slices parameterize IR container types by a
/// `LayerTag<L>` so that "which layer this IR belongs to" is carried in the
/// type — making a consumer that reads the wrong layer a compile error rather
/// than a runtime check. Empty (zero-cost) today.
template <Layer L>
struct LayerTag {
    static constexpr Layer layer = L;
    static constexpr Path path = path_of(L);
};

using TypedHirTag = LayerTag<Layer::TypedHir>;
using AhflIrTag = LayerTag<Layer::AhflIr>;
using CoreIrTag = LayerTag<Layer::CoreIr>;

} // namespace ahfl::ir::tower
