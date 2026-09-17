#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/base/query/query_engine.hpp"

#include <cstdint>
#include <stdexcept>

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
        : engine(CyclePolicy::Error), x(engine.register_input<int>()),
          y(engine.register_input<int>()),
          double_x(engine.register_derived<int>([this](QueryContext &ctx, DerivedId) {
              ++double_x_runs;
              return ctx.get(x, InputId{0}) * 2;
          })),
          double_y(engine.register_derived<int>([this](QueryContext &ctx, DerivedId) {
              ++double_y_runs;
              return ctx.get(y, InputId{0}) * 2;
          })),
          chain_b(engine.register_derived<int>([this](QueryContext &ctx, DerivedId) {
              ++chain_b_runs;
              return ctx.get(x, InputId{0}) + 10;
          })),
          chain_a(engine.register_derived<int>([this](QueryContext &ctx, DerivedId) {
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

    const SlotInfo info = g.engine.inspect_slot(g.double_x.family(), 0);
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
    CHECK(g.engine.inspect_slot(g.double_y.family(), 0).state == SlotState::Verified);
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
    CHECK(*updated == 18);      // 7 + 10 + 1
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
    CHECK(g.engine.inspect_slot(g.chain_a.family(), 0).state == SlotState::Clean);
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

    const SlotInfo input_info = g.engine.inspect_slot(g.x.family(), 0);
    CHECK(input_info.changed_at == 2);
    CHECK(input_info.verified_at == 2);
    CHECK(input_info.has_value);

    const SlotInfo y_info = g.engine.inspect_slot(g.y.family(), 0);
    CHECK(y_info.changed_at == 3);

    // Never touched slot exposes defaults.
    const SlotInfo absent = g.engine.inspect_slot(g.double_x.family(), 42);
    CHECK_FALSE(absent.has_value);
    CHECK(absent.state == SlotState::Dirty);
}

TEST_CASE("unchanged dependency output keeps a transitive dependent green") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    // derived returns 1 for every positive input: its VALUE is stable even
    // though x itself changes.
    auto clamped = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) { return ctx.get(input, InputId{0}) > 0 ? 1 : 0; });
    auto above = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) { return ctx.read(clamped, DerivedId{0}) + 100; });

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
    auto chosen = engine.register_derived<int>([&](QueryContext &ctx, DerivedId) {
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
    CHECK(engine.inspect_slot(chosen.family(), 0).state == SlotState::Verified);
}

TEST_CASE("CyclePolicy::Error reports the closed key path") {
    QueryEngine engine(CyclePolicy::Error);
    // Slot 0 and slot 1 of one family depend on each other.
    DerivedQueryT<int> mutual =
        engine.register_derived<int>([&](QueryContext &ctx, DerivedId self) {
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
    DerivedQueryT<int> mutual =
        engine.register_derived<int>([&](QueryContext &ctx, DerivedId self) {
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
        [&](QueryContext &ctx, DerivedId) { return ctx.get(input, InputId{0}); });
    const auto invoke = [&] {
        static_cast<void>(engine.eval(reader, DerivedId{0}));
    };
    CHECK_THROWS_AS(invoke(), std::logic_error);
}

TEST_CASE("multiple independent slots coexist in one family") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    int runs = 0;
    auto plus_one = engine.register_derived<int>([&](QueryContext &ctx, DerivedId self) {
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

TEST_CASE("a throwing compute invalidates the memo and recovers on input change") {
    QueryEngine engine;
    auto gate = engine.register_input<int>();
    auto x = engine.register_input<int>();
    int runs = 0;
    auto a = engine.register_derived<int>([&](QueryContext &ctx, DerivedId) -> int {
        ++runs;
        if (ctx.get(gate, InputId{0}) == 1) {
            throw std::runtime_error("compute failure");
        }
        return ctx.get(x, InputId{0});
    });

    engine.set_input(gate, InputId{0}, 0);
    engine.set_input(x, InputId{0}, 10);
    {
        const auto first = engine.eval(a, DerivedId{0});
        REQUIRE(first.has_value());
        CHECK(*first == 10);
        CHECK(runs == 1);
    }

    // Gate the compute to throw; the exception crosses the eval boundary.
    engine.set_input(gate, InputId{0}, 1);
    CHECK_THROWS_AS(static_cast<void>(engine.eval(a, DerivedId{0})), std::runtime_error);
    CHECK(runs == 2);

    // Retrying at the same failing inputs must run the compute again: never
    // serve the pre-failure memo, and never falsely mark it Verified.
    CHECK_THROWS_AS(static_cast<void>(engine.eval(a, DerivedId{0})), std::runtime_error);
    CHECK(runs == 3);
    {
        const SlotInfo info = engine.inspect_slot(a.family(), 0);
        CHECK_FALSE(info.has_value);
        CHECK(info.state == SlotState::Dirty);
    }

    // Recover and change the other input: the fresh value must come through
    // (the reverse edges survived the failed attempt).
    engine.set_input(gate, InputId{0}, 0);
    engine.set_input(x, InputId{0}, 20);
    {
        const auto recovered = engine.eval(a, DerivedId{0});
        REQUIRE(recovered.has_value());
        CHECK(*recovered == 20);
        CHECK(runs == 4);
    }
}

TEST_CASE("a transitive dependent recomputes after a dependency failure recovers") {
    QueryEngine engine;
    auto gate = engine.register_input<int>();
    auto x = engine.register_input<int>();
    int base_runs = 0;
    int above_runs = 0;
    auto base = engine.register_derived<int>([&](QueryContext &ctx, DerivedId) -> int {
        ++base_runs;
        if (ctx.get(gate, InputId{0}) == 1) {
            throw std::runtime_error("base failure");
        }
        return ctx.get(x, InputId{0});
    });
    auto above = engine.register_derived<int>([&](QueryContext &ctx, DerivedId) -> int {
        ++above_runs;
        return ctx.read(base, DerivedId{0}) + 1;
    });

    engine.set_input(gate, InputId{0}, 0);
    engine.set_input(x, InputId{0}, 10);
    REQUIRE(*engine.eval(above, DerivedId{0}) == 11);
    CHECK(base_runs == 1);
    CHECK(above_runs == 1);

    engine.set_input(gate, InputId{0}, 1);
    CHECK_THROWS_AS(static_cast<void>(engine.eval(above, DerivedId{0})), std::runtime_error);

    engine.set_input(gate, InputId{0}, 0);
    engine.set_input(x, InputId{0}, 40);
    // Both levels must recompute; above must not keep the memo from rev1.
    const auto recovered = engine.eval(above, DerivedId{0});
    REQUIRE(recovered.has_value());
    CHECK(*recovered == 41);
    CHECK(base_runs == 3);
    CHECK(above_runs == 2);
}

TEST_CASE("a cycle formed by a dependency flip keeps reporting on retries (Error)") {
    QueryEngine engine(CyclePolicy::Error);
    auto selector = engine.register_input<int>();
    // One derived family: slot 0 = D, slot 1 = C.
    // selector == 0: D = 1; C = D + 10 (acyclic).
    // selector == 1: D reads C, so C -> D -> C is a cycle.
    DerivedQueryT<int> mutual =
        engine.register_derived<int>([&](QueryContext &ctx, DerivedId self) -> int {
            if (self.index() == 0) {
                if (ctx.get(selector, InputId{0}) == 0) {
                    return 1;
                }
                return ctx.read(mutual, DerivedId{1});
            }
            return ctx.read(mutual, DerivedId{0}) + 10;
        });

    engine.set_input(selector, InputId{0}, 0);
    {
        const auto acyclic = engine.eval(mutual, DerivedId{1});
        REQUIRE(acyclic.has_value());
        CHECK(*acyclic == 11);
    }

    engine.set_input(selector, InputId{0}, 1);
    const auto assert_cycle = [&] {
        const auto result = engine.eval(mutual, DerivedId{1});
        REQUIRE_FALSE(result.has_value());
        const CycleError &error = result.error();
        // Closed path through every in-progress frame, length at least 3.
        CHECK(error.path.size() >= 3);
        CHECK(error.path.front() == error.path.back());
        CHECK_FALSE(error.describe().empty());
        CHECK(error.describe().find("->") != std::string::npos);
        CHECK_FALSE(error.describe().ends_with("->"));
    };
    assert_cycle(); // first eval after flip
    assert_cycle(); // immediate retry at the same revision
    assert_cycle(); // third retry

    // No slot involved in the cycle is falsely Verified at the current
    // revision after the errors.
    const Revision current = engine.revision();
    for (std::size_t slot = 0; slot < 2; ++slot) {
        const SlotInfo info = engine.inspect_slot(mutual.family(), slot);
        if (info.state == SlotState::Verified) {
            CHECK(info.verified_at != current);
        }
    }
}

TEST_CASE("a cycle formed by a dependency flip keeps throwing on retries (Panic)") {
    QueryEngine engine(CyclePolicy::Panic);
    auto selector = engine.register_input<int>();
    DerivedQueryT<int> mutual =
        engine.register_derived<int>([&](QueryContext &ctx, DerivedId self) -> int {
            if (self.index() == 0) {
                if (ctx.get(selector, InputId{0}) == 0) {
                    return 1;
                }
                return ctx.read(mutual, DerivedId{1});
            }
            return ctx.read(mutual, DerivedId{0}) + 10;
        });

    engine.set_input(selector, InputId{0}, 0);
    REQUIRE(*engine.eval(mutual, DerivedId{1}) == 11);

    engine.set_input(selector, InputId{0}, 1);
    const auto invoke = [&] {
        static_cast<void>(engine.eval(mutual, DerivedId{1}));
    };
    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK_THROWS_AS(invoke(), CyclePanic);
        try {
            invoke();
            FAIL("expected CyclePanic");
        } catch (const CyclePanic &panic) {
            const auto &path = panic.cycle().path;
            CHECK(path.size() >= 3);
            CHECK(path.front() == path.back());
            const std::string text = panic.cycle().describe();
            CHECK_FALSE(text.ends_with("->"));
        }
    }
}

TEST_CASE("a green target whose dependency recomputes equal still counts a memo hit") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    auto clamped = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) { return ctx.get(input, InputId{0}) > 0 ? 1 : 0; });
    auto above = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) { return ctx.read(clamped, DerivedId{0}) + 100; });

    engine.set_input(input, InputId{0}, 1);
    REQUIRE(*engine.eval(above, DerivedId{0}) == 101);
    const std::uint64_t hits_before = engine.stats().memo_hits;

    // clamped recomputes (changed input) with an equal value; above itself
    // stays green and must count as a memo hit.
    engine.set_input(input, InputId{0}, 2);
    REQUIRE(*engine.eval(above, DerivedId{0}) == 101);
    CHECK(engine.stats().memo_hits == hits_before + 1);
}

