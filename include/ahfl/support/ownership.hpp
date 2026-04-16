#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl {

template <typename T> using Owned = std::unique_ptr<T>;

template <typename T> using Ref = std::reference_wrapper<T>;

template <typename T> using MaybeRef = std::optional<Ref<T>>;

template <typename T> using MaybeCRef = std::optional<Ref<const T>>;

template <typename T, typename... Args> [[nodiscard]] Owned<T> make_owned(Args &&...args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T> [[nodiscard]] MaybeRef<T> borrow(T *value) {
    if (value == nullptr) {
        return std::nullopt;
    }

    return std::ref(*value);
}

template <typename T> [[nodiscard]] MaybeCRef<T> borrow(const T *value) {
    if (value == nullptr) {
        return std::nullopt;
    }

    return std::cref(*value);
}

template <typename T> [[nodiscard]] T &require(T *value, std::string_view message) {
    if (value == nullptr) {
        throw std::logic_error(std::string(message));
    }

    return *value;
}

template <typename T> [[nodiscard]] const T &require(const T *value, std::string_view message) {
    if (value == nullptr) {
        throw std::logic_error(std::string(message));
    }

    return *value;
}

} // namespace ahfl
