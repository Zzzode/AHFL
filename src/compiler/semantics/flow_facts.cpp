#include "ahfl/compiler/semantics/flow_facts.hpp"

#include <algorithm>
#include <functional>

namespace ahfl {

namespace {

[[nodiscard]] std::size_t hash_mix(std::size_t seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
    return seed;
}

} // namespace

std::size_t PlaceHash::operator()(const Place &place) const noexcept {
    auto seed = std::hash<std::string>{}(place.root);
    for (const auto &member : place.members) {
        seed = hash_mix(seed, std::hash<std::string>{}(member));
    }
    return seed;
}

bool is_same_or_descendant(const Place &candidate, const Place &ancestor) noexcept {
    if (candidate.root != ancestor.root || candidate.members.size() < ancestor.members.size()) {
        return false;
    }

    return std::equal(ancestor.members.begin(), ancestor.members.end(), candidate.members.begin());
}

void FlowFacts::add(TypeFact fact) {
    const auto same_place_and_kind = [&](const TypeFact &existing) {
        return existing.place == fact.place && existing.kind == fact.kind;
    };
    if (std::none_of(facts.begin(), facts.end(), same_place_and_kind)) {
        facts.push_back(std::move(fact));
    }
}

void FlowFacts::merge_from(const FlowFacts &other) {
    for (const auto &fact : other.facts) {
        add(fact);
    }
}

void FlowFacts::invalidate(const Place &place) {
    facts.erase(std::remove_if(facts.begin(),
                               facts.end(),
                               [&](const TypeFact &fact) {
                                   return is_same_or_descendant(fact.place, place);
                               }),
                facts.end());
}

bool FlowFacts::has_fact(const Place &place, TypeFactKind kind) const noexcept {
    return std::any_of(facts.begin(), facts.end(), [&](const TypeFact &fact) {
        return fact.place == place && fact.kind == kind;
    });
}

} // namespace ahfl
