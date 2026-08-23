#include <tooling/incremental/cache_core.hpp>
#include <tooling/incremental/dependency_graph.hpp>
#include <tooling/incremental/import_graph_discovery.hpp>
#include <tooling/incremental/incremental_compiler.hpp>
#include <tooling/incremental/ir_cache.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

    // Test 5: cache_core: CacheKey equality and hashing
    {
        using ahfl::incremental::CacheKey;

        CacheKey a;
        a.project_root_hash = "abc123";
        a.source_path = "src/main.ahfl";
        a.content_hash = 42;
        a.toolchain_fingerprint = "tc-v1";

        CacheKey b = a;
        CacheKey c = a;
        c.content_hash = 99;
        CacheKey d = a;
        d.source_path = "src/other.ahfl";

        const std::hash<CacheKey> hasher{};
        bool equal_keys = (a == b);
        bool equal_hash = (hasher(a) == hasher(b));
        bool different_content = !(a == c);
        bool different_path = !(a == d);
        bool hash_differs = (hasher(a) != hasher(c)) && (hasher(a) != hasher(d));

        check(equal_keys && equal_hash && different_content && different_path && hash_differs,
              "cache_core: CacheKey equality and hashing");
    }

    // Test 6: cache_core: fnv1a64 consistency
    {
        using ahfl::incremental::fnv1a64;
        const auto h1 = fnv1a64("hello");
        const auto h2 = fnv1a64("hello");
        const auto h3 = fnv1a64("world");
        // FNV-1a 64-bit of "hello" is a well-known constant.
        constexpr std::uint64_t kExpectedHello = 0xa430d84680aabd0bULL;
        bool valid = (h1 == h2) && (h1 != h3) && (h1 == kExpectedHello);
        check(valid, "cache_core: fnv1a64 deterministic and matches known vector");
    }

    // Test 7: persistent_cache: store, lookup, invalidate, clear
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_cache_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);

            ahfl::incremental::CacheKey key;
            key.project_root_hash = "deadbeef";
            key.source_path = "src/main.ahfl";
            key.content_hash = 12345;
            key.toolchain_fingerprint = "tc-v1";

            // Miss on empty cache.
            auto miss = cache.lookup(key);
            valid = valid && (miss.kind == ahfl::incremental::PersistentCacheHitKind::Miss);

            // Store and hit.
            ahfl::incremental::PersistentCacheEntry entry;
            entry.key = key;
            entry.source_graph_revision = "graph-v1";
            entry.resolver_snapshot_version = "resolver-v1";
            entry.signature_fingerprint = 777;
            entry.serialized_typed_hir = R"({"typed_hir":{}})";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);

            auto hit = cache.lookup(key);
            valid = valid && (hit.kind == ahfl::incremental::PersistentCacheHitKind::Hit) &&
                    hit.entry.has_value();
            if (hit.entry.has_value()) {
                valid = valid && (hit.entry->key == key) &&
                        (hit.entry->source_graph_revision == "graph-v1") &&
                        (hit.entry->signature_fingerprint == 777) &&
                        (hit.entry->serialized_typed_hir == R"({"typed_hir":{}})");
            }
            valid = valid && (cache.entry_count() == 1);

            // Miss with different content hash.
            auto stale_key = key;
            stale_key.content_hash = 99999;
            auto stale = cache.lookup(stale_key);
            valid = valid && (stale.kind == ahfl::incremental::PersistentCacheHitKind::Miss);

            // Invalidate and miss.
            cache.invalidate(key.source_path);
            auto after_invalidate = cache.lookup(key);
            valid = valid &&
                    (after_invalidate.kind ==
                     ahfl::incremental::PersistentCacheHitKind::Miss) &&
                    (cache.entry_count() == 0);

            // Store again, then clear.
            cache.store(entry);
            valid = valid && (cache.entry_count() == 1);
            cache.clear();
            auto after_clear = cache.lookup(key);
            valid = valid &&
                    (after_clear.kind == ahfl::incremental::PersistentCacheHitKind::Miss) &&
                    (cache.entry_count() == 0);
        }

        // Reopen: a fresh PersistentCache on the same directory should see an
        // empty index (clear persisted).
        {
            ahfl::incremental::PersistentCache reopened(test_dir);
            valid = valid && (reopened.entry_count() == 0);
        }

        fs::remove_all(test_dir);
        check(valid, "persistent_cache: store, lookup, invalidate, clear");
    }

    // Test 8: persistent_cache: persistence across instances
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_persist_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);
            ahfl::incremental::PersistentCacheEntry entry;
            entry.key.project_root_hash = "persist";
            entry.key.source_path = "mod.ahfl";
            entry.key.content_hash = 555;
            entry.key.toolchain_fingerprint = "tc";
            entry.signature_fingerprint = 111;
            entry.serialized_typed_hir = R"({"ir":1})";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);
        }
        {
            ahfl::incremental::PersistentCache reopened(test_dir);
            ahfl::incremental::CacheKey key;
            key.project_root_hash = "persist";
            key.source_path = "mod.ahfl";
            key.content_hash = 555;
            key.toolchain_fingerprint = "tc";
            auto hit = reopened.lookup(key);
            valid = (hit.kind == ahfl::incremental::PersistentCacheHitKind::Hit) &&
                    hit.entry.has_value() &&
                    (hit.entry->signature_fingerprint == 111) &&
                    (hit.entry->serialized_typed_hir == R"({"ir":1})") &&
                    (reopened.entry_count() == 1);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: entries survive across instances");
    }

    // Test 9: persistent_cache: corrupted file falls back to miss
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_corrupt_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);
            ahfl::incremental::PersistentCacheEntry entry;
            entry.key.project_root_hash = "corrupt";
            entry.key.source_path = "x.ahfl";
            entry.key.content_hash = 1;
            entry.key.toolchain_fingerprint = "tc";
            entry.serialized_typed_hir = "{}";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);

            // Corrupt the entry file on disk (every .json that is not index.json).
            for (const auto &file : fs::directory_iterator(test_dir)) {
                if (file.path().filename() == "index.json") {
                    continue;
                }
                std::ofstream(file.path(), std::ios::trunc) << "not json {{{";
            }

            ahfl::incremental::CacheKey key;
            key.project_root_hash = "corrupt";
            key.source_path = "x.ahfl";
            key.content_hash = 1;
            key.toolchain_fingerprint = "tc";
            auto result = cache.lookup(key);
            valid = (result.kind == ahfl::incremental::PersistentCacheHitKind::Miss);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: corrupted entry file falls back to miss");
    }

    // Test 10: import_graph_discovery: discovers import edges from ahfl.toml
    {
        namespace fs = std::filesystem;
        const auto project_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_discovery_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(project_dir);

        // Write a minimal package with two modules where b imports a.
        {
            std::ofstream manifest(project_dir / "ahfl.toml");
            manifest << "manifest_version = 1\n"
                     << "[package]\n"
                     << "name = \"discovery-test\"\n"
                     << "version = \"0.1.0\"\n"
                     << "edition = \"2026\"\n"
                     << "kind = \"library\"\n"
                     << "[module]\n"
                     << "prefix = \"dt\"\n"
                     << "root = \".\"\n"
                     << "[exports]\n"
                     << "modules = [\"a\", \"b\"]\n"
                     << "[targets.lib]\n"
                     << "kind = \"library\"\n"
                     << "entry = \"a.ahfl\"\n"
                     << "[dependencies]\n"
                     << "std = { source = \"sysroot\" }\n";
        }
        {
            std::ofstream source_a(project_dir / "a.ahfl");
            source_a << "module dt::a;\n"
                     << "pub struct Thing {\n"
                     << "    value: int;\n"
                     << "}\n";
        }
        {
            std::ofstream source_b(project_dir / "b.ahfl");
            source_b << "module dt::b;\n"
                     << "import dt::a as a;\n"
                     << "pub struct Box {\n"
                     << "    thing: a::Thing;\n"
                     << "}\n";
        }

        auto result = ahfl::incremental::discover_import_graph(
            project_dir, project_dir / "ahfl.toml");

        bool valid = result.discovered;
        if (!valid) {
            for (const auto &diag : result.diagnostics) {
                std::fprintf(stderr, "  discovery diagnostic: %s\n", diag.c_str());
            }
        }

        // The graph must contain both project modules.
        const auto a_path = (project_dir / "a.ahfl").string();
        const auto b_path = (project_dir / "b.ahfl").string();
        valid = valid && result.graph.has_module(a_path) && result.graph.has_module(b_path);

        // b must depend on a; a must have b as a dependent.
        const auto b_deps = result.graph.dependencies_of(b_path);
        const auto a_dependents = result.graph.dependents_of(a_path);
        const bool b_imports_a =
            std::find(b_deps.begin(), b_deps.end(), a_path) != b_deps.end();
        const bool a_depended_by_b =
            std::find(a_dependents.begin(), a_dependents.end(), b_path) !=
            a_dependents.end();
        valid = valid && b_imports_a && a_depended_by_b;

        // Topological order: a before b.
        const auto topo = result.graph.topological_order();
        const auto a_pos = std::find(topo.begin(), topo.end(), a_path);
        const auto b_pos = std::find(topo.begin(), topo.end(), b_path);
        valid = valid && (a_pos != topo.end()) && (b_pos != topo.end()) &&
                (a_pos < b_pos);

        fs::remove_all(project_dir);
        check(valid, "import_graph_discovery: discovers import edges from ahfl.toml");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
