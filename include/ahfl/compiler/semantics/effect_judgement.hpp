#pragma once

// P4a (RFC corelib-effect-system.zh.md §2.2 / §6.1): the function-signature
// level effect judgement.
//
// `ExprEffect` (effects.hpp) is the *expression-level* inference lattice
// (6 ranks: Pure / ConstOnly / PredicateCall / CapabilityCall / ExternalEffect
// / Unknown). `EffectJudgement` is the *signature-level* judgement produced by
// projecting an inferred `ExprEffect` (§6.1 mapping table). It is one of:
//
//   Pure              — pure, terminating, side-effect free
//   Nondet            — genuinely non-deterministic (wall-clock / random)
//   CapabilitySet     — exercises a named set of capabilities {c1, ..., ck}
//   Bottom            — error-recovery sentinel (does not propagate 2nd-order)
//
// The partial order ⊑ (§2.2) and the join operator (§2.2 / §2.6) are defined
// below. `project` is the §6.1 projection from `ExprEffect`.
//
// This header is intentionally self-contained: it depends only on ExprEffect
// and SymbolId (a POD struct) so it can be included from any semantics TU
// without dragging the full AST.

#include <optional>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "ahfl/compiler/semantics/effects.hpp"
#include "ahfl/compiler/semantics/resolver.hpp" // SymbolId

namespace ahfl {

/// A set of capability SymbolIds. Stored by value (cheap to copy for the
/// judgement lattice where sets are small); equality compares contents.
struct CapabilitySymbolSet {
    std::unordered_set<std::size_t> values; // SymbolId.value entries

    [[nodiscard]] bool empty() const noexcept { return values.empty(); }

    [[nodiscard]] bool contains(SymbolId id) const noexcept {
        return values.find(id.value) != values.end();
    }

    void insert(SymbolId id) { values.insert(id.value); }

    /// A ⊇ B (superset): used by the ⊑ partial order — declaring *more*
    /// capabilities is the stronger effect (RFC §2.2 rule 4, "reversed").
    [[nodiscard]] bool is_superset_of(const CapabilitySymbolSet &other) const noexcept {
        for (const auto &value : other.values) {
            if (values.find(value) == values.end()) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] friend bool operator==(const CapabilitySymbolSet &lhs,
                                         const CapabilitySymbolSet &rhs) noexcept = default;
};

/// The function-signature level effect judgement (RFC §2.2).
struct EffectJudgement {
    enum class Kind {
        Pure,
        Nondet,
        CapabilitySet,
        Bottom,
    };

    Kind kind{Kind::Bottom};
    CapabilitySymbolSet capabilities; // populated iff kind == CapabilitySet

    [[nodiscard]] static EffectJudgement make_pure() noexcept { return {Kind::Pure, {}}; }
    [[nodiscard]] static EffectJudgement make_nondet() noexcept { return {Kind::Nondet, {}}; }
    [[nodiscard]] static EffectJudgement make_bottom() noexcept { return {Kind::Bottom, {}}; }
    [[nodiscard]] static EffectJudgement
    make_capability_set(CapabilitySymbolSet caps) noexcept {
        return {Kind::CapabilitySet, std::move(caps)};
    }

    [[nodiscard]] bool is_pure() const noexcept { return kind == Kind::Pure; }
    [[nodiscard]] bool is_bottom() const noexcept { return kind == Kind::Bottom; }

