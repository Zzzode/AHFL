#pragma once

#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::query {

// Monotonic engine clock. Every *changing* input update advances it by one;
// setting an input to an equal value does not. Slot memos carry the revision at
// which they were last produced/verified.
using Revision = std::uint64_t;

// What should happen when derived queries re-enter a slot already being
// evaluated (A depends on B which depends on A)?
enum class CyclePolicy {
    // eval() returns std::unexpected(CycleError) carrying the key path.
    Error,
    // A cycle is a programming bug: throw CyclePanic carrying the key path.
    Panic,
    // Re-entrant reads return the per-family conservative assumption; the
    // involved equations are re-run until their values reach a fixpoint
    // (coinductive semantics, mirroring RelationState::Visiting in
    // MemoizedRelationSolver). Equations that do not converge within the
    // iteration cap surface as std::unexpected(CycleError{diverged=true}).
    Coinductive,
};

// Lifecycle state of a derived slot memo.
enum class SlotState {
    // No memo, or a transitive input change has invalidated it since it was
    // last verified: the next read must prove it or recompute.
    Dirty,
    // Memo produced by running the compute function at the current revision.
    Clean,
    // Memo reused without recomputation after a revision bump: every recorded
    // dependency was re-fetched and proven unchanged (salsa "green" phase).
    Verified,
    // Slot owned by an in-flight coinductive fixpoint attempt: its stored
    // value is the current assumption (initial hook value on the first
    // iteration, the previous iterate afterwards), exposed to re-entrant
    // reads instead of raising a cycle. Mirrors RelationState::Visiting.
    Visiting,
};

// Strongly typed per-family slot index. The Tag phantom parameter prevents an
// input index from being passed where a derived index is expected (and vice
// versa); canonical identity stays numeric (CLAUDE.md Principle 2).
template <typename Tag> class QueryKey {
  public:
    constexpr explicit QueryKey(std::size_t index) noexcept : index_(index) {}

    [[nodiscard]] constexpr std::size_t index() const noexcept {
        return index_;
    }

    constexpr bool operator==(const QueryKey &) const noexcept = default;

  private:
    std::size_t index_;
};

struct InputTag {};
struct DerivedTag {};

using InputId = QueryKey<InputTag>;
using DerivedId = QueryKey<DerivedTag>;

// Numeric identity of a registered query family (one input value type or one
// derived compute function). Families are allocated by registration order.
class FamilyId {
  public:
    constexpr explicit FamilyId(std::size_t index) noexcept : index_(index) {}

    [[nodiscard]] constexpr std::size_t index() const noexcept {
        return index_;
    }

    constexpr bool operator==(const FamilyId &) const noexcept = default;

  private:
    std::size_t index_;
};

// Fully qualified identity of a single slot: family plus the input/derived
// index inside it. This is the index-based identity carried in diagnostics;
// strings are rendered only for display (CLAUDE.md Principle 2).
struct SlotKey {
    FamilyId family;
    std::variant<InputId, DerivedId> slot;

    bool operator==(const SlotKey &) const = default;
};

// Why a closed cycle was reported.
enum class CycleErrorKind {
    // Illegal re-entrant read under CyclePolicy::Error (or Panic, before the
    // throw): the key is already being evaluated on the active frame chain.
    Reentrant,
    // Coinductive iteration exhausted its cap without every involved slot
    // settling on the same iterate as the previous pass (equation diverges).
    Diverged,
    // A re-entrant read under CyclePolicy::Coinductive reached a derived
    // family with no registered assumption hook and no prior memo to seed
    // from: no conservative value exists, so the cycle cannot be unfolded.
    MissingAssumption,
};

// Closed cycle result: the evaluation path from the outer eval() entry through
// every in-progress slot, closed by the first key repeated.
struct CycleError {
    std::vector<SlotKey> path;
    CycleErrorKind kind = CycleErrorKind::Reentrant;
    // Fixpoint passes executed (0 for a plain re-entrant cycle).
    std::size_t iterations = 0;

    [[nodiscard]] std::string describe() const;
};

// Exception thrown by CyclePolicy::Panic when a cycle is detected.
class CyclePanic : public std::runtime_error {
  public:
    explicit CyclePanic(CycleError error);

    [[nodiscard]] const CycleError &cycle() const noexcept {
        return error_;
    }

  private:
    CycleError error_;
};

// Observable snapshot of one slot (introspection/testing). For input slots
// changed_at is the revision of the last changing write; for derived slots it
// is the revision at which the value last actually changed (recomputation that
// produced an equal value does not advance it), while verified_at is the
// revision of the last produce-or-prove evaluation.
struct SlotInfo {
    SlotState state = SlotState::Dirty;
    Revision changed_at = 0;
    Revision verified_at = 0;
    bool has_value = false;
};

