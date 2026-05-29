#include <tooling/incremental/dependency_graph.hpp>
#include <tooling/incremental/incremental_compiler.hpp>
#include <tooling/incremental/ir_cache.hpp>

#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("Incremental Compilation Tests\n");
    std::printf("==============================\n\n");

    // Test 1: dependency_graph: add modules, check topological order
    {
        ahfl::incremental::DependencyGraph graph;

        ahfl::incremental::ModuleNode a;
        a.module_path = "a.ahfl";
        a.content_hash = 100;
        a.imports = {};

        ahfl::incremental::ModuleNode b;
        b.module_path = "b.ahfl";
        b.content_hash = 200;
        b.imports = {"a.ahfl"};

        ahfl::incremental::ModuleNode c;
        c.module_path = "c.ahfl";
        c.content_hash = 300;
        c.imports = {"b.ahfl"};

        graph.add_module(a);
        graph.add_module(b);
        graph.add_module(c);

        auto order = graph.topological_order();
        bool valid = (graph.module_count() == 3) &&
                     (order.size() == 3) &&
                     // a should come before b, b before c
                     (order[0] == "a.ahfl") &&
                     (order[1] == "b.ahfl") &&
                     (order[2] == "c.ahfl");
        check(valid, "dependency_graph: add modules and check topological order");
    }

    // Test 2: ir_cache: store and lookup (hit vs miss)
    {
        ahfl::incremental::IrCache cache;

        ahfl::incremental::CacheEntry entry;
        entry.module_path = "test.ahfl";
        entry.content_hash = 42;
        entry.serialized_ir = R"({"ir": "test"})";
        entry.cached_at = std::chrono::system_clock::now();

        cache.store(entry);

        auto hit = cache.lookup("test.ahfl", 42);
        auto miss = cache.lookup("unknown.ahfl", 99);
        auto stale = cache.lookup("test.ahfl", 99);

        bool valid = (hit.kind == ahfl::incremental::CacheHitKind::Hit) &&
                     (hit.entry.has_value()) &&
                     (miss.kind == ahfl::incremental::CacheHitKind::Miss) &&
                     (!miss.entry.has_value()) &&
                     (stale.kind == ahfl::incremental::CacheHitKind::Stale) &&
                     (cache.entry_count() == 1) &&
                     (cache.total_size_bytes() == 14);
        check(valid, "ir_cache: store and lookup (hit vs miss vs stale)");
    }

    // Test 3: incremental_compiler: failures are not cached as up-to-date
    {
        ahfl::incremental::DependencyGraph graph;
        ahfl::incremental::IrCache cache;

        ahfl::incremental::ModuleNode a;
        a.module_path = "lib.ahfl";
        a.content_hash = 100;
        a.imports = {};

        ahfl::incremental::ModuleNode b;
        b.module_path = "app.ahfl";
        b.content_hash = 200;
        b.imports = {"lib.ahfl"};

        graph.add_module(a);
        graph.add_module(b);

        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        auto results1 = compiler.compile_changed({"lib.ahfl"});
        auto stats1 = compiler.stats();

        bool first_ok = (results1.size() == 2) &&
                        (stats1.modules_recompiled == 2) &&
                        (stats1.cache_misses == 2) &&
                        (cache.entry_count() == 0);

        compiler.reset_stats();
        auto results2 = compiler.compile_changed({"lib.ahfl"});
        auto stats2 = compiler.stats();

        bool second_ok = (results2.size() == 2) &&
                         (stats2.cache_hits == 0) &&
                         (stats2.cache_misses == 2) &&
                         (cache.entry_count() == 0);

        check(first_ok && second_ok,
              "incremental_compiler: compile_changed does not cache failures");
    }

    // Test 4: dependency_graph: cycle detection
    {
        ahfl::incremental::DependencyGraph graph;

        ahfl::incremental::ModuleNode a;
        a.module_path = "x.ahfl";
        a.content_hash = 1;
        a.imports = {"y.ahfl"};

        ahfl::incremental::ModuleNode b;
        b.module_path = "y.ahfl";
        b.content_hash = 2;
        b.imports = {"x.ahfl"};

        graph.add_module(a);
        graph.add_module(b);

        bool has_cycle = graph.has_cycle();

        // Also test no-cycle case
        ahfl::incremental::DependencyGraph graph2;
        ahfl::incremental::ModuleNode c;
        c.module_path = "p.ahfl";
        c.content_hash = 1;
        c.imports = {};

        ahfl::incremental::ModuleNode d;
        d.module_path = "q.ahfl";
        d.content_hash = 2;
        d.imports = {"p.ahfl"};

        graph2.add_module(c);
        graph2.add_module(d);

        bool no_cycle = !graph2.has_cycle();

        check(has_cycle && no_cycle, "dependency_graph: cycle detection");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
