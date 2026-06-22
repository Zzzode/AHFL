#pragma once

#include <string_view>

namespace ahfl {

enum class ExprEffect {
    Pure,
    ConstOnly,
    PredicateCall,
    // P2 (RFC §2.2): a call to a Nondet-declared fn. Distinct from
    // CapabilityCall/ExternalEffect so the body-effect projection can recover
    // a Nondet judgement (rather than collapsing every non-Pure call to
    // CapabilitySet, which would make a Nondet fn calling another Nondet fn
    // spuriously under-declared).
    Nondet,
    CapabilityCall,
    ExternalEffect,
    Unknown,
};

[[nodiscard]] bool is_effect_pure(ExprEffect effect) noexcept;
[[nodiscard]] ExprEffect join_effects(ExprEffect lhs, ExprEffect rhs) noexcept;
[[nodiscard]] std::string_view to_string(ExprEffect effect) noexcept;

} // namespace ahfl
