#include "ahfl/base/query/query_engine.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::query {

std::size_t QueryEngine::FlatSlotKeyHash::operator()(FlatSlotKey key) const noexcept {
    // Family/slot indices are dense small integers; mix both into one word.
    return (key.family * 0x9e3779b1U) ^ (key.slot + 0x85ebca77U);
}

std::string CycleError::describe() const {
    std::string text;
    switch (kind) {
    case CycleErrorKind::Reentrant:
        text = "query cycle detected:";
        break;
    case CycleErrorKind::Diverged:
        text = "coinductive fixpoint did not converge after " + std::to_string(iterations) +
               " iteration(s); cycle path:";
        break;
    case CycleErrorKind::MissingAssumption:
        text = "coinductive cycle reached a derived family without a "
               "conservative assumption hook; cycle path:";
        break;
    case CycleErrorKind::ResolutionBound:
        text = "query engine internal resolution bound exceeded after " +
               std::to_string(iterations) + " step(s) while resolving";
        break;
    }
    for (std::size_t i = 0; i < path.size(); ++i) {
        const SlotKey &key = path[i];
        const std::size_t slot =
            std::visit([](const auto &typed) { return typed.index(); }, key.slot);
        if (i > 0) {
            text += " ->";
        }
        text += " family ";
        text += std::to_string(key.family.index());
        text += " slot ";
        text += std::to_string(slot);
    }
    return text;
}

CyclePanic::CyclePanic(CycleError error)
    : std::runtime_error(error.describe()), error_(std::move(error)) {}

QueryEngine::QueryEngine(CyclePolicy policy, QueryEngineOptions options)
    : policy_(policy), iteration_cap_(options.coinductive_iteration_cap), context_(*this) {}

QueryEngine::~QueryEngine() = default;

FamilyId QueryEngine::register_input_family(EqualsFn equals) {
    Family family;
    family.is_input = true;
    family.equals = std::move(equals);
    families_.push_back(std::move(family));
    return FamilyId{families_.size() - 1};
}

FamilyId
QueryEngine::register_derived_family(RecomputeFn recompute, AssumeFn assume, EqualsFn equals) {
    Family family;
    family.is_input = false;
    family.recompute = std::move(recompute);
    family.assume = std::move(assume);
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
    // The repeated key must be on the active evaluation-frame chain (it was
    // found Visiting), so a missing entry here would be an internal bug.
    auto begin = std::ranges::find(eval_frames_, repeated);
    for (auto it = begin; it != eval_frames_.end(); ++it) {
        error.path.push_back(materialize_key(*it));
    }
    error.path.push_back(materialize_key(repeated));
    return error;
}

const std::any *QueryEngine::on_visiting_read(FlatSlotKey target) {
    if (policy_ == CyclePolicy::Coinductive) {
        // During a fixpoint pass, Visiting members expose their current
        // iterate (the coinductive hypothesis); a Visiting slot outside the
        // member set is a frame joining the strongly connected component
        // mid-pass.
        if (!fixpoint_members_.empty()) {
            if (fixpoint_members_.contains(target)) {
                // The active frame chain is [running member, external...].
                // Any external frame above the running member depends on a
                // member while that member (transitively) depends on it, so
                // every such frame belongs to the SCC and must join before
                // the pass restarts.
                std::vector<FlatSlotKey> joining;
                for (auto it = eval_frames_.rbegin(); it != eval_frames_.rend(); ++it) {
                    if (fixpoint_members_.contains(*it)) {
                        break;
                    }
                    joining.push_back(*it);
                }
                if (!joining.empty()) {
                    std::ranges::reverse(joining);
                    throw FixpointMembership{std::move(joining)};
                }
                ++stats_.coinductive_assumptions;
                return &slot_at(target).value;
            }
            FixpointMembership membership;
            auto begin = std::ranges::find(eval_frames_, target);
            membership.members.assign(begin, eval_frames_.end());
            throw membership;
        }
        CoinductiveCycleDetected detected;
        detected.error = current_cycle_path(target);
        auto begin = std::ranges::find(eval_frames_, target);
        detected.members.assign(begin, eval_frames_.end());
        // The discovery pass consumed one conservative assumption at the
        // re-entrant read, mirroring MemoizedRelationSolver's
        // RelationState::Visiting -> coinductive assumption branch.
        ++stats_.coinductive_assumptions;
        throw detected;
    }
    CycleError error = current_cycle_path(target);
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
    if (std::ranges::find(dependent.dependencies, target) != dependent.dependencies.end()) {
        return;
    }
    dependent.dependencies.push_back(target);
    Slot &provider = slot_at(target);
    if (std::ranges::find(provider.dependents, current) == provider.dependents.end()) {
        provider.dependents.push_back(current);
    }
}

