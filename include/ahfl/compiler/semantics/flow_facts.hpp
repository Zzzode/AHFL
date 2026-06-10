#pragma once

#include "ahfl/base/support/source.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace ahfl {

struct Place {
    std::string root;
    std::vector<std::string> members;

    [[nodiscard]] friend bool operator==(const Place &lhs, const Place &rhs) noexcept = default;
};

struct PlaceHash {
    [[nodiscard]] std::size_t operator()(const Place &place) const noexcept;
};

enum class TypeFactKind {
    IsNone,
    IsNotNone,
};

struct TypeFact {
    Place place;
    TypeFactKind kind{TypeFactKind::IsNotNone};
    SourceRange origin;
};

struct FlowFacts {
    std::vector<TypeFact> facts;

    void add(TypeFact fact);
    void merge_from(const FlowFacts &other);
    void invalidate(const Place &place);
    [[nodiscard]] bool has_fact(const Place &place, TypeFactKind kind) const noexcept;
};

[[nodiscard]] bool is_same_or_descendant(const Place &candidate, const Place &ancestor) noexcept;

} // namespace ahfl
