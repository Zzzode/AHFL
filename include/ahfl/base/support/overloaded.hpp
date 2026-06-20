#pragma once

namespace ahfl {

/// Overloaded pattern for std::visit with multiple lambdas.
/// Usage: std::visit(Overloaded{lambda1, lambda2, ...}, variant);
template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace ahfl