void QueryEngine::mark_dependents_dirty(const Slot &changed) {
    // Iterative DFS over reverse edges. Every transitive dependent is marked
    // dirty exactly once; nothing is recomputed here (lazy red phase).
    std::vector<FlatSlotKey> pending(changed.dependents.begin(), changed.dependents.end());
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> seen;
    while (!pending.empty()) {
        const FlatSlotKey key = pending.back();
        pending.pop_back();
        if (!seen.insert(key).second) {
            continue; // each transitive dependent is visited exactly once
        }
        Slot &dependent = slot_at(key);
        dependent.state = SlotState::Dirty;
        pending.insert(pending.end(), dependent.dependents.begin(), dependent.dependents.end());
    }
}

void QueryEngine::update_input(FamilyId family_id, std::size_t slot_index, std::any value) {
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

const std::any &QueryEngine::read_input(FamilyId family_id, std::size_t slot_index) {
    Family &family = families_[family_id.index()];
    ensure_slot(family, slot_index);
    Slot &input = family.slots[slot_index];
    if (!input.has_value) {
        throw std::logic_error("ahfl::query: input family " + std::to_string(family_id.index()) +
                               " slot " + std::to_string(slot_index) +
                               " was read before being set");
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
            if (!provider.has_value || provider.changed_at > candidate.verified_at) {
                return false;
            }
            continue;
        }
        if (fetch_derived(dependency, nullptr) == nullptr) {
            return false;
        }
        const Slot &provider = slot_at(dependency);
        if (provider.changed_at > candidate.verified_at) {
            return false;
        }
    }
    return true;
}

const std::any *QueryEngine::fetch_derived(FlatSlotKey target, bool *target_recomputed) {
    Slot &candidate = slot_at(target);
    if (candidate.state == SlotState::Visiting) {
        // Does not return for a re-entrant non-member read: raises, throws a
        // detection, or expands the fixpoint member set.
        return on_visiting_read(target);
    }

    const bool in_pass = !fixpoint_members_.empty();
    const bool external_reached_from_pass =
        in_pass && !fixpoint_members_.contains(target) && !touched_external_.contains(target);
    if (!external_reached_from_pass && candidate.has_value && candidate.verified_at == revision_) {
        return &candidate.value; // memo already valid at this revision
    }

    // Visiting for the whole bring-up-to-date attempt, green proof included:
    // a slot being proven is still being evaluated, so re-entering it (however
    // the dependency graph got there) is a cycle.
    const SlotState prior_state = candidate.state;
    candidate.state = SlotState::Visiting;
    // Push an evaluation frame for the entire attempt (prove OR recompute).
    // The frame chain, not the narrower edge-attribution stack, is what makes
    // a cycle entered during a green proof report a closed key path.
    eval_frames_.push_back(target);
    try {
        // A non-member reached from a fixpoint pass must recompute against
        // this pass's iterates: never green-prove a memo built earlier.
        if (!external_reached_from_pass && candidate.has_value && prove_unchanged(candidate)) {
            candidate.state = SlotState::Verified;
            candidate.verified_at = revision_;
            eval_frames_.pop_back();
            return &candidate.value;
        }
        const std::any *result = recompute(target);
        if (external_reached_from_pass) {
            touched_external_.insert(target);
        }
        if (target_recomputed != nullptr) {
            *target_recomputed = true;
        }
        eval_frames_.pop_back();
        return result;
    } catch (...) {
        eval_frames_.pop_back();
        if (!candidate.has_value) {
            // invoke_compute failed (or the SCC expansion unwound through
            // this slot): the memo was deliberately invalidated.
            candidate.state = SlotState::Dirty;
        } else {
            // Interrupted during the green proof: keep the stale memo state
            // the frame entered with rather than fabricating Verified.
            candidate.state = prior_state;
        }
        throw;
    }
}

