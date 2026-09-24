#pragma once

// ---------------------------------------------------------------------------
// Fn direct-call recursion-depth lattice (RFC 0026 FB-2 /
// CORE-FNBODY-DESIGN §8.1 rule 6, §5.4).
// ---------------------------------------------------------------------------
//
// AHFL fn bodies have no loop statement: the ONLY way a pure fn body repeats
// work is recursion through a static `CoreCallExpr` edge OR through an indirect
// `CoreCallClosureExpr` edge (FB-3b call_indirect, design §5.2/§6.2). On the
// wasm MVP target both are realized with a native frame (a plain `call` or
// `call_indirect`), so the compiler must seal the call depth at COMPILE TIME
// instead of relying on tail call optimization or a runtime stack guard.
//
// The termination MEASURE (`decreases`) is deliberately NOT read here: it is
// erased before Core (termination was already consumed at the AHFL-IR layer).
// The bound is derived independently from STRUCTURAL Core-ANF facts. Formal
// rule:
//
//   Invocation graph G over CoreProgram::fns; compute SCCs. A direct edge is a
//   CoreCallExpr; an INDIRECT edge is one possible target of a
//   CoreCallClosureExpr. The possible-target set of a closure call is computed
//   conservatively as EVERY fn whose address a CoreClosureExpr takes anywhere
//   in the program and whose interned CoreVtFn signature structurally matches
//   the call site (one closure call site therefore contributes 1..N edges, N =
//   matching targets). A nontrivial SCC (self edge, or >= 2 members) is a
//   recursion group.
//
//   For every member f of a recursion group C, one Int-typed RANK PARAMETER
//   r_f (some slot of f.params) must satisfy:
//
//   (R1 progression) every internal edge e: f -> g in C binds g's rank slot to
//       r_f +/- c, c a positive compile-time constant, with ONE direction for
//       the whole group (uniform ascending or uniform descending).
//
//   (R2 base guard) f's body has a divergent `if` whose condition compares the
//       rank with a bounded expression B_f: an integer literal, a length word
//       (CoreCollectionExpr::Len) of an INVARIANT container parameter, or an
//       invariant scalar parameter. Non-rank parameters the guard references
//       must be threaded VERBATIM on every internal edge (same SSA value at
//       the matching slot).
//
//   (R3 entries)    every edge entering C (from a flow handler, a workflow
//       region, or an fn outside C) binds the rank slot to a value with a
//       STATIC interval, and every invariant slot the guard reads is statically
//       bounded there too. A value is statically bounded iff it is an integer
//       literal, a length word of a value whose LOGICAL type is a bounded
//       nominal container (CoreVtNominal.capacity present, role List/Set/Map),
//       or an affine add/sub combination of those. Concrete monomorphized fn
//       bodies carry the capacity on the parameter type itself, so the
//       inductive evidence threads through arbitrary non-recursive call chains
//       without a second pass. Runtime String lengths, aggregate fields, call
//       results, or bare unbounded parameters are NOT static evidence.
//
//   The SCC's sealed static depth is
//       ascending : 1 + ceil((max B_f - min entry_rank) / min step c)
//       descending: 1 + ceil((max entry_rank - min B_f) / min step c)
//   clamped to at least one activation; bounded-container capacities provide
//   the inductive edge (the live length never exceeds the P4-D container
//   capacity). A group that fails R1/R2/R3 in ANY way is rejected with
//   FN_RECURSION_UNBOUNDED — recursion is never silently accepted as unbounded.
//   A bound above the Core-layer sanity ceiling is reported as
//   FN_RECURSION_DEPTH; the wasm backend additionally folds the bound with the
//   concrete per-frame native-stack budget (design §6.3; env/heap accounting
//   lands with FB-3).
//
//   Accepted stdlib shapes: list_copy_into (ascending r=i, B=len(source),
//   capacity from the concrete bounded instantiation), list_copy_range_into /
//   list_concat_flatten / map_values_step / filter_keys_step (ascending r with
//   an invariant bound parameter fed from a bounded-container length),
//   list_fold_right_from (descending r from a bounded-container length).
//   Helpers whose recursion routes through a first-class callable value
//   (list_map_into and friends) join this lattice only after the FB-3 closure
//   slice.
//
// PURE: the analysis never mutates the program and is total (every failure is
// an explicit finding, never an exception). It is the SINGLE shared derivation
// used by the structural verifier (the gate) and the wasm backend (the
// consumer), so the bound the verifier seals is the bound codegen consumes — no
// second derivation.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core {