TEST_CASE("inspect_slot on an unregistered family fails closed") {
    QueryEngine engine;
    auto input = engine.register_input<int>();
    engine.set_input(input, InputId{0}, 7);
    const SlotInfo info = engine.inspect_slot(FamilyId{9999}, 0);
    CHECK_FALSE(info.has_value);
    CHECK(info.state == SlotState::Dirty);
    CHECK(info.verified_at == 0);
}

namespace {

// One derived family whose slot 0 solves f = (f + input) / 2 by coinductive
// iteration; the conservative assumption is 0.
struct ContractingGraph {
    QueryEngine engine;
    InputQueryT<int> x;
    DerivedQueryT<int> f;
    int runs = 0;

    ContractingGraph()
        : engine(CyclePolicy::Coinductive), x(engine.register_input<int>()),
          f(engine.register_derived<int>(
              [this](QueryContext &ctx, DerivedId self) {
                  ++runs;
                  return (ctx.read(f, self) + ctx.get(x, InputId{0})) / 2;
              },
              [](DerivedId) { return 0; })) {}
};

} // namespace

TEST_CASE("CyclePolicy::Error on self-recursion caches no value") {
    QueryEngine engine(CyclePolicy::Error);
    DerivedQueryT<int> self = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId me) { return ctx.read(self, me); });

    auto result = engine.eval(self, DerivedId{0});
    REQUIRE_FALSE(result.has_value());
    const CycleError &error = result.error();
    CHECK(error.kind == CycleErrorKind::Reentrant);
    // Exact closed path for a direct self-loop: key 0 -> key 0.
    REQUIRE(error.path.size() == 2);
    CHECK(error.path.front() == error.path.back());
    CHECK(error.path.front().family == self.family());
    CHECK(std::holds_alternative<DerivedId>(error.path.front().slot));
    CHECK(std::get<DerivedId>(error.path.front().slot).index() == 0);

    // Nothing was cached: no committed recomputation, slot has no value.
    CHECK(engine.stats().recomputations == 0);
    const SlotInfo info = engine.inspect_slot(self.family(), 0);
    CHECK_FALSE(info.has_value);
    CHECK(info.state == SlotState::Dirty);
}

