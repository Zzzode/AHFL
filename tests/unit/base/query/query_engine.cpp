#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/query/query_engine.hpp"

#include <cstdint>

using namespace ahfl::query;

namespace {

// Derived graph used by the memo/invalidation tests:
//   inputs x, y
//   double_x   = 2 * x
//   double_y   = 2 * y            (unrelated sibling)
//   chain_a    = chain_b + 1      (transitive: a -> b -> x)
//   chain_b    = x + 10
struct Graph {
    QueryEngine engine;
    InputQueryT<int> x;
    InputQueryT<int> y;
    DerivedQueryT<int> double_x;
    DerivedQueryT<int> double_y;
    DerivedQueryT<int> chain_b;
    DerivedQueryT<int> chain_a;

    int double_x_runs = 0;
    int double_y_runs = 0;
    int chain_b_runs = 0;
    int chain_a_runs = 0;

    Graph()
        : engine(CyclePolicy::Error),
          x(engine.register_input<int>()),
          y(engine.register_input<int>()),
          double_x(engine.register_derived<int>(
              [this](QueryContext &ctx, DerivedId) {
                  ++double_x_runs;
                  return ctx.get(x, InputId{0}) * 2;
              })),
          double_y(engine.register_derived<int>(
              [this](QueryContext &ctx, DerivedId) {
                  ++double_y_runs;
                  return ctx.get(y, InputId{0}) * 2;
              })),
          chain_b(engine.register_derived<int>(
              [this](QueryContext &ctx, DerivedId) {
                  ++chain_b_runs;
                  return ctx.get(x, InputId{0}) + 10;
              })),
          chain_a(engine.register_derived<int>(
              [this](QueryContext &ctx, DerivedId) {
                  ++chain_a_runs;
                  return ctx.read(chain_b, DerivedId{0}) + 1;
              })) {}
};

} // namespace

TEST_CASE("derived value computes once and repeats are served from the memo") {
    Graph g;
    g.engine.set_input(g.x, InputId{0}, 3);

    const auto first = g.engine.eval(g.double_x, DerivedId{0});
    REQUIRE(first.has_value());
    CHECK(*first == 6);
    CHECK(g.double_x_runs == 1);

    const auto second = g.engine.eval(g.double_x, DerivedId{0});
    REQUIRE(second.has_value());
    CHECK(*second == 6);
    CHECK(g.double_x_runs == 1); // no second compute

    const auto third = g.engine.eval(g.double_x, DerivedId{0});
    REQUIRE(third.has_value());
    CHECK(*third == 6);
    CHECK(g.double_x_runs == 1);
    CHECK(g.engine.stats().recomputations == 1);
    CHECK(g.engine.stats().memo_hits == 2);

    const SlotInfo info =
        g.engine.inspect_slot(g.double_x.family(), 0);
    CHECK(info.state == SlotState::Clean);
    CHECK(info.has_value);
    CHECK(info.verified_at == g.engine.revision());
}

TEST_CASE("set_input bumps the revision and recomputes only dependents") {
    Graph g;
    g.engine.set_input(g.x, InputId{0}, 3);
    g.engine.set_input(g.y, InputId{0}, 0);
    REQUIRE(g.engine.eval(g.double_x, DerivedId{0}).has_value());
    REQUIRE(g.engine.eval(g.double_y, DerivedId{0}).has_value());
    REQUIRE(g.engine.revision() == 2);
    CHECK(g.double_x_runs == 1);
    CHECK(g.double_y_runs == 1);

    g.engine.set_input(g.x, InputId{0}, 4);
    CHECK(g.engine.revision() == 3);
    CHECK(g.double_x_runs == 1); // eager marking, lazy recompute

    const auto updated = g.engine.eval(g.double_x, DerivedId{0});
    REQUIRE(updated.has_value());
    CHECK(*updated == 8);
    CHECK(g.double_x_runs == 2);

    // y never changed: the unrelated derived slot stays green.
    const auto untouched = g.engine.eval(g.double_y, DerivedId{0});
    REQUIRE(untouched.has_value());
    CHECK(*untouched == 0);
    CHECK(g.double_y_runs == 1);
    CHECK(g.engine.inspect_slot(g.double_y.family(), 0).state ==
          SlotState::Verified);
}