/// Core-layer sanity ceiling for a sealed recursion depth. Independent of any
/// backend frame/stack size: it only rejects bounds that cannot denote a
/// finite wasm32 resource plan; the wasm backend applies its tighter native
/// stack budget on top. Bounded-container capacities inside the fixed 64 KiB
/// page stay well under this value.
inline constexpr std::uint64_t kFnRecursionDepthCeiling = 1'000'000ULL;

/// Why a recursion group could not be sealed with a finite static depth. Every
/// shape maps to `core.verify.FN_RECURSION_UNBOUNDED` (fail-closed); the enum
/// is the identity, the verifier owns the human-readable message.
enum class FnRecursionIssueKind {
    /// A member has no Int rank parameter at all.
    RankNotInteger,
    /// No rank-slot/direction assignment makes every internal edge pass
    /// rank +/- a positive constant (R1).
    NoRankProgression,
    /// A member's body has no divergent base-case comparison over its rank
    /// against a bounded expression (R2).
    NoBaseGuard,
    /// The guard's bound references a parameter that is not threaded
    /// verbatim along an internal recursive edge, or its value is not
    /// statically bounded at the group entries (R2/R3).
    BoundNotStatic,
    /// An edge entering the recursion group binds the rank slot to a value
    /// with no static interval (R3).
    EntryRankNotStatic,
    /// A recursive edge is NOT dominated by the accepted base guard: the call
    /// sits lexically BEFORE the guard (so the guard cannot stop the first
    /// activation) or INSIDE the guard's divergent stop (THEN) branch, where it
    /// runs exactly when the rank has already reached its bound. A structurally
    /// correct guard plus rank progression are not enough — the recursive edges
    /// must be reachable only on the guard's continue path (its ELSE / the
    /// fallthrough after the if).
    EdgeNotDominatedByGuard,
};

/// One structural finding against one recursion group. Ranges anchor the
/// diagnostic at the offending recursive call / fn; member lists are in
/// deterministic ascending CoreFnId order so output is reproducible.
struct FnRecursionIssue {
    FnRecursionIssueKind kind{};
    /// The offending edge's caller/callee fn ids (kInvalid marks an SCC-level
    /// finding such as a missing base guard).
    std::uint32_t edge_from{CoreFnId::kInvalid};
    std::uint32_t edge_to{CoreFnId::kInvalid};
    /// The argument slot that failed the rank/bound rule, when edge-specific.
    std::optional<std::uint32_t> slot;
    SourceRangeOpt range;
};

/// One sealed recursion group (a nontrivial SCC) and its static depth bound.
struct FnRecursionScc {
    std::vector<std::uint32_t> members; // ascending CoreFnId order
    std::uint64_t depth_bound{0};
};

struct FnRecursionAnalysis {
    /// Only NONTRIVIAL SCCs (a self edge or at least two members) that sealed.
    std::vector<FnRecursionScc> sccs;
    /// Per-fn sealed depth: -1 = not part of a recursion group; >= 0 = the
    /// bound of the (unique) SCC the fn belongs to.
    std::vector<std::int64_t> fn_depth_bound;
    /// Full Tarjan condensation (trivial, acyclic components included), each a
    /// sorted member list in deterministic component order (Tarjan emission
    /// order: a reverse topological order). Backends that budget a resource
    /// across a whole call tree consume this so they never re-derive SCCs.
    std::vector<std::vector<std::uint32_t>> components;
    /// CoreFnId -> condensation component index for every fn.
    std::vector<std::uint32_t> component_of;
    /// Condensation DAG edges (component -> component), external edges only,
    /// each sorted/deduped, indexed in parallel with `components`.
    std::vector<std::vector<std::uint32_t>> condensation_edges;
    /// Structural failures; a non-empty vector means the recursion groups are
    /// NOT executable.
    std::vector<FnRecursionIssue> unbounded_issues;
    /// Groups whose derived bound exceeds `kFnRecursionDepthCeiling`.
    std::vector<FnRecursionScc> overflow_sccs;
};

