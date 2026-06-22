#include "ahfl/compiler/semantics/effects.hpp"

namespace ahfl {

bool is_effect_pure(ExprEffect effect) noexcept {
    switch (effect) {
    case ExprEffect::Pure:
    case ExprEffect::ConstOnly:
    case ExprEffect::PredicateCall:
        return true;
    case ExprEffect::Nondet:
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
        case ExprEffect::Nondet:
            return 3;
        case ExprEffect::CapabilityCall:
            return 4;
        case ExprEffect::ExternalEffect:
            return 5;
        case ExprEffect::Unknown:
            return 6;
        }

        return 6;
    };

    return rank(lhs) >= rank(rhs) ? lhs : rhs;
}

std::string_view to_string(ExprEffect effect) noexcept {
    switch (effect) {
    case ExprEffect::Pure:
        return "pure";
    case ExprEffect::ConstOnly:
        return "const-only";
    case ExprEffect::PredicateCall:
        return "predicate call";
    case ExprEffect::Nondet:
        return "nondet";
    case ExprEffect::CapabilityCall:
        return "capability call";
    case ExprEffect::ExternalEffect:
        return "external effect";
    case ExprEffect::Unknown:
        return "unknown effect";
    }

    return "unknown effect";
}

} // namespace ahfl