// Aggregate engine counters. Per-slot recompute instrumentation additionally
// lives inside the user-supplied compute closures.
struct QueryStats {
    std::uint64_t input_updates = 0;      // changing set_input calls
    std::uint64_t input_update_noops = 0; // equal-value set_input calls
    std::uint64_t derived_evals = 0;      // user-facing eval entries
    std::uint64_t recomputations = 0;     // compute function invocations
    // Evals whose TARGET reused its memo (current-revision hit or green proof),
    // even when a transitive dependency had to recompute underneath it.
    std::uint64_t memo_hits = 0;
    // Re-entrant reads served a Visiting approximation under
    // CyclePolicy::Coinductive (coinductive assumptions, in the
    // MemoizedRelationSolver vocabulary).
    std::uint64_t coinductive_assumptions = 0;
    // Equation passes executed while driving coinductive SCCs to fixpoints.
    std::uint64_t fixpoint_iterations = 0;
};

// Tunable engine limits.
struct QueryEngineOptions {
    // Maximum equation passes per coinductive fixpoint attempt before the
    // equations are declared divergent (CycleErrorKind::Diverged).
    std::size_t coinductive_iteration_cap = 64;
};

class QueryEngine;
template <typename T> class InputQueryT;
template <typename T> class DerivedQueryT;

// Read surface handed to derived compute functions. Reads issued here are
// automatically attributed as dependency edges of the slot under evaluation.
class QueryContext {
  public:
    // Read an input. Throws std::logic_error if the input was never set.
    template <typename T>
    [[nodiscard]] const T &get(const InputQueryT<T> &family, InputId key) const;

    // Read a derived slot, bringing it up to date lazily. Under
    // CyclePolicy::Error a nested cycle unwinds to the outer eval() call;
    // under CyclePolicy::Panic it throws CyclePanic; under
    // CyclePolicy::Coinductive a re-entrant read is served the Visiting
    // slot's current assumption.
    template <typename T> [[nodiscard]] T read(const DerivedQueryT<T> &family, DerivedId key) const;

  private:
    friend class QueryEngine;
    explicit QueryContext(QueryEngine &engine) noexcept : engine_(&engine) {}
    QueryEngine *engine_;
};

// Handle to a registered family of input values of type T.
template <typename T> class InputQueryT {
  public:
    [[nodiscard]] FamilyId family() const noexcept {
        return family_;
    }

  private:
    friend class QueryEngine;
    explicit InputQueryT(FamilyId family) noexcept : family_(family) {}
    FamilyId family_;
};

// Handle to a registered family of derived values of type T, computed by a
// function (QueryContext&, DerivedId) -> T.
template <typename T> class DerivedQueryT {
  public:
    [[nodiscard]] FamilyId family() const noexcept {
        return family_;
    }

  private:
    friend class QueryEngine;
    explicit DerivedQueryT(FamilyId family) noexcept : family_(family) {}
    FamilyId family_;
};

// Salsa red-green subset: typed input slots, memoized derived slots with
// automatically recorded dependency edges, a single monotonic revision clock,
// eager transitive dirty marking and lazy recomputation.
//
// Single-threaded: families must be registered before evaluation starts, and
// set_input() must not be called from inside a compute function.
class QueryEngine {
  public:
    explicit QueryEngine(CyclePolicy policy = CyclePolicy::Error, QueryEngineOptions options = {});
    ~QueryEngine();

    QueryEngine(const QueryEngine &) = delete;
    QueryEngine &operator=(const QueryEngine &) = delete;
    QueryEngine(QueryEngine &&) = delete;
    QueryEngine &operator=(QueryEngine &&) = delete;

    // Register a family of externally set inputs holding T.
    template <typename T>
        requires std::equality_comparable<T>
    [[nodiscard]] InputQueryT<T> register_input() {
        return InputQueryT<T>{register_input_family(make_equals<T>())};
    }

    // Register a derived query. The compute closure receives the read context
    // and the derived slot key and produces T; every read it performs records
    // a dependency edge for the slot being evaluated.
    template <typename T, typename Compute>
        requires std::equality_comparable<T> &&
                 std::is_invocable_r_v<T, Compute &, QueryContext &, DerivedId>
    [[nodiscard]] DerivedQueryT<T> register_derived(Compute compute) {
        return DerivedQueryT<T>{
            register_derived_family(make_recompute<T>(std::move(compute)), {}, make_equals<T>())};
    }