/// FB-3b (design §5.2/§6.2/§8.1-6): an interprocedural, flow-sensitive CLOSURE
/// POINTS-TO analysis (a simple 0-CFA over Core-ANF). For every abstract slot
/// that can hold a first-class callable — a fn parameter, a lifted fn's env
/// capture slot, or a let-bound SSA local — it records the CONSERVATIVE set of
/// fn bodies (`CoreFnId`) a closure value at that slot can actually be at run
/// time. The set is derived structurally, never by signature alone:
///   * a `CoreClosureExpr` seeds its target fn into its result slot;
///   * a direct `CoreCallExpr` propagates each argument's set into the callee
///     fn's corresponding PARAMETER slot;
///   * a `CoreClosureExpr` operand propagates into the lifted fn's ENV slot;
///   * a `CoreValueRefExpr` aliases its source slot;
///   * a `CoreCallClosureExpr` through a slot dispatches to that slot's set.
/// This is a CLOSED-WORLD fact: every callable value in a Core program
/// originates at a `CoreClosureExpr` (a bare static-fn reference lowers to a
/// zero-env `CoreClosureExpr`, and the Pure lane has no external/opaque
/// function pointers), so an EMPTY set at a call site means no constructable
/// closure reaches it in THIS program and the site contributes no edge — it is
/// not an unknown that must fail closed. Context INSENSITIVITY gives the safety
/// direction: when the same parameter is passed distinct closures on distinct
/// calls, the slot's set is their union, which only ever ADDS edges (and so
/// only ever seals more cycles). This is the SINGLE derivation shared by the
/// recursion lattice, the native-depth gate, and the wasm heap-budget gate.
class ClosurePointsTo {
  public:
    /// Run the fixed point over one whole Core program.
    [[nodiscard]] static ClosurePointsTo analyze(const CoreProgram &program);

    /// The possible runtime targets of one `CoreCallClosureExpr`: the closure
    /// set of its callee SSA slot within `caller_storage` (a fn body or a
    /// flow/workflow root storage). Sorted and de-duplicated `CoreFnId` body
    /// indices. An empty result means no trackable closure reaches the site.
    [[nodiscard]] std::vector<std::uint32_t> targets_of(
        const CoreBodyStorage &caller_storage,
        const CoreCallClosureExpr &call) const;

    /// The closure set of one storage-local slot (empty when untracked).
    [[nodiscard]] const std::vector<std::uint32_t> &slot_set(
        const CoreBodyStorage &storage, CoreValueId slot) const;

  private:
    // storage identity -> per-SSA-slot closure set.
    std::unordered_map<const CoreBodyStorage *, std::vector<std::vector<std::uint32_t>>>
        sets_;
};

/// Derive the recursion-depth lattice for one lowered Core program. Pure and
/// deterministic: the same program always yields byte-identical SCC order,
/// bounds, and findings.
[[nodiscard]] FnRecursionAnalysis analyze_fn_recursion(const CoreProgram &program);

/// The worst-case NATIVE wasm call-stack depth the direct-call graph can
/// reach, given a sealed analysis: the condensation DAG is weighted with each
/// nontrivial SCC's sealed `depth_bound` and each trivial SCC's single
/// activation, and the maximum weighted path is returned. This is the number
/// the wasm backend gates against its engine-safe call-stack budget; it is the
/// SAME analysis the verifier seals, not a second derivation. Returns 0 when
/// the program has no outlined fns. When `reachable` is non-null only fns with
/// a true entry participate (an unreachable recursion group is never emitted
/// and therefore never gates).
[[nodiscard]] std::uint64_t max_native_fn_call_depth(
    const CoreProgram &program, const FnRecursionAnalysis &analysis,
    const std::vector<bool> *reachable = nullptr);

} // namespace ahfl::ir::core