TEST_CASE("Coinductive policy on f(n)=f(n) settles on the assumption") {
    QueryEngine engine(CyclePolicy::Coinductive);
    DerivedQueryT<int> self = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId me) { return ctx.read(self, me); },
        [](DerivedId) { return 42; });

    auto result = engine.eval(self, DerivedId{0});
    REQUIRE(result.has_value());
    CHECK(*result == 42); // the conservative assumption IS the fixed value

    // Exactly one equation pass: the iterate is already a fixed point.
    CHECK(engine.stats().fixpoint_iterations == 1);
    CHECK(engine.stats().coinductive_assumptions >= 2);
    const SlotInfo info = engine.inspect_slot(self.family(), 0);
    CHECK(info.state == SlotState::Clean);
    CHECK(info.verified_at == engine.revision());

    // Re-eval serves the settled memo without restarting the fixpoint.
    const std::uint64_t iterations_before = engine.stats().fixpoint_iterations;
    CHECK(*engine.eval(self, DerivedId{0}) == 42);
    CHECK(engine.stats().fixpoint_iterations == iterations_before);
}

TEST_CASE("Coinductive policy iterates a contracting self-cycle to fixpoint") {
    ContractingGraph g;
    g.engine.set_input(g.x, InputId{0}, 10);

    auto result = g.engine.eval(g.f, DerivedId{0});
    REQUIRE(result.has_value());
    // 0 -> 5 -> 7 -> 8 -> 9 -> 9: the greatest fixed point of the integer
    // contraction settles at 9, bounded by the iteration cap.
    CHECK(*result == 9);
    CHECK(g.engine.stats().fixpoint_iterations >= 2);
    CHECK(g.engine.stats().fixpoint_iterations <= QueryEngineOptions{}.coinductive_iteration_cap);
    CHECK(g.engine.inspect_slot(g.f.family(), 0).state == SlotState::Clean);

    // The memo is stable on re-eval: no extra passes, no extra computes.
    const std::uint64_t runs_before = static_cast<std::uint64_t>(g.runs);
    CHECK(*g.engine.eval(g.f, DerivedId{0}) == 9);
    CHECK(static_cast<std::uint64_t>(g.runs) == runs_before);
}