    // Register a derived query with a conservative cycle assumption. Under
    // CyclePolicy::Coinductive a re-entrant read of a Visiting slot with no
    // memo is served `assume(key)`; the equations are then iterated to their
    // fixpoint. The hook is ignored by the Error/Panic policies.
    template <typename T, typename Compute, typename Assume>
        requires std::equality_comparable<T> &&
                 std::is_invocable_r_v<T, Compute &, QueryContext &, DerivedId> &&
                 std::is_invocable_r_v<T, Assume &, DerivedId>
    [[nodiscard]] DerivedQueryT<T> register_derived(Compute compute, Assume assume) {
        return DerivedQueryT<T>{register_derived_family(
            make_recompute<T>(std::move(compute)),
            [fn = std::move(assume)](DerivedId key, std::any &out) mutable {
                out.emplace<T>(fn(key));
            },
            make_equals<T>())};
    }

    // Set an input. Equal values are a no-op (no revision bump, no dirtying);
    // a changed value bumps the revision and eagerly marks only the transitive
    // dependents dirty without recomputing anything.
    template <typename T> void set_input(const InputQueryT<T> &family, InputId key, T value) {
        update_input(family.family_, key.index(), std::any(std::move(value)));
    }

    // Evaluate a derived slot: return the memoized value when still valid,
    // otherwise recompute it (and its stale dependencies) lazily.
    template <typename T>
    [[nodiscard]] std::expected<T, CycleError> eval(const DerivedQueryT<T> &family, DerivedId key) {
        std::expected<const std::any *, CycleError> memo = eval_slot(family.family_, key.index());
        if (!memo) {
            return std::unexpected(std::move(memo.error()));
        }
        return std::any_cast<const T &>(**memo);
    }

    [[nodiscard]] Revision revision() const noexcept;
    [[nodiscard]] QueryStats stats() const;

    // Introspect any slot of a known family.
    [[nodiscard]] SlotInfo inspect_slot(FamilyId family, std::size_t slot_index) const;

  private:
    struct FlatSlotKey {
        std::size_t family = 0;
        std::size_t slot = 0;

        bool operator==(const FlatSlotKey &) const = default;
    };

    struct FlatSlotKeyHash {
        [[nodiscard]] std::size_t operator()(FlatSlotKey key) const noexcept;
    };

    // Internal propagation vehicle for CyclePolicy::Error: thrown across user
    // compute frames when a nested read reaches a Visiting slot, caught at the
    // public eval boundary and converted to std::unexpected.
    struct CyclePropagation {
        CycleError error;
    };

    // Internal propagation vehicle for CyclePolicy::Coinductive at discovery
    // time: a re-entrant read reached a Visiting slot while no fixpoint pass
    // was active. Carries the evaluation-frame suffix forming the initially
    // discovered strongly connected component and the closed key path.
    struct CoinductiveCycleDetected {
        std::vector<FlatSlotKey> members;
        CycleError error;
    };

    // Internal signal raised during a fixpoint pass: a nested fetch reached a
    // Visiting slot, so every in-flight non-member frame belongs to the same
    // SCC and must join the iteration set before the pass restarts.
    struct FixpointMembership {
        std::vector<FlatSlotKey> members;
    };

    struct Slot {
        std::any value;
        bool has_value = false;
        Revision changed_at = 0;  // revision at which the value last changed
        Revision verified_at = 0; // revision of last produce-or-prove eval
        SlotState state = SlotState::Dirty;
        std::vector<FlatSlotKey> dependencies; // slots this derived read
        std::vector<FlatSlotKey> dependents;   // derived slots reading this one
    };

    using EqualsFn = std::function<bool(const std::any &, const std::any &)>;
    using RecomputeFn = std::function<void(QueryContext &, DerivedId, std::any &)>;
    // Conservative seed for a Visiting slot (CyclePolicy::Coinductive).
    using AssumeFn = std::function<void(DerivedId, std::any &)>;

    struct Family {
        bool is_input = false;
        EqualsFn equals;        // value equality, type erased
        RecomputeFn recompute;  // derived families only
        AssumeFn assume;        // coinductive seed, may be null
        std::deque<Slot> slots; // stable addresses on growth
    };

    friend class QueryContext;

    template <typename T> [[nodiscard]] static EqualsFn make_equals() {
        return [](const std::any &before, const std::any &after) {
            return std::any_cast<const T &>(before) == std::any_cast<const T &>(after);
        };
    }

    template <typename T>
    [[nodiscard]] static RecomputeFn
    make_recompute(std::function<T(QueryContext &, DerivedId)> fn) {
        return [fn = std::move(fn)](QueryContext &ctx, DerivedId key, std::any &out) mutable {
            out.emplace<T>(fn(ctx, key));
        };
    }