void QueryEngine::invoke_compute(FlatSlotKey target, std::any &fresh) {
    Family &family = families_[target.family];
    Slot &candidate = family.slots[target.slot];

    const std::vector<FlatSlotKey> previous_dependencies = candidate.dependencies;
    candidate.dependencies.clear();

    // Visiting / eval_frames_ are owned by the caller; invoke_compute only
    // manages the edge-attribution stack.
    stack_.push_back(target);
    try {
        family.recompute(context_, DerivedId{target.slot}, fresh);
    } catch (...) {
        // Fail closed: restore the previous (last-known-good) edge set
        // symmetrically so later input writes keep dirtying this slot; never
        // leave the one-sided edge deletion of the aborted attempt in place.
        for (const FlatSlotKey dependency : candidate.dependencies) {
            Slot &provider = slot_at(dependency);
            std::erase(provider.dependents, target);
        }
        candidate.dependencies = previous_dependencies;
        for (const FlatSlotKey dependency : previous_dependencies) {
            Slot &provider = slot_at(dependency);
            if (std::ranges::find(provider.dependents, target) == provider.dependents.end()) {
                provider.dependents.push_back(target);
            }
        }
        // Invalidate the stale memo: the next fetch must recompute rather
        // than green-prove an old value over (any) dependency set.
        candidate.has_value = false;
        candidate.state = SlotState::Dirty;
        stack_.pop_back();
        throw;
    }
    stack_.pop_back();

    // Drop reverse edges to dependencies this computation no longer reads.
    // Done here (not in commit) so both the normal path and fixpoint passes
    // maintain symmetric edges through every iteration.
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> current;
    current.reserve(candidate.dependencies.size() * 2 + 1);
    current.insert(candidate.dependencies.begin(), candidate.dependencies.end());
    for (const FlatSlotKey old_dependency : previous_dependencies) {
        if (current.contains(old_dependency)) {
            continue;
        }
        Slot &provider = slot_at(old_dependency);
        std::erase(provider.dependents, target);
    }
}

const std::any *
QueryEngine::commit_compute(FlatSlotKey target, std::any fresh, bool keep_visiting) {
    Family &family = families_[target.family];
    Slot &candidate = family.slots[target.slot];

    const bool changed = !candidate.has_value || !family.equals(candidate.value, fresh);
    candidate.value = std::move(fresh);
    candidate.has_value = true;
    ++stats_.recomputations;

    if (keep_visiting) {
        // Fixpoint iterate: visible to re-entrant reads, but not a settled
        // memo until the whole SCC converges.
        candidate.state = SlotState::Visiting;
        return &candidate.value;
    }

    candidate.state = SlotState::Clean;
    candidate.verified_at = revision_;
    if (changed) {
        // Equal recompute results keep the older changed_at so transitive
        // dependents can stay green.
        candidate.changed_at = revision_;
    }
    return &candidate.value;
}

const std::any *QueryEngine::recompute(FlatSlotKey target) {
    std::any fresh;
    invoke_compute(target, fresh);
    return commit_compute(target, std::move(fresh), false);
}

const std::any *QueryEngine::read_derived(FamilyId family_id, std::size_t slot_index) {
    const FlatSlotKey target{family_id.index(), slot_index};
    ensure_slot(families_[family_id.index()], slot_index);
    record_dependency(target);
    // Edge attribution belongs to the enclosing compute frame.
    return fetch_derived(target, nullptr);
}

void QueryEngine::reset_member(FlatSlotKey member) {
    Slot &slot = slot_at(member);
    for (const FlatSlotKey dependency : slot.dependencies) {
        Slot &provider = slot_at(dependency);
        std::erase(provider.dependents, member);
    }
    slot.dependencies.clear();
    slot.value.reset();
    slot.has_value = false;
    slot.state = SlotState::Dirty;
    slot.verified_at = 0;
}

void QueryEngine::invalidate_touched_external() {
    for (const FlatSlotKey external : touched_external_) {
        reset_member(external);
    }
    touched_external_.clear();
}