TEST_CASE("coinductive mutual cycle converges and re-runs on input change") {
    // A = (A + B) / 2
    // B = (B + A + x) / 2
    // Integer truncation admits a two-element fixed-point band, so the test
    // asserts both the deterministic Gauss-Seidel result and that the settled
    // values satisfy the equations (a genuine fixed point, not the seeds).
    QueryEngine engine(CyclePolicy::Coinductive);
    auto x = engine.register_input<int>();
    DerivedQueryT<int> ab = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            const int a = ctx.read(ab, DerivedId{0});
            const int b = ctx.read(ab, DerivedId{1});
            if (self.index() == 0) {
                return (a + b) / 2;
            }
            return (b + a + ctx.get(x, InputId{0})) / 2;
        },
        [](DerivedId key) { return key.index() == 0 ? 0 : 10; });

    const auto assert_fixed_point = [&](int input) {
        const int a = *engine.eval(ab, DerivedId{0});
        const int b = *engine.eval(ab, DerivedId{1});
        CHECK(a == (a + b) / 2);
        CHECK(b == (b + a + input) / 2);
        // Settled away from the conservative seeds.
        CHECK((a != 0 || b != 10));
        CHECK(engine.inspect_slot(ab.family(), 0).state == SlotState::Clean);
        CHECK(engine.inspect_slot(ab.family(), 1).state == SlotState::Clean);
        return std::pair<int, int>{a, b};
    };

    engine.set_input(x, InputId{0}, 0);
    // Deterministic Gauss-Seidel settlement for the (0, 10) seeds at x = 0.
    CHECK(assert_fixed_point(0) == std::pair<int, int>{6, 6});
    const std::uint64_t passes_after_settle = engine.stats().fixpoint_iterations;
    CHECK(passes_after_settle >= 1);

    // Input change dirties the SCC through the recorded dependency edges;
    // re-eval must run the fixpoint again, not reuse the old memo.
    engine.set_input(x, InputId{0}, 2);
    CHECK(engine.inspect_slot(ab.family(), 0).state == SlotState::Dirty);
    const auto [a2, b2] = assert_fixed_point(2);
    CHECK(a2 == 7);
    CHECK(b2 == 8);
    CHECK(engine.stats().fixpoint_iterations > passes_after_settle);
}