    [[nodiscard]] FamilyId register_input_family(EqualsFn equals);
    [[nodiscard]] FamilyId
    register_derived_family(RecomputeFn recompute, AssumeFn assume, EqualsFn equals);

    void update_input(FamilyId family, std::size_t slot_index, std::any value);
    [[nodiscard]] std::expected<const std::any *, CycleError> eval_slot(FamilyId family,
                                                                        std::size_t slot_index);

    // Read primitives invoked by QueryContext; both record an edge from the
    // slot currently being evaluated (if any).
    [[nodiscard]] const std::any &read_input(FamilyId family, std::size_t slot_index);
    [[nodiscard]] const std::any *read_derived(FamilyId family, std::size_t slot_index);

    // Bring a derived slot's memo up to date without attributing edges: used by
    // the green-phase prover and by cross-slot reads after edge attribution.
    //
    // *target_recomputed (when non-null) is set when the target slot itself
    // runs its compute function; a memo reuse or green proof leaves it false,
    // and nested dependency recomputations never set the caller's flag.
    [[nodiscard]] const std::any *fetch_derived(FlatSlotKey target, bool *target_recomputed);
    [[nodiscard]] bool prove_unchanged(Slot &candidate);
    // Clear the slot's dependency edges, invoke its compute function with the
    // edge-attribution stack managed, roll the edges back on a throw, and
    // prune reverse edges to dependencies no longer read on success.
    void invoke_compute(FlatSlotKey target, std::any &fresh);
    // Store a freshly computed value as the memo. keep_visiting (fixpoint
    // passes) stores the iterate but leaves the slot Visiting with verified_at
    // untouched; otherwise the slot settles Clean at the current revision.
    [[nodiscard]] const std::any *
    commit_compute(FlatSlotKey target, std::any fresh, bool keep_visiting);
    // Normal recompute entry point used by fetch_derived.
    [[nodiscard]] const std::any *recompute(FlatSlotKey target);

    // A read reached a Visiting slot. Under Error/Panic this raises; under
    // Coinductive with no pass active it throws CoinductiveCycleDetected;
    // during a pass it serves a member iterate or throws FixpointMembership
    // when the Visiting slot is a pass-external frame joining the SCC.
    [[nodiscard]] const std::any *on_visiting_read(FlatSlotKey target);
    // Drive the member equations to a fixpoint. Throws CyclePropagation
    // (Diverged/MissingAssumption) on failure; user compute exceptions
    // propagate after every involved slot is reset.
    void resolve_coinductive(const CoinductiveCycleDetected &detected);
    // Drop a slot's memo and dependency edges symmetrically.
    void reset_member(FlatSlotKey member);
    // Invalidate memos of non-member slots fetched during fixpoint passes:
    // they were computed against tentative iterates.
    void invalidate_touched_external();

    void record_dependency(FlatSlotKey target);
    void ensure_slot(Family &family, std::size_t slot_index);
    [[nodiscard]] Slot &slot_at(FlatSlotKey key);
    void mark_dependents_dirty(const Slot &changed);
    [[nodiscard]] SlotKey materialize_key(FlatSlotKey key) const;
    // Build the closed cycle path from the active evaluation-frame chain,
    // starting at (and closing with) the repeated key.
    [[nodiscard]] CycleError current_cycle_path(FlatSlotKey repeated) const;

    CyclePolicy policy_;
    std::size_t iteration_cap_;
    Revision revision_ = 0;
    std::deque<Family> families_;
    QueryContext context_;
    // Edge-attribution stack: slots currently RUNNING their compute function.
    std::vector<FlatSlotKey> stack_;
    // Evaluation-frame chain: slots inside fetch_derived, green proofs and
    // fixpoint passes included. Cycle detection and path construction read
    // this chain; it always contains every key on the active eval path.
    std::vector<FlatSlotKey> eval_frames_;
    QueryStats stats_;
    // Non-empty only while resolve_coinductive runs passes.
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> fixpoint_members_;
    // Non-member derived slots computed during the current pass.
    std::unordered_set<FlatSlotKey, FlatSlotKeyHash> touched_external_;
};

template <typename T> const T &QueryContext::get(const InputQueryT<T> &family, InputId key) const {
    return std::any_cast<const T &>(engine_->read_input(family.family(), key.index()));
}

template <typename T> T QueryContext::read(const DerivedQueryT<T> &family, DerivedId key) const {
    const std::any *memo = engine_->read_derived(family.family(), key.index());
    return std::any_cast<const T &>(*memo);
}

} // namespace ahfl::query
