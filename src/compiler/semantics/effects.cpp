#include "ahfl/compiler/semantics/effects.hpp"

namespace ahfl {

bool is_effect_pure(ExprEffect effect) noexcept {
    switch (effect) {
    case ExprEffect::Pure:
    case ExprEffect::ConstOnly:
    case ExprEffect::PredicateCall:
        return true;
    case ExprEffect::CapabilityCall:
    case ExprEffect::ExternalEffect:
    case ExprEffect::Unknown:
        return false;
    }

    return false;
}

ExprEffect join_effects(ExprEffect lhs, ExprEffect rhs) noexcept {
    const auto rank = [](ExprEffect effect) noexcept {
        switch (effect) {
        case ExprEffect::Pure:
            return 0;
        case ExprEffect::ConstOnly:
            return 1;
        case ExprEffect::PredicateCall:
            return 2;
        case ExprEffect::CapabilityCall:
            return 3;
        case ExprEffect::ExternalEffect:
            return 4;
        case ExprEffect::Unknown:
            return 5;
        }

        return 5;
    };

    return rank(lhs) >= rank(rhs) ? lhs : rhs;
}

} // namespace ahfl
