#include <compiler/project_discovery/discovery.hpp>
#include <tooling/incremental/cache_core.hpp>
#include <tooling/incremental/daemon.hpp>
#include <tooling/incremental/dependency_graph.hpp>
#include <tooling/incremental/import_graph_discovery.hpp>
#include <tooling/incremental/incremental_compiler.hpp>
#include <tooling/incremental/ir_cache.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

// Find the compile status for a module path in a result vector. Returns
// Failed when the path is absent so assertions fail loudly instead of
// silently matching a default.
static ahfl::incremental::CompileStatus
status_of(const std::vector<ahfl::incremental::CompileResult> &results,
          const std::string &path) {
    for (const auto &r : results) {
        if (r.module_path == path) {
            return r.status;
        }
    }
    return ahfl::incremental::CompileStatus::Failed;
}

// Counts non-overlapping occurrences of `needle` in `text`. Used to assert
// on the daemon's JSON Lines output without pulling in a JSON parser.
static std::size_t count_occurrences(const std::string &text, const std::string &needle) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// Creates a two-module project (b imports a) under a fresh temp directory and
// returns the normalized absolute module paths. Shared by the daemon tests.
struct DaemonFixture {
    std::filesystem::path project_dir;
    std::string a_path;
    std::string b_path;
};

static DaemonFixture make_daemon_fixture() {
    namespace fs = std::filesystem;
    const auto project_dir =
        fs::temp_directory_path() /
        ("ahfl_daemon_test_" +
         std::to_string(static_cast<long long>(
             std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::create_directories(project_dir);

    std::ofstream(project_dir / "a.ahfl")
        << "module fp::a;\n"
        << "pub struct Config {\n    width: Int;\n}\n";
    std::ofstream(project_dir / "b.ahfl")
        << "module fp::b;\n"
        << "import fp::a as a;\n"
        << "pub struct Wrapper {\n    count: Int;\n}\n";

    DaemonFixture fixture;
    fixture.project_dir = project_dir;
    fixture.a_path =
        ahfl::project_discovery::normalize_project_path(project_dir / "a.ahfl").string();
    fixture.b_path =
        ahfl::project_discovery::normalize_project_path(project_dir / "b.ahfl").string();
    return fixture;
}

static ahfl::incremental::DependencyGraph
make_daemon_graph(const DaemonFixture &fixture) {
    ahfl::incremental::DependencyGraph graph;
    ahfl::incremental::ModuleNode node_a;
    node_a.module_path = fixture.a_path;
    node_a.imports = {};
    ahfl::incremental::ModuleNode node_b;
    node_b.module_path = fixture.b_path;
    node_b.imports = {fixture.a_path};
    graph.add_module(node_a);
    graph.add_module(node_b);
    return graph;
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

    // Test 11: signature fingerprint unchanged -> downstream skip.
    // A comment-only edit to A flips its content hash (so A recompiles) but
    // leaves the TypeEnvironment fingerprint untouched, so B must keep its
    // cache entry and hit on the next pass.
    {
        namespace fs = std::filesystem;
        const auto project_dir =
            fs::temp_directory_path() /
            ("ahfl_fp_skip_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(project_dir);

        const auto a_path = (project_dir / "a.ahfl").string();
        const auto b_path = (project_dir / "b.ahfl").string();

        const auto write_a = [&](const std::string &body) {
            std::ofstream(project_dir / "a.ahfl") << "module fp::a;\n" << body;
        };
        const auto write_b = [&]() {
            std::ofstream(project_dir / "b.ahfl")
                << "module fp::b;\n"
                << "import fp::a as a;\n"
                << "pub struct Wrapper {\n"
                << "    count: Int;\n"
                << "}\n";
        };

        write_a("pub struct Config {\n    width: Int;\n}\n");
        write_b();

        ahfl::incremental::DependencyGraph graph;
        ahfl::incremental::ModuleNode node_a;
        node_a.module_path = a_path;
        node_a.imports = {};
        ahfl::incremental::ModuleNode node_b;
        node_b.module_path = b_path;
        node_b.imports = {a_path};
        graph.add_module(node_a);
        graph.add_module(node_b);

        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        // First pass: cold cache, both modules recompile.
        const auto r1 = compiler.compile_changed({a_path});
        const bool first_ok =
            (r1.size() == 2) &&
            (status_of(r1, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r1, b_path) == ahfl::incremental::CompileStatus::Recompiled);

        // Comment-only edit: content hash flips, fingerprint stays.
        write_a("// tuning note\npub struct Config {\n    width: Int;\n}\n");
        compiler.reset_stats();
        const auto r2 = compiler.compile_changed({a_path});
        const auto stats2 = compiler.stats();

        const bool second_ok =
            (status_of(r2, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r2, b_path) == ahfl::incremental::CompileStatus::UpToDate) &&
            (stats2.fingerprint_skipped == 1) &&
            (stats2.cache_hits == 1) &&
            (cache.entry_count() == 2);

        fs::remove_all(project_dir);
        check(first_ok && second_ok,
              "incremental_compiler: fingerprint unchanged skips downstream rebuild");
    }

    // Test 12: signature fingerprint changed -> downstream rebuild.
    // Adding a struct field to A changes its fingerprint, so B's cached IR
    // (compiled against the old A) is stale and must be invalidated.
    {
        namespace fs = std::filesystem;
        const auto project_dir =
            fs::temp_directory_path() /
            ("ahfl_fp_change_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(project_dir);

        const auto a_path = (project_dir / "a.ahfl").string();
        const auto b_path = (project_dir / "b.ahfl").string();

        const auto write_a = [&](const std::string &body) {
            std::ofstream(project_dir / "a.ahfl") << "module fp::a;\n" << body;
        };
        const auto write_b = [&]() {
            std::ofstream(project_dir / "b.ahfl")
                << "module fp::b;\n"
                << "import fp::a as a;\n"
                << "pub struct Wrapper {\n"
                << "    count: Int;\n"
                << "}\n";
        };

        write_a("pub struct Config {\n    width: Int;\n}\n");
        write_b();

        ahfl::incremental::DependencyGraph graph;
        ahfl::incremental::ModuleNode node_a;
        node_a.module_path = a_path;
        node_a.imports = {};
        ahfl::incremental::ModuleNode node_b;
        node_b.module_path = b_path;
        node_b.imports = {a_path};
        graph.add_module(node_a);
        graph.add_module(node_b);

        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        const auto r1 = compiler.compile_changed({a_path});
        const bool first_ok =
            (r1.size() == 2) &&
            (status_of(r1, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r1, b_path) == ahfl::incremental::CompileStatus::Recompiled);

        // Signature edit: add a field. Fingerprint changes.
        write_a("pub struct Config {\n    width: Int;\n    height: Int;\n}\n");
        compiler.reset_stats();
        const auto r2 = compiler.compile_changed({a_path});
        const auto stats2 = compiler.stats();

        const bool second_ok =
            (status_of(r2, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r2, b_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (stats2.fingerprint_skipped == 0) &&
            (stats2.cache_hits == 0);

        fs::remove_all(project_dir);
        check(first_ok && second_ok,
              "incremental_compiler: fingerprint changed rebuilds downstream");
    }

    // Test 13: no old fingerprint -> downstream rebuild.
    // When A's cache entry is missing (cold in-memory cache), there is no
    // old fingerprint to compare against, so dependents are invalidated
    // conservatively even though A's content is unchanged.
    {
        namespace fs = std::filesystem;
        const auto project_dir =
            fs::temp_directory_path() /
            ("ahfl_fp_missing_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(project_dir);

        const auto a_path = (project_dir / "a.ahfl").string();
        const auto b_path = (project_dir / "b.ahfl").string();

        std::ofstream(project_dir / "a.ahfl")
            << "module fp::a;\n"
            << "pub struct Config {\n    width: Int;\n}\n";
        std::ofstream(project_dir / "b.ahfl")
            << "module fp::b;\n"
            << "import fp::a as a;\n"
            << "pub struct Wrapper {\n"
            << "    count: Int;\n"
            << "}\n";

        ahfl::incremental::DependencyGraph graph;
        ahfl::incremental::ModuleNode node_a;
        node_a.module_path = a_path;
        node_a.imports = {};
        ahfl::incremental::ModuleNode node_b;
        node_b.module_path = b_path;
        node_b.imports = {a_path};
        graph.add_module(node_a);
        graph.add_module(node_b);

        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        const auto r1 = compiler.compile_changed({a_path});
        const bool first_ok =
            (r1.size() == 2) &&
            (status_of(r1, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r1, b_path) == ahfl::incremental::CompileStatus::Recompiled);

        // Drop A's cache entry, then recompile with unchanged content.
        cache.invalidate(a_path);
        compiler.reset_stats();
        const auto r2 = compiler.compile_changed({a_path});
        const auto stats2 = compiler.stats();

        const bool second_ok =
            (status_of(r2, a_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (status_of(r2, b_path) == ahfl::incremental::CompileStatus::Recompiled) &&
            (stats2.fingerprint_skipped == 0);

        fs::remove_all(project_dir);
        check(first_ok && second_ok,
              "incremental_compiler: missing old fingerprint rebuilds downstream");
    }

    // Test 14: daemon: didChange invalidates and emits stats.
    // A didChange notification for A must compile A plus its transitive
    // dependent B and emit exactly one stats object on stdout.
    {
        auto fixture = make_daemon_fixture();
        auto graph = make_daemon_graph(fixture);
        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        std::ostringstream input_text;
        input_text << "{\"type\":\"didChange\",\"uri\":\"file://" << fixture.a_path
                   << "\"}\n";
        std::istringstream input(input_text.str());
        std::ostringstream output;
        ahfl::incremental::run_daemon(compiler, graph, input, output);

        const auto out = output.str();
        const auto stats = compiler.stats();
        const bool valid =
            (count_occurrences(out, "\"type\":\"stats\"") == 1) &&
            (count_occurrences(out, "\"type\":\"error\"") == 0) &&
            (stats.modules_recompiled == 2) &&
            (stats.cache_misses == 2) &&
            (cache.entry_count() == 2);

        std::filesystem::remove_all(fixture.project_dir);
        check(valid, "daemon: didChange invalidates and emits stats");
    }

    // Test 15: daemon: didClose is a no-op.
    // didClose must not invalidate anything and must not emit output; a
    // subsequent didChange with unchanged content must hit the cache.
    {
        auto fixture = make_daemon_fixture();
        auto graph = make_daemon_graph(fixture);
        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        std::ostringstream input_text;
        input_text << "{\"type\":\"didChange\",\"uri\":\"file://" << fixture.a_path
                   << "\"}\n"
                   << "{\"type\":\"didClose\",\"uri\":\"file://" << fixture.a_path
                   << "\"}\n"
                   << "{\"type\":\"didChange\",\"uri\":\"file://" << fixture.a_path
                   << "\"}\n";
        std::istringstream input(input_text.str());
        std::ostringstream output;
        ahfl::incremental::run_daemon(compiler, graph, input, output);

        const auto out = output.str();
        const auto stats = compiler.stats();
        const bool valid =
            // One stats line per didChange, none for didClose.
            (count_occurrences(out, "\"type\":\"stats\"") == 2) &&
            (count_occurrences(out, "\"type\":\"error\"") == 0) &&
            // Nothing was invalidated: both modules still cached.
            (cache.entry_count() == 2) &&
            // First pass compiled both; second pass hit both.
            (stats.modules_recompiled == 2) &&
            (stats.cache_hits == 2) &&
            (stats.cache_misses == 2);

        std::filesystem::remove_all(fixture.project_dir);
        check(valid, "daemon: didClose is a no-op");
    }

    // Test 16: daemon: malformed JSON and unknown types report errors and
    // the loop keeps processing subsequent notifications.
    {
        auto fixture = make_daemon_fixture();
        auto graph = make_daemon_graph(fixture);
        ahfl::incremental::IrCache cache;
        ahfl::incremental::IncrementalCompiler compiler(graph, cache);

        std::ostringstream input_text;
        input_text << "not json {\n"
                   << "{\"type\":\"wat\",\"uri\":\"file:///x\"}\n"
                   << "{\"type\":\"didChange\"}\n"
                   << "{\"type\":\"didChange\",\"uri\":\"file://" << fixture.a_path
                   << "\"}\n";
        std::istringstream input(input_text.str());
        std::ostringstream output;
        ahfl::incremental::run_daemon(compiler, graph, input, output);

        const auto out = output.str();
        const auto stats = compiler.stats();
        const bool valid =
            // Three error lines: malformed, unknown type, missing uri.
            (count_occurrences(out, "\"type\":\"error\"") == 3) &&
            (out.find("malformed JSON") != std::string::npos) &&
            (out.find("unknown type: wat") != std::string::npos) &&
            (out.find("missing 'uri' field") != std::string::npos) &&
            // The final valid notification still compiled both modules.
            (count_occurrences(out, "\"type\":\"stats\"") == 1) &&
            (stats.modules_recompiled == 2) &&
            (cache.entry_count() == 2);

        std::filesystem::remove_all(fixture.project_dir);
        check(valid, "daemon: malformed input reports errors and recovers");
    }

    // Test 17: persistent_cache: TTL eviction removes stale entries.
    // Entries whose last_accessed is older than the TTL are evicted by an
    // explicit evict_expired call and lazily by lookup().
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_ttl_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);

            auto make_entry = [](std::string_view path) {
                ahfl::incremental::PersistentCacheEntry entry;
                entry.key.project_root_hash = "ttl";
                entry.key.source_path = std::string{path};
                entry.key.content_hash = 1;
                entry.key.toolchain_fingerprint = "tc";
                entry.serialized_typed_hir = "{}";
                entry.cached_at = std::chrono::system_clock::now();
                return entry;
            };
            auto key_for = [](std::string_view path) {
                ahfl::incremental::CacheKey key;
                key.project_root_hash = "ttl";
                key.source_path = std::string{path};
                key.content_hash = 1;
                key.toolchain_fingerprint = "tc";
                return key;
            };

            cache.store(make_entry("old.ahfl"));
            cache.store(make_entry("fresh.ahfl"));

            const auto now = std::chrono::system_clock::now();
            cache.set_last_accessed_for_test("old.ahfl", now - std::chrono::days(8));
            cache.set_last_accessed_for_test("fresh.ahfl", now - std::chrono::days(1));

            // Explicit eviction with the default 7-day TTL drops only the
            // stale entry.
            cache.evict_expired(std::chrono::days(7));
            valid = valid && (cache.entry_count() == 1) &&
                    (cache.lookup(key_for("old.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Miss) &&
                    (cache.lookup(key_for("fresh.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Hit);

            // Lazy eviction: lookup() evicts an expired entry before the hit
            // check, so the lookup misses and the index shrinks.
            cache.store(make_entry("lazy.ahfl"));
            cache.set_last_accessed_for_test("lazy.ahfl",
                                             now - std::chrono::days(8));
            valid = valid &&
                    (cache.lookup(key_for("lazy.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Miss) &&
                    (cache.entry_count() == 1);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: TTL eviction removes stale entries");
    }

    // Test 18: persistent_cache: LRU eviction removes oldest entries first.
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_lru_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);

            auto make_entry = [](std::string_view path) {
                ahfl::incremental::PersistentCacheEntry entry;
                entry.key.project_root_hash = "lru";
                entry.key.source_path = std::string{path};
                entry.key.content_hash = 7;
                entry.key.toolchain_fingerprint = "tc";
                entry.serialized_typed_hir = R"({"hir":{}})";
                entry.cached_at = std::chrono::system_clock::now();
                return entry;
            };
            auto key_for = [](std::string_view path) {
                ahfl::incremental::CacheKey key;
                key.project_root_hash = "lru";
                key.source_path = std::string{path};
                key.content_hash = 7;
                key.toolchain_fingerprint = "tc";
                return key;
            };

            cache.store(make_entry("a.ahfl"));
            cache.store(make_entry("b.ahfl"));
            cache.store(make_entry("c.ahfl"));

            const auto now = std::chrono::system_clock::now();
            cache.set_last_accessed_for_test("a.ahfl", now - std::chrono::days(3));
            cache.set_last_accessed_for_test("b.ahfl", now - std::chrono::days(2));
            cache.set_last_accessed_for_test("c.ahfl", now - std::chrono::days(1));

            // Cap the cache one byte below its current total size: exactly one
            // entry (the least-recently-accessed, a) must be evicted.
            std::size_t total_size = 0;
            for (const auto &file : fs::directory_iterator(test_dir)) {
                if (file.path().filename() == "index.json") {
                    continue;
                }
                total_size += file.file_size();
            }
            cache.evict_lru(total_size - 1);
            valid = valid && (cache.entry_count() == 2) &&
                    (cache.lookup(key_for("a.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Miss) &&
                    (cache.lookup(key_for("b.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Hit) &&
                    (cache.lookup(key_for("c.ahfl")).kind ==
                     ahfl::incremental::PersistentCacheHitKind::Hit);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: LRU eviction removes oldest entries first");
    }

    // Test 19: persistent_cache: lookup refreshes last_accessed.
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_access_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);

            ahfl::incremental::PersistentCacheEntry entry;
            entry.key.project_root_hash = "access";
            entry.key.source_path = "m.ahfl";
            entry.key.content_hash = 1;
            entry.key.toolchain_fingerprint = "tc";
            entry.serialized_typed_hir = "{}";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);

            const auto old_access =
                std::chrono::system_clock::now() - std::chrono::hours(1);
            cache.set_last_accessed_for_test("m.ahfl", old_access);

            ahfl::incremental::CacheKey key;
            key.project_root_hash = "access";
            key.source_path = "m.ahfl";
            key.content_hash = 1;
            key.toolchain_fingerprint = "tc";
            const auto hit = cache.lookup(key);
            valid = valid &&
                    (hit.kind == ahfl::incremental::PersistentCacheHitKind::Hit);

            const auto refreshed = cache.last_accessed_for_test("m.ahfl");
            valid = valid && refreshed.has_value() && *refreshed > old_access;
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: lookup refreshes last_accessed");
    }

    // Test 20: persistent_cache: clear removes entry files and index from disk.
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_clear_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        bool valid = true;
        {
            ahfl::incremental::PersistentCache cache(test_dir);

            auto store_one = [&cache](std::string_view path) {
                ahfl::incremental::PersistentCacheEntry entry;
                entry.key.project_root_hash = "clear";
                entry.key.source_path = std::string{path};
                entry.key.content_hash = 1;
                entry.key.toolchain_fingerprint = "tc";
                entry.serialized_typed_hir = "{}";
                entry.cached_at = std::chrono::system_clock::now();
                cache.store(entry);
            };
            store_one("a.ahfl");
            store_one("b.ahfl");
            valid = valid && (cache.entry_count() == 2);

            const auto count_json_files = [&test_dir] {
                std::size_t count = 0;
                for (const auto &file : fs::directory_iterator(test_dir)) {
                    if (file.path().extension() == ".json") {
                        ++count;
                    }
                }
                return count;
            };
            // Two entry files plus index.json.
            valid = valid && (count_json_files() == 3);

            cache.clear();
            valid = valid && (cache.entry_count() == 0) &&
                    (count_json_files() == 0);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: clear removes entry files and index from disk");
    }

    // Test 21: persistent_cache: legacy index defaults last_accessed to cached_at.
    // Index files written by older builds lack the last_accessed field; loading
    // must treat cached_at as the last access time so old caches age out under
    // the TTL policy.
    {
        namespace fs = std::filesystem;
        const auto test_dir =
            fs::temp_directory_path() /
            ("ahfl_inc_legacy_index_test_" +
             std::to_string(static_cast<long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        fs::create_directories(test_dir);

        const auto now = std::chrono::system_clock::now();
        const auto epoch_seconds = [](std::chrono::system_clock::time_point t) {
            return std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch())
                .count();
        };
        const auto write_legacy_index = [&](std::string_view path,
                                            std::int64_t cached_at) {
            std::ofstream index(test_dir / "index.json", std::ios::trunc);
            index << "{\n"
                  << "  \"schema_version\": \"AHFL_TYPED_HIR_CACHE_INDEX_V1\",\n"
                  << "  \"entries\": {\n"
                  << "    \"" << path << "\": {\"file\": \"legacy.json\", \"cached_at\": "
                  << cached_at << "}\n"
                  << "  }\n"
                  << "}\n";
        };

        bool valid = true;
        {
            // Stale legacy index: cached_at 8 days ago, no last_accessed. The
            // entry must load and then be evicted by the 7-day TTL.
            write_legacy_index("old.ahfl",
                               epoch_seconds(now - std::chrono::days(8)));
            ahfl::incremental::PersistentCache cache(test_dir);
            valid = valid && (cache.entry_count() == 1);
            cache.evict_expired(std::chrono::days(7));
            valid = valid && (cache.entry_count() == 0);
        }
        {
            // Recent legacy index: cached_at 1 second ago. The entry must
            // survive the 7-day TTL.
            write_legacy_index("new.ahfl",
                               epoch_seconds(now - std::chrono::seconds(1)));
            ahfl::incremental::PersistentCache cache(test_dir);
            valid = valid && (cache.entry_count() == 1);
            cache.evict_expired(std::chrono::days(7));
            valid = valid && (cache.entry_count() == 1);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: legacy index defaults last_accessed to cached_at");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