TEST_CASE("transitive edges A->B->input invalidate A when the input changes") {
    Graph g;
    g.engine.set_input(g.x, InputId{0}, 5);

    REQUIRE(g.engine.eval(g.chain_a, DerivedId{0}).has_value());
    CHECK(*g.engine.eval(g.chain_a, DerivedId{0}) == 16); // 5 + 10 + 1
    CHECK(g.chain_a_runs == 1);
    CHECK(g.chain_b_runs == 1);

    g.engine.set_input(g.x, InputId{0}, 7);
    const auto updated = g.engine.eval(g.chain_a, DerivedId{0});
    REQUIRE(updated.has_value());
    CHECK(*updated == 18); // 7 + 10 + 1
    CHECK(g.chain_b_runs == 2); // both levels recomputed lazily
    CHECK(g.chain_a_runs == 2);

    // One more read at the same revision: everything is memoized again.
    CHECK(*g.engine.eval(g.chain_a, DerivedId{0}) == 18);
    CHECK(g.chain_a_runs == 2);
    CHECK(g.chain_b_runs == 2);
}

TEST_CASE("setting an input to the equal value is a complete no-op") {
    Graph g;
    g.engine.set_input(g.x, InputId{0}, 11);
    REQUIRE(g.engine.eval(g.chain_a, DerivedId{0}).has_value());
    const Revision revision_before = g.engine.revision();
    CHECK(revision_before == 1);

    g.engine.set_input(g.x, InputId{0}, 11);
    CHECK(g.engine.revision() == revision_before);
    CHECK(g.engine.stats().input_update_noops == 1);
    CHECK(g.engine.stats().input_updates == 1);

    REQUIRE(g.engine.eval(g.chain_a, DerivedId{0}).has_value());
    CHECK(g.chain_a_runs == 1);
    CHECK(g.chain_b_runs == 1);
    // No revision happened, so the memo was simply still current.
    CHECK(g.engine.inspect_slot(g.chain_a.family(), 0).state ==
          SlotState::Clean);
}

TEST_CASE("revisions are strictly monotonic and visible on slot metadata") {
    Graph g;
    CHECK(g.engine.revision() == 0);
    g.engine.set_input(g.x, InputId{0}, 1);
    CHECK(g.engine.revision() == 1);
    g.engine.set_input(g.x, InputId{0}, 2);
    CHECK(g.engine.revision() == 2);
    g.engine.set_input(g.y, InputId{0}, 100);
    CHECK(g.engine.revision() == 3);
    g.engine.set_input(g.x, InputId{0}, 2); // equal: no bump
    CHECK(g.engine.revision() == 3);

    const SlotInfo input_info =
        g.engine.inspect_slot(g.x.family(), 0);
    CHECK(input_info.changed_at == 2);
    CHECK(input_info.verified_at == 2);
    CHECK(input_info.has_value);

    const SlotInfo y_info = g.engine.inspect_slot(g.y.family(), 0);
    CHECK(y_info.changed_at == 3);

    // Never touched slot exposes defaults.
    const SlotInfo absent =
        g.engine.inspect_slot(g.double_x.family(), 42);
    CHECK_FALSE(absent.has_value);
    CHECK(absent.state == SlotState::Dirty);
}

TEST_CASE("unchanged dependency output keeps a transitive dependent green") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    // derived returns 1 for every positive input: its VALUE is stable even
    // though x itself changes.
    auto clamped = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) {
            return ctx.get(input, InputId{0}) > 0 ? 1 : 0;
        });
    auto above = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) {
            return ctx.read(clamped, DerivedId{0}) + 100;
        });

    engine.set_input(input, InputId{0}, 1);
    CHECK(*engine.eval(above, DerivedId{0}) == 101);

    engine.set_input(input, InputId{0}, 2);
    // clamped must recompute (its input changed), but its value is equal so
    // above is proven green without recomputing.
    CHECK(*engine.eval(above, DerivedId{0}) == 101);
    CHECK(engine.stats().recomputations == 3); // clamped, above (initial), clamped
}