void QueryEngine::resolve_coinductive(const CoinductiveCycleDetected &detected) {
    std::vector<FlatSlotKey> members = detected.members;

    const auto validate = [&](std::size_t begin) -> bool {
        for (std::size_t i = begin; i < members.size(); ++i) {
            if (!families_[members[i].family].assume) {
                return false;
            }
        }
        return true;
    };

    const auto missing_assumption = [&]() -> CyclePropagation {
        CycleError error = detected.error;
        error.kind = CycleErrorKind::MissingAssumption;
        return CyclePropagation{std::move(error)};
    };

    if (!validate(0)) {
        throw missing_assumption();
    }

    // Seed every member with its conservative initial iterate and mark it
    // Visiting for the duration of the iteration.
    const auto seed = [&](std::size_t begin) {
        for (std::size_t i = begin; i < members.size(); ++i) {
            const FlatSlotKey member = members[i];
            Family &family = families_[member.family];
            Slot &slot = family.slots[member.slot];
            try {
                family.assume(DerivedId{member.slot}, slot.value);
            } catch (...) {
                // A hook that yields no conservative assumption (throws) is
                // equivalent to a missing hook: the cycle cannot be unfolded.
                // Fail closed as MissingAssumption rather than seeding the
                // remaining members from a half-populated SCC.
                throw missing_assumption();
            }
            slot.has_value = true;
            slot.state = SlotState::Visiting;
            fixpoint_members_.insert(member);
        }
    };

    // Tear down everything the fixpoint attempt touched: reset every member
    // (including members added by mid-pass SCC expansion), invalidate external
    // slots computed against tentative iterates, and leave no active pass.
    const auto rollback = [&]() {
        for (const FlatSlotKey member : members) {
            reset_member(member);
        }
        invalidate_touched_external();
        fixpoint_members_.clear();
    };

    bool committed = false;
    std::size_t iterations = 0;
    try {
        // Seeding runs inside the cleanup scope: a throwing assumption hook
        // (or one of several hooks failing partway) must never leave Visiting
        // iterates or a populated member set behind, which would fail-open on
        // the next eval and leak internal control-flow types.
        seed(0);

        // Gauss-Seidel fixpoint: members run in evaluation order and each
        // result commits immediately, so later members in a pass see the
        // freshest iterates. A pass converges when every member produces a
        // value equal to its stored iterate; at that point every member read
        // made during the pass observed the same fixed point.
        while (iterations < iteration_cap_) {
            bool expanded = false;
            bool all_stable = true;

            for (std::size_t i = 0; i < members.size(); ++i) {
                const FlatSlotKey member = members[i];
                Family &family = families_[member.family];
                Slot &slot = family.slots[member.slot];

                std::any fresh;
                eval_frames_.push_back(member);
                try {
                    invoke_compute(member, fresh);
                    // Compare BEFORE committing the new iterate.
                    if (!family.equals(slot.value, fresh)) {
                        all_stable = false;
                    }
                    static_cast<void>(commit_compute(member, std::move(fresh), true));
                    eval_frames_.pop_back();
                } catch (const FixpointMembership &membership) {
                    // The joining external frames already unwound themselves;
                    // fold any frames not yet in the SCC into the member set
                    // and restart the pass with fresh assumptions.
                    eval_frames_.pop_back();
                    const std::size_t existing = members.size();
                    for (const FlatSlotKey joining : membership.members) {
                        if (std::ranges::find(members, joining) == members.end()) {
                            members.push_back(joining);
                        }
                    }
                    expanded = true;
                    // The aborted pass is void: reset every member and every
                    // external slot it touched, then reseed.
                    for (const FlatSlotKey m : members) {
                        reset_member(m);
                    }
                    fixpoint_members_.clear();
                    invalidate_touched_external();
                    if (!validate(existing)) {
                        throw missing_assumption();
                    }
                    seed(0);
                    break;
                } catch (...) {
                    eval_frames_.pop_back();
                    throw;
                }
            }

            // Both settled passes and passes voided by SCC expansion are
            // equation passes and share the cap: a value-gated external frame
            // that rejoins with a freshly keyed slot on every restart must
            // terminate with Diverged instead of looping forever.
            ++iterations;
            ++stats_.fixpoint_iterations;

            if (expanded) {
                continue;
            }

            if (all_stable) {
                committed = true;
                break;
            }
            // Iterates moved: external slots were computed against tentative
            // values and must be recomputed on the next pass.
            invalidate_touched_external();
        }
    } catch (...) {
        rollback();
        throw;
    }

    fixpoint_members_.clear();
    touched_external_.clear();

    if (!committed) {
        for (const FlatSlotKey member : members) {
            reset_member(member);
        }
        CycleError error = detected.error;
        error.kind = CycleErrorKind::Diverged;
        error.iterations = iterations;
        throw CyclePropagation{std::move(error)};
    }

    for (const FlatSlotKey member : members) {
        Slot &slot = slot_at(member);
        slot.state = SlotState::Clean;
        slot.verified_at = revision_;
        slot.changed_at = revision_;
    }
    // External slots evaluated on the converging pass keep their memos: they
    // were computed against the final iterates with correct dependency edges.
}

