#pragma once

#include "ahfl/base/support/source.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
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
    IsVariant,
    IsNotVariant,
};

struct TypeFact {
    Place place;
    TypeFactKind kind{TypeFactKind::IsNotNone};
    SourceRange origin;

    // Only populated when kind == IsVariant.
    std::string enum_name;
    std::string variant_name;
};

struct FlowFacts {
    void add(TypeFact fact);
    void merge_from(const FlowFacts &other);
    void invalidate(const Place &place);
    [[nodiscard]] bool has_fact(const Place &place, TypeFactKind kind) const noexcept;
    [[nodiscard]] bool has_variant_fact(const Place &place, const std::string &enum_name,
                                       const std::string &variant_name) const noexcept;
    [[nodiscard]] bool has_not_variant_fact(const Place &place, const std::string &enum_name,
                                           const std::string &variant_name) const noexcept;
    [[nodiscard]] bool empty() const noexcept { return facts_by_place_.empty(); }

    /// Invoke fn(const TypeFact&) for every stored fact.
    template <typename Fn>
    void for_each(Fn &&fn) const {
        for (const auto &bucket : facts_by_place_) {
            for (const auto &fact : bucket.second) {
                fn(fact);
            }
        }
    }

  private:
    std::unordered_map<Place, std::vector<TypeFact>, PlaceHash> facts_by_place_;
};

[[nodiscard]] bool is_same_or_descendant(const Place &candidate, const Place &ancestor) noexcept;

} // namespace ahfl