TEST_CASE("dependency edges follow what the compute actually read") {
    QueryEngine engine;
    auto selector = engine.register_input<int>();
    auto a = engine.register_input<int>();
    auto b = engine.register_input<int>();
    int runs = 0;
    auto chosen = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) {
            ++runs;
            if (ctx.get(selector, InputId{0}) == 0) {
                return ctx.get(a, InputId{0});
            }
            return ctx.get(b, InputId{0});
        });

    engine.set_input(selector, InputId{0}, 0);
    engine.set_input(a, InputId{0}, 10);
    engine.set_input(b, InputId{0}, 20);
    CHECK(*engine.eval(chosen, DerivedId{0}) == 10);

    // Flip the selector: the recompute records a new edge set; afterwards b is
    // the live dependency and a is stale.
    engine.set_input(selector, InputId{0}, 1);
    CHECK(*engine.eval(chosen, DerivedId{0}) == 20);
    CHECK(runs == 2);

    // Changing the now-unread input a must not invalidate the slot.
    engine.set_input(a, InputId{0}, 999);
    CHECK(*engine.eval(chosen, DerivedId{0}) == 20);
    CHECK(runs == 2);
    CHECK(engine.inspect_slot(chosen.family(), 0).state ==
          SlotState::Verified);
}

TEST_CASE("CyclePolicy::Error reports the closed key path") {
    QueryEngine engine(CyclePolicy::Error);
    // Slot 0 and slot 1 of one family depend on each other.
    DerivedQueryT<int> mutual = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            if (self.index() == 0) {
                return ctx.read(mutual, DerivedId{1});
            }
            return ctx.read(mutual, DerivedId{0});
        });

    auto result = engine.eval(mutual, DerivedId{0});
    REQUIRE_FALSE(result.has_value());
    // Path starts at the re-entered key, closes with the same key again.
    const CycleError &error = result.error();
    REQUIRE(error.path.size() >= 3);
    CHECK(error.path.front() == error.path.back());
    CHECK_FALSE(error.describe().empty());
}

TEST_CASE("CyclePolicy::Panic throws CyclePanic carrying the key path") {
    QueryEngine engine(CyclePolicy::Panic);
    DerivedQueryT<int> mutual = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            if (self.index() == 0) {
                return ctx.read(mutual, DerivedId{1});
            }
            return ctx.read(mutual, DerivedId{0});
        });

    const auto invoke = [&] {
        static_cast<void>(engine.eval(mutual, DerivedId{0}));
    };
    CHECK_THROWS_AS(invoke(), CyclePanic);
    try {
        invoke();
    } catch (const CyclePanic &panic) {
        CHECK_FALSE(panic.cycle().path.empty());
        CHECK(panic.cycle().path.front() == panic.cycle().path.back());
    }
}

TEST_CASE("reading an unset input throws a logic error") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    auto reader = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) {
            return ctx.get(input, InputId{0});
        });
    const auto invoke = [&] {
        static_cast<void>(engine.eval(reader, DerivedId{0}));
    };
    CHECK_THROWS_AS(invoke(), std::logic_error);
}

TEST_CASE("multiple independent slots coexist in one family") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    int runs = 0;
    auto plus_one = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            ++runs;
            return ctx.get(input, InputId{self.index()}) + 1;
        });

    engine.set_input(input, InputId{0}, 10);
    engine.set_input(input, InputId{1}, 20);
    CHECK(*engine.eval(plus_one, DerivedId{0}) == 11);
    CHECK(*engine.eval(plus_one, DerivedId{1}) == 21);
    CHECK(runs == 2);

    // Only slot 0's input changed.
    engine.set_input(input, InputId{0}, 11);
    CHECK(*engine.eval(plus_one, DerivedId{0}) == 12);
    CHECK(*engine.eval(plus_one, DerivedId{1}) == 21);
    CHECK(runs == 3);
}