std::expected<const std::any *, CycleError> QueryEngine::eval_slot(FamilyId family_id,
                                                                   std::size_t slot_index) {
    ++stats_.derived_evals;
    const FlatSlotKey target{family_id.index(), slot_index};
    ensure_slot(families_[family_id.index()], slot_index);
    // Every coinductive resolution commits at least one new Visiting slot as
    // Clean at this revision; a committed slot never re-triggers a detection,
    // so the number of resolutions is bounded by the number of derived
    // slots. The bound is recomputed every iteration because fixpoint passes
    // can read previously unseen keys (growing families). The guard turns any
    // defect into a fail-closed unexpected instead of a hang.
    const auto derived_slot_count = [&] {
        std::size_t total = 0;
        for (const Family &family : families_) {
            if (!family.is_input) {
                total += family.slots.size();
            }
        }
        return total;
    };
    // Fail-closed result for the internal safety net: no frame chain exists to
    // build a closed cycle from, so carry the offending slot and step count
    // under a distinct kind rather than fabricating a one-key cycle path.
    const auto resolution_bound = [&](std::size_t steps) -> CycleError {
        CycleError error;
        error.kind = CycleErrorKind::ResolutionBound;
        error.iterations = steps;
        error.path.push_back(materialize_key(target));
        return error;
    };
    // Tear down an orphaned fixpoint pass so a later top-level eval starts
    // from clean bookkeeping rather than a half-open Visiting set.
    const auto abandon_orphaned_pass = [&] {
        if (fixpoint_members_.empty()) {
            return;
        }
        for (const FlatSlotKey member : fixpoint_members_) {
            reset_member(member);
        }
        fixpoint_members_.clear();
        invalidate_touched_external();
    };
    for (std::size_t resolutions = 0;; ++resolutions) {
        if (resolutions > derived_slot_count() + 1) {
            return std::unexpected(resolution_bound(resolutions));
        }
        try {
            bool target_recomputed = false;
            const std::any *memo = fetch_derived(target, &target_recomputed);
            // A memo hit is a property of the TARGET frame: a dependency may
            // recompute (even transitively) while the target itself stays
            // green.
            if (!target_recomputed) {
                ++stats_.memo_hits;
            }
            return memo;
        } catch (const CoinductiveCycleDetected &detected) {
            // resolve_coinductive throws CyclePropagation on divergence or a
            // missing assumption; a throw from inside a catch arm is not
            // caught by a sibling arm of the same try, so nest.
            try {
                resolve_coinductive(detected);
            } catch (const CyclePropagation &propagation) {
                return std::unexpected(propagation.error);
            }
        } catch (const CyclePropagation &propagation) {
            return std::unexpected(propagation.error);
        } catch (const FixpointMembership &) {
            // Defensive boundary: the SCC-expansion signal is internal to a
            // fixpoint pass and must never cross the public eval() API. If it
            // escapes, bookkeeping is corrupt; reset the orphaned pass and
            // fail closed instead of letting a private type reach
            // std::terminate.
            abandon_orphaned_pass();
            return std::unexpected(resolution_bound(resolutions));
        }
    }
}

Revision QueryEngine::revision() const noexcept {
    return revision_;
}

QueryStats QueryEngine::stats() const {
    return stats_;
}

SlotInfo QueryEngine::inspect_slot(FamilyId family_id, std::size_t slot_index) const {
    SlotInfo info;
    if (family_id.index() >= families_.size()) {
        return info; // unknown family: fail closed with default snapshot
    }
    const Family &family = families_[family_id.index()];
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