TEST_CASE("coinductive divergence is reported when the cap is exhausted") {
    QueryEngine engine(CyclePolicy::Coinductive,
                       QueryEngineOptions{.coinductive_iteration_cap = 3});
    DerivedQueryT<int> growing = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId me) {
            return ctx.read(growing, me) + 1; // 0 -> 1 -> 2 -> ... never settles
        },
        [](DerivedId) { return 0; });

    auto result = engine.eval(growing, DerivedId{0});
    REQUIRE_FALSE(result.has_value());
    const CycleError &error = result.error();
    CHECK(error.kind == CycleErrorKind::Diverged);
    CHECK(error.iterations == 3);
    CHECK_FALSE(error.path.empty());
    CHECK(error.path.front() == error.path.back());
    CHECK(error.describe().find("did not converge") != std::string::npos);

    // A divergent attempt leaves no cached value.
    const SlotInfo info = engine.inspect_slot(growing.family(), 0);
    CHECK_FALSE(info.has_value);
    CHECK(info.state == SlotState::Dirty);

    // Retry is deterministic and bounded: it cannot loop forever.
    auto retry = engine.eval(growing, DerivedId{0});
    REQUIRE_FALSE(retry.has_value());
    CHECK(retry.error().kind == CycleErrorKind::Diverged);
}

TEST_CASE("coinductive cycle without an assumption hook fails closed") {
    QueryEngine engine(CyclePolicy::Coinductive);
    // Registered through the plain overload: no conservative seed exists.
    DerivedQueryT<int> self = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId me) { return ctx.read(self, me); });

    auto result = engine.eval(self, DerivedId{0});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == CycleErrorKind::MissingAssumption);
    CHECK_FALSE(result.error().describe().empty());
    CHECK_FALSE(engine.inspect_slot(self.family(), 0).has_value);
}

TEST_CASE("coinductive SCC expands through a value-gated external frame") {
    // Equations (B's assumed value selects the branch that pulls C in):
    //   A = B > 5 ? C : B
    //   B = A
    //   C = B
    // Discovery (seeds not visible yet) follows A -> B -> A, so the initial
    // SCC is {A, B}; C joins only during the first pass once B's iterate
    // selects the C branch, exercising FixpointMembership expansion.
    QueryEngine engine(CyclePolicy::Coinductive);
    DerivedQueryT<int> *abp = nullptr; // registration-order indirection
    DerivedQueryT<int> c = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) {
            // C = B: reading back into the SCC closes the enlarged cycle.
            return ctx.read(*abp, DerivedId{1});
        },
        [](DerivedId) { return 10; });
    DerivedQueryT<int> ab = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            if (self.index() == 0) {
                const int b = ctx.read(ab, DerivedId{1});
                if (b > 5) {
                    return ctx.read(c, DerivedId{0});
                }
                return b;
            }
            return ctx.read(ab, DerivedId{0});
        },
        [](DerivedId key) { return key.index() == 0 ? 0 : 10; });
    abp = &ab;

    auto result = engine.eval(ab, DerivedId{0});
    REQUIRE(result.has_value());
    CHECK(*result == 10); // A = C = B = 10 after expansion + reseed
    CHECK(*engine.eval(ab, DerivedId{1}) == 10);
    CHECK(*engine.eval(c, DerivedId{0}) == 10);
    CHECK(engine.inspect_slot(ab.family(), 0).state == SlotState::Clean);
    CHECK(engine.inspect_slot(c.family(), 0).state == SlotState::Clean);
}

TEST_CASE("SCC expansion fails closed when the joining family has no hook") {
    // Same discovery shape, but C (registered without an assumption hook)
    // joins the SCC mid-pass: the attempt must fail MissingAssumption and
    // leave nothing cached, rather than iterating with an unseeded slot.
    QueryEngine engine(CyclePolicy::Coinductive);
    DerivedQueryT<int> *abp = nullptr;
    DerivedQueryT<int> c = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId) { return ctx.read(*abp, DerivedId{1}); });
    DerivedQueryT<int> ab = engine.register_derived<int>(
        [&](QueryContext &ctx, DerivedId self) {
            if (self.index() == 0) {
                const int b = ctx.read(ab, DerivedId{1});
                if (b > 5) {
                    return ctx.read(c, DerivedId{0});
                }
                return b;
            }
            return ctx.read(ab, DerivedId{0});
        },
        [](DerivedId key) { return key.index() == 0 ? 0 : 10; });
    abp = &ab;

    auto result = engine.eval(ab, DerivedId{0});
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == CycleErrorKind::MissingAssumption);
    CHECK_FALSE(engine.inspect_slot(ab.family(), 0).has_value);
    CHECK_FALSE(engine.inspect_slot(c.family(), 0).has_value);
}
