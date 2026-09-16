#include "ahfl/base/query/query_engine.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::query {

namespace {

// Internal propagation vehicle for CyclePolicy::Error: thrown across user
// compute frames when a nested read reaches an in-progress slot, caught at the
// public eval boundary and converted to std::unexpected.
struct CyclePropagation {
    CycleError error;
};

} // namespace

std::size_t QueryEngine::FlatSlotKeyHash::operator()(FlatSlotKey key) const
    noexcept {
    // Family/slot indices are dense small integers; mix both into one word.
    return (key.family * 0x9e3779b1U) ^ (key.slot + 0x85ebca77U);
}

std::string CycleError::describe() const {
    std::string text = "query cycle detected:";
    for (const SlotKey &key : path) {
        const std::size_t slot = std::visit(
            [](const auto &typed) { return typed.index(); }, key.slot);
        text += " family ";
        text += std::to_string(key.family.index());
        text += " slot ";
        text += std::to_string(slot);
        text += " ->";
    }
    return text;
}

CyclePanic::CyclePanic(CycleError error)
    : std::runtime_error(error.describe()), error_(std::move(error)) {}

QueryEngine::QueryEngine(CyclePolicy policy)
    : policy_(policy), context_(*this) {}

QueryEngine::~QueryEngine() = default;

FamilyId QueryEngine::register_input_family(EqualsFn equals) {
    Family family;
    family.is_input = true;
    family.equals = std::move(equals);
    families_.push_back(std::move(family));
    return FamilyId{families_.size() - 1};
}

FamilyId QueryEngine::register_derived_family(RecomputeFn recompute,
                                              EqualsFn equals) {
    Family family;
    family.is_input = false;
    family.recompute = std::move(recompute);
    family.equals = std::move(equals);
    families_.push_back(std::move(family));
    return FamilyId{families_.size() - 1};
}

void QueryEngine::ensure_slot(Family &family, std::size_t slot_index) {
    if (family.slots.size() <= slot_index) {
        family.slots.resize(slot_index + 1);
    }
}

QueryEngine::Slot &QueryEngine::slot_at(FlatSlotKey key) {
    return families_[key.family].slots[key.slot];
}

SlotKey QueryEngine::materialize_key(FlatSlotKey key) const {
    if (families_[key.family].is_input) {
        return SlotKey{FamilyId{key.family}, InputId{key.slot}};
    }
    return SlotKey{FamilyId{key.family}, DerivedId{key.slot}};
}

CycleError QueryEngine::current_cycle_path(FlatSlotKey repeated) const {
    CycleError error;
    auto begin = std::ranges::find(stack_, repeated);
    for (auto it = begin; it != stack_.end(); ++it) {
        error.path.push_back(materialize_key(*it));
    }
    error.path.push_back(materialize_key(repeated));
    return error;
}

void QueryEngine::raise_cycle(FlatSlotKey repeated) {
    CycleError error = current_cycle_path(repeated);
    if (policy_ == CyclePolicy::Panic) {
        throw CyclePanic(std::move(error));
    }
    throw CyclePropagation{std::move(error)};
}

void QueryEngine::record_dependency(FlatSlotKey target) {
    if (stack_.empty()) {
        return;
    }
    const FlatSlotKey current = stack_.back();
    Slot &dependent = slot_at(current);
    if (std::ranges::find(dependent.dependencies, target) !=
        dependent.dependencies.end()) {
        return;
    }
    dependent.dependencies.push_back(target);
    Slot &provider = slot_at(target);
    if (std::ranges::find(provider.dependents, current) ==
        provider.dependents.end()) {
        provider.dependents.push_back(current);
    }
}

void QueryEngine::mark_dependents_dirty(const Slot &changed) {
    // Iterative DFS over reverse edges. Every transitive dependent is marked
    // dirty exactly once; nothing is recomputed here (lazy red phase).
    std::vector<FlatSlotKey> pending(changed.dependents.begin(),
                                     changed.dependents.end());
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> seen;
    while (!pending.empty()) {
        const FlatSlotKey key = pending.back();
        pending.pop_back();
        if (!seen.insert(key).second) {
            continue; // each transitive dependent is visited exactly once
        }
        Slot &dependent = slot_at(key);
        dependent.state = SlotState::Dirty;
        pending.insert(pending.end(), dependent.dependents.begin(),
                       dependent.dependents.end());
    }
}

void QueryEngine::update_input(FamilyId family_id, std::size_t slot_index,
                               std::any value) {
    Family &family = families_[family_id.index()];
    ensure_slot(family, slot_index);
    Slot &input = family.slots[slot_index];

    if (input.has_value && family.equals(input.value, value)) {
        // Equal write: no revision bump, no dirtying.
        ++stats_.input_update_noops;
        return;
    }

    ++revision_;
    input.value = std::move(value);
    input.has_value = true;
    input.state = SlotState::Clean;
    input.changed_at = revision_;
    input.verified_at = revision_;
    ++stats_.input_updates;

    mark_dependents_dirty(input);
}

const std::any &QueryEngine::read_input(FamilyId family_id,
                                        std::size_t slot_index) {
    Family &family = families_[family_id.index()];
    ensure_slot(family, slot_index);
    Slot &input = family.slots[slot_index];
    if (!input.has_value) {
        throw std::logic_error(
            "ahfl::query: input family " +
            std::to_string(family_id.index()) + " slot " +
            std::to_string(slot_index) + " was read before being set");
    }
    record_dependency(FlatSlotKey{family_id.index(), slot_index});
    return input.value;
}

