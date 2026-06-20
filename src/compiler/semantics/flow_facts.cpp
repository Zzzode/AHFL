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
    auto &bucket = facts_by_place_[fact.place];
    const auto same_fact = [&](const TypeFact &existing) {
        if (existing.kind != fact.kind) {
            return false;
        }
        if (fact.kind == TypeFactKind::IsVariant || fact.kind == TypeFactKind::IsNotVariant) {
            return existing.enum_name == fact.enum_name &&
                   existing.variant_name == fact.variant_name;
        }
        return true;
    };
    if (std::none_of(bucket.begin(), bucket.end(), same_fact)) {
        bucket.push_back(std::move(fact));
    }
}

void FlowFacts::merge_from(const FlowFacts &other) {
    other.for_each([this](const TypeFact &fact) { add(TypeFact{fact}); });
}

void FlowFacts::invalidate(const Place &place) {
    std::erase_if(facts_by_place_,
                  [&](const auto &entry) { return is_same_or_descendant(entry.first, place); });
}

bool FlowFacts::has_fact(const Place &place, TypeFactKind kind) const noexcept {
    auto it = facts_by_place_.find(place);
    if (it == facts_by_place_.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const TypeFact &fact) {
        return fact.kind == kind;
    });
}

bool FlowFacts::has_variant_fact(const Place &place,
                                 const std::string &enum_name,
                                 const std::string &variant_name) const noexcept {
    auto it = facts_by_place_.find(place);
    if (it == facts_by_place_.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const TypeFact &fact) {
        return fact.kind == TypeFactKind::IsVariant && fact.enum_name == enum_name &&
               fact.variant_name == variant_name;
    });
}

bool FlowFacts::has_not_variant_fact(const Place &place,
                                     const std::string &enum_name,
                                     const std::string &variant_name) const noexcept {
    auto it = facts_by_place_.find(place);
    if (it == facts_by_place_.end()) {
        return false;
    }
    return std::any_of(it->second.begin(), it->second.end(), [&](const TypeFact &fact) {
        return fact.kind == TypeFactKind::IsNotVariant && fact.enum_name == enum_name &&
               fact.variant_name == variant_name;
    });
}

} // namespace ahfl