    [[nodiscard]] friend bool operator==(const EffectJudgement &lhs,
                                         const EffectJudgement &rhs) noexcept = default;
};

/// §6.1 projection table: ExprEffect -> EffectJudgement.
///
///   Pure            -> Pure
///   ConstOnly       -> Pure
///   PredicateCall   -> Pure
///   CapabilityCall  -> CapabilitySet({called_capability})  (*)
///   ExternalEffect  -> CapabilitySet({called_capability})  (*)
///   Unknown         -> Bottom
///
/// (*) CapabilityCall / ExternalEffect carry the called capability as an
/// ExprEffect rank only; the *identity* of the capability is not stored on the
/// ExprEffect. When the caller knows which capability symbol was invoked (the
/// fn-call typecheck path does), pass it via `called_capability` so the
/// projection emits a single-element set; when unknown, pass std::nullopt and
/// the projection falls back to a single anonymous element (SymbolId{0}) so
/// the judgement is still classified as CapabilitySet rather than collapsed to
/// Pure. The anonymous element is sufficient for the §2.6.4 Pure-completeness
/// check (which only cares whether the kind is Pure or not).
[[nodiscard]] inline EffectJudgement
project(ExprEffect effect, std::optional<SymbolId> called_capability = std::nullopt) noexcept {
    switch (effect) {
    case ExprEffect::Pure:
    case ExprEffect::ConstOnly:
    case ExprEffect::PredicateCall:
        return EffectJudgement::make_pure();
    case ExprEffect::Nondet:
        // A Nondet-declared fn call projects back to a Nondet judgement so a
        // Nondet fn calling another Nondet fn is correctly classified as
        // Nondet (not collapsed to CapabilitySet, which is incomparable with
        // Nondet and would spuriously trigger the under-declared check).
        return EffectJudgement::make_nondet();
    case ExprEffect::CapabilityCall:
    case ExprEffect::ExternalEffect: {
        CapabilitySymbolSet caps;
        caps.insert(called_capability.value_or(SymbolId{0}));
        return EffectJudgement::make_capability_set(std::move(caps));
    }
    case ExprEffect::Unknown:
        return EffectJudgement::make_bottom();
    }
    return EffectJudgement::make_bottom();
}

/// §2.2 partial order: returns true iff `lhs ⊑ rhs` (lhs is no stronger than
/// rhs; equivalently rhs is an admissible upper bound for lhs).
///
///   Pure ⊑ Pure
///   Pure ⊑ Nondet,  Pure ⊑ CapabilitySet
///   Nondet ⊑ Nondet
///   CapabilitySet ⊑ CapabilitySet  when lhs.capabilities ⊇ rhs.capabilities
///                                  (reversed: declaring more is stronger)
///   Bottom ⊑ anything (error-recovery does not restrict the bound)
///   Nondet vs CapabilitySet: incomparable -> false
[[nodiscard]] inline bool judgement_le(const EffectJudgement &lhs,
                                       const EffectJudgement &rhs) noexcept {
    if (lhs.kind == EffectJudgement::Kind::Bottom) {
        return true; // Bottom is the recovery floor.
    }
    if (rhs.kind == EffectJudgement::Kind::Bottom) {
        return lhs.kind == EffectJudgement::Kind::Bottom;
    }

    switch (lhs.kind) {
    case EffectJudgement::Kind::Pure:
        return true; // Pure ⊑ anything non-Bottom.
    case EffectJudgement::Kind::Nondet:
        return rhs.kind == EffectJudgement::Kind::Nondet;
    case EffectJudgement::Kind::CapabilitySet:
        if (rhs.kind != EffectJudgement::Kind::CapabilitySet) {
            return false;
        }
        return lhs.capabilities.is_superset_of(rhs.capabilities);
    case EffectJudgement::Kind::Bottom:
        return true;
    }
    return false;
}

/// §2.2 / §2.6 effect join (for composing sub-expression / sub-statement
/// effects). Returns Bottom when Nondet meets CapabilitySet (incompatible —
/// the caller is expected to diagnose `E::effect_incompatible`).
///
///   join(Pure, E)            = E
///   join(E, Pure)            = E
///   join(Nondet, Nondet)     = Nondet
///   join(CapSet A, CapSet B) = CapSet (A ∪ B)
///   join(Nondet, CapSet)     = Bottom   (incompatible)
///   join(Bottom, E)          = E        (Bottom is the identity, recovery)
[[nodiscard]] inline EffectJudgement join(const EffectJudgement &lhs,
                                          const EffectJudgement &rhs) noexcept {
    if (lhs.kind == EffectJudgement::Kind::Bottom) {
        return rhs;
    }
    if (rhs.kind == EffectJudgement::Kind::Bottom) {
        return lhs;
    }
    if (lhs.kind == EffectJudgement::Kind::Pure) {
        return rhs;
    }
    if (rhs.kind == EffectJudgement::Kind::Pure) {
        return lhs;
    }
    if (lhs.kind == EffectJudgement::Kind::Nondet &&
        rhs.kind == EffectJudgement::Kind::Nondet) {
        return EffectJudgement::make_nondet();
    }
    if (lhs.kind == EffectJudgement::Kind::CapabilitySet &&
        rhs.kind == EffectJudgement::Kind::CapabilitySet) {
        CapabilitySymbolSet merged = lhs.capabilities;
        for (const auto &value : rhs.capabilities.values) {
            merged.values.insert(value);
        }
        return EffectJudgement::make_capability_set(std::move(merged));
    }
    // Nondet vs CapabilitySet: incomparable -> recover to Bottom.
    return EffectJudgement::make_bottom();
}

/// §6.3 canonical `is_pure(EffectJudgement)` entry point.
[[nodiscard]] inline bool is_pure(const EffectJudgement &judgement) noexcept {
    return judgement.is_pure();
}

/// §2.6.4 effect-underdeclared check: returns true iff the *declared* effect
/// is an admissible upper bound for the *body* effect (i.e. body ⊑
/// declared). If false, the function's declared effect does not cover the
/// actual body effect, and the compiler should report E::effect_underdeclared.
///
/// A Bottom body effect recovers to true (error recovery — a
/// previously-errored function body does not compound errors).
/// A Bottom declared effect returns false (nothing is under-declared relative to
/// Bottom, which is the least element).
[[nodiscard]] inline bool
effect_decl_covers(const EffectJudgement &declared,
                     const EffectJudgement &body) noexcept {
    if (body.kind == EffectJudgement::Kind::Bottom) {
        return true; // recovery: don't cascade
    }
    if (declared.kind == EffectJudgement::Kind::Bottom) {
        return false; // nothing is under Bottom
    }
    return judgement_le(body, declared);
}

/// §4.2 verifiable-subset eligibility check. A function is eligible for the
/// verifiable subset iff:
///   1. effect == Pure
///   2. has a decreases termination measure
///
/// Bounded refinement (condition 3) is checked separately at the call site
/// because it depends on the concrete argument types.
///
/// Returns true for Bottom (recovery: don't cascade).
[[nodiscard]] inline bool
is_verifiable_subset_eligible(const EffectJudgement &judgement,
                              bool has_decreases) noexcept {
    if (judgement.kind == EffectJudgement::Kind::Bottom) {
        return true; // recovery
    }
    return judgement.kind == EffectJudgement::Kind::Pure && has_decreases;
}

/// Human-readable name for diagnostics / debugging.
[[nodiscard]] inline std::string_view to_string(const EffectJudgement &j) noexcept {
    switch (j.kind) {
    case EffectJudgement::Kind::Pure:
        return "Pure";
    case EffectJudgement::Kind::Nondet:
        return "Nondet";
    case EffectJudgement::Kind::CapabilitySet:
        return "CapabilitySet";
    case EffectJudgement::Kind::Bottom:
        return "Bottom";
    }
    return "Bottom";
}

} // namespace ahfl
