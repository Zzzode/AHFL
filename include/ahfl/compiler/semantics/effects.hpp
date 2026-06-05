#pragma once

namespace ahfl {

enum class ExprEffect {
    Pure,
    ConstOnly,
    PredicateCall,
    CapabilityCall,
    ExternalEffect,
    Unknown,
};

[[nodiscard]] bool is_effect_pure(ExprEffect effect) noexcept;
[[nodiscard]] ExprEffect join_effects(ExprEffect lhs, ExprEffect rhs) noexcept;

} // namespace ahfl