bool QueryEngine::prove_unchanged(Slot &candidate) {
    // Salsa green phase: re-fetch every recorded dependency (bringing stale
    // ones up to date recursively) and keep the memo only if every dependency
    // last changed at or before the memo's verification revision.
    const std::vector<FlatSlotKey> dependencies = candidate.dependencies;
    for (const FlatSlotKey dependency : dependencies) {
        const Family &provider_family = families_[dependency.family];
        if (provider_family.is_input) {
            const Slot &provider = provider_family.slots[dependency.slot];
            if (!provider.has_value ||
                provider.changed_at > candidate.verified_at) {
                return false;
            }
            continue;
        }
        if (fetch_derived(dependency) == nullptr) {
            return false;
        }
        const Slot &provider = slot_at(dependency);
        if (provider.changed_at > candidate.verified_at) {
            return false;
        }
    }
    return true;
}

const std::any *QueryEngine::fetch_derived(FlatSlotKey target) {
    Slot &candidate = slot_at(target);
    if (candidate.in_progress) {
        raise_cycle(target);
    }
    if (candidate.has_value && candidate.verified_at == revision_) {
        return &candidate.value; // memo already valid at this revision
    }

    // Active for the whole bring-up-to-date attempt, green proof included: a
    // slot being proven is still being evaluated, so re-entering it (however
    // the dependency graph got there) is a cycle.
    candidate.in_progress = true;
    try {
        if (candidate.has_value && prove_unchanged(candidate)) {
            candidate.state = SlotState::Verified;
            candidate.verified_at = revision_;
            candidate.in_progress = false;
            return &candidate.value;
        }
        const std::any *result = recompute(target);
        candidate.in_progress = false;
        return result;
    } catch (...) {
        candidate.in_progress = false;
        throw;
    }
}

const std::any *QueryEngine::recompute(FlatSlotKey target) {
    Family &family = families_[target.family];
    ensure_slot(family, target.slot);
    Slot &candidate = family.slots[target.slot];

    const std::vector<FlatSlotKey> previous_dependencies =
        candidate.dependencies;
    candidate.dependencies.clear();

    // in_progress is owned by the fetch_derived frame; recompute only manages
    // the edge-attribution stack.
    stack_.push_back(target);

    std::any fresh;
    try {
        family.recompute(context_, DerivedId{target.slot}, fresh);
    } catch (...) {
        // Do not leave half-recorded edges from the aborted attempt.
        for (const FlatSlotKey dependency : candidate.dependencies) {
            Slot &provider = slot_at(dependency);
            std::erase(provider.dependents, target);
        }
        candidate.dependencies.clear();
        stack_.pop_back();
        throw;
    }

    stack_.pop_back();

    const bool value_changed =
        !candidate.has_value || !family.equals(candidate.value, fresh);
    candidate.value = std::move(fresh);
    candidate.has_value = true;
    candidate.state = SlotState::Clean;
    candidate.verified_at = revision_;
    if (value_changed) {
        // Equal recompute results keep the older changed_at so transitive
        // dependents can stay green.
        candidate.changed_at = revision_;
    }
    ++stats_.recomputations;

    // Drop reverse edges to dependencies the new computation no longer reads.
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> current;
    current.reserve(candidate.dependencies.size() * 2 + 1);
    current.insert(candidate.dependencies.begin(),
                   candidate.dependencies.end());
    for (const FlatSlotKey old_dependency : previous_dependencies) {
        if (current.contains(old_dependency)) {
            continue;
        }
        Slot &provider = slot_at(old_dependency);
        std::erase(provider.dependents, target);
    }

    return &candidate.value;
}

const std::any *QueryEngine::read_derived(FamilyId family_id,
                                          std::size_t slot_index) {
    const FlatSlotKey target{family_id.index(), slot_index};
    ensure_slot(families_[family_id.index()], slot_index);
    record_dependency(target);
    return fetch_derived(target);
}

std::expected<const std::any *, CycleError>
QueryEngine::eval_slot(FamilyId family_id, std::size_t slot_index) {
    ++stats_.derived_evals;
    const std::uint64_t recomputations_before = stats_.recomputations;
    const FlatSlotKey target{family_id.index(), slot_index};
    ensure_slot(families_[family_id.index()], slot_index);
    try {
        const std::any *memo = fetch_derived(target);
        if (stats_.recomputations == recomputations_before) {
            ++stats_.memo_hits;
        }
        return memo;
    } catch (const CyclePropagation &propagation) {
        return std::unexpected(propagation.error);
    }
}

Revision QueryEngine::revision() const noexcept { return revision_; }

QueryStats QueryEngine::stats() const { return stats_; }

SlotInfo QueryEngine::inspect_slot(FamilyId family_id,
                                   std::size_t slot_index) const {
    const Family &family = families_[family_id.index()];
    SlotInfo info;
    if (family.slots.size() <= slot_index) {
        return info;
    }
    const Slot &slot = family.slots[slot_index];
    info.has_value = slot.has_value;
    info.changed_at = slot.changed_at;
    info.verified_at = slot.verified_at;
    info.state = slot.state;
    return info;
}

} // namespace ahfl::query
