// RFC 0016 cache-contract tests for the QueryEngine persistence layer
// (RFC 0027 P5 / KR6.12). These are the cache_core (CacheKey, FNV-1a) and
// on-disk PersistentCache envelope tests that survived the retirement of the
// hand-rolled incremental subsystem: the deterministic, secret-free,
// index-keyed cache contract is re-anchored on the QueryEngine, and these
// properties are its guard.
#include <tooling/cache/cache_core.hpp>

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("QueryEngine Persistent Cache Tests\n");
    std::printf("==================================\n\n");

    // Test 5: cache_core: CacheKey equality and hashing
    {
        using ahfl::cache::CacheKey;

        CacheKey a;
        a.project_root_hash = "abc123";
        a.source_path = "src/main.ahfl";
        a.content_digest = "source-42";
        a.producer_digest = "producer-a";

        CacheKey b = a;
        CacheKey c = a;
        c.content_digest = "source-99";
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
            ahfl::cache::PersistentCache cache(test_dir);

            ahfl::cache::CacheKey key;
            key.project_root_hash = "deadbeef";
            key.source_path = "src/main.ahfl";
            key.content_digest = "source-12345";
            key.producer_digest = "producer-a";

            // Miss on empty cache.
            auto miss = cache.lookup(key);
            valid = valid && (miss.kind == ahfl::cache::PersistentCacheHitKind::Miss);

            // Store and hit.
            ahfl::cache::PersistentCacheEntry entry;
            entry.key = key;
            entry.source_graph_digest = "graph-a";
            entry.resolver_snapshot_digest = "resolver-a";
            entry.signature_fingerprint = 777;
            entry.serialized_typed_hir = R"({"typed_hir":{}})";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);

            auto hit = cache.lookup(key);
            valid = valid && (hit.kind == ahfl::cache::PersistentCacheHitKind::Hit) &&
                    hit.entry.has_value();
            if (hit.entry.has_value()) {
                valid = valid && (hit.entry->key == key) &&
                        (hit.entry->source_graph_digest == "graph-a") &&
                        (hit.entry->signature_fingerprint == 777) &&
                        (hit.entry->serialized_typed_hir == R"({"typed_hir":{}})");
            }
            valid = valid && (cache.entry_count() == 1);

            // Miss with different content digest.
            auto stale_key = key;
            stale_key.content_digest = "source-99999";
            auto stale = cache.lookup(stale_key);
            valid = valid && (stale.kind == ahfl::cache::PersistentCacheHitKind::Miss);

            // Invalidate and miss.
            cache.invalidate(key.source_path);
            auto after_invalidate = cache.lookup(key);
            valid = valid &&
                    (after_invalidate.kind ==
                     ahfl::cache::PersistentCacheHitKind::Miss) &&
                    (cache.entry_count() == 0);

            // Store again, then clear.
            cache.store(entry);
            valid = valid && (cache.entry_count() == 1);
            cache.clear();
            auto after_clear = cache.lookup(key);
            valid = valid &&
                    (after_clear.kind == ahfl::cache::PersistentCacheHitKind::Miss) &&
                    (cache.entry_count() == 0);
        }

        // Reopen: a fresh PersistentCache on the same directory should see an
        // empty index (clear persisted).
        {
            ahfl::cache::PersistentCache reopened(test_dir);
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
            ahfl::cache::PersistentCache cache(test_dir);
            ahfl::cache::PersistentCacheEntry entry;
            entry.key.project_root_hash = "persist";
            entry.key.source_path = "mod.ahfl";
            entry.key.content_digest = "source-555";
            entry.key.producer_digest = "tc";
            entry.signature_fingerprint = 111;
            entry.serialized_typed_hir = R"({"ir":1})";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);
        }
        {
            ahfl::cache::PersistentCache reopened(test_dir);
            ahfl::cache::CacheKey key;
            key.project_root_hash = "persist";
            key.source_path = "mod.ahfl";
            key.content_digest = "source-555";
            key.producer_digest = "tc";
            auto hit = reopened.lookup(key);
            valid = (hit.kind == ahfl::cache::PersistentCacheHitKind::Hit) &&
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
            ahfl::cache::PersistentCache cache(test_dir);
            ahfl::cache::PersistentCacheEntry entry;
            entry.key.project_root_hash = "corrupt";
            entry.key.source_path = "x.ahfl";
            entry.key.content_digest = "source-1";
            entry.key.producer_digest = "tc";
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

            ahfl::cache::CacheKey key;
            key.project_root_hash = "corrupt";
            key.source_path = "x.ahfl";
            key.content_digest = "source-1";
            key.producer_digest = "tc";
            auto result = cache.lookup(key);
            valid = (result.kind == ahfl::cache::PersistentCacheHitKind::Miss);
        }
        fs::remove_all(test_dir);
        check(valid, "persistent_cache: corrupted entry file falls back to miss");
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
            ahfl::cache::PersistentCache cache(test_dir);

            auto make_entry = [](std::string_view path) {
                ahfl::cache::PersistentCacheEntry entry;
                entry.key.project_root_hash = "ttl";
                entry.key.source_path = std::string{path};
                entry.key.content_digest = "source-1";
                entry.key.producer_digest = "tc";
                entry.serialized_typed_hir = "{}";
                entry.cached_at = std::chrono::system_clock::now();
                return entry;
            };
            auto key_for = [](std::string_view path) {
                ahfl::cache::CacheKey key;
                key.project_root_hash = "ttl";
                key.source_path = std::string{path};
                key.content_digest = "source-1";
                key.producer_digest = "tc";
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
                     ahfl::cache::PersistentCacheHitKind::Miss) &&
                    (cache.lookup(key_for("fresh.ahfl")).kind ==
                     ahfl::cache::PersistentCacheHitKind::Hit);

            // Lazy eviction: lookup() evicts an expired entry before the hit
            // check, so the lookup misses and the index shrinks.
            cache.store(make_entry("lazy.ahfl"));
            cache.set_last_accessed_for_test("lazy.ahfl",
                                             now - std::chrono::days(8));
            valid = valid &&
                    (cache.lookup(key_for("lazy.ahfl")).kind ==
                     ahfl::cache::PersistentCacheHitKind::Miss) &&
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
            ahfl::cache::PersistentCache cache(test_dir);

            auto make_entry = [](std::string_view path) {
                ahfl::cache::PersistentCacheEntry entry;
                entry.key.project_root_hash = "lru";
                entry.key.source_path = std::string{path};
                entry.key.content_digest = "source-7";
                entry.key.producer_digest = "tc";
                entry.serialized_typed_hir = R"({"hir":{}})";
                entry.cached_at = std::chrono::system_clock::now();
                return entry;
            };
            auto key_for = [](std::string_view path) {
                ahfl::cache::CacheKey key;
                key.project_root_hash = "lru";
                key.source_path = std::string{path};
                key.content_digest = "source-7";
                key.producer_digest = "tc";
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
                     ahfl::cache::PersistentCacheHitKind::Miss) &&
                    (cache.lookup(key_for("b.ahfl")).kind ==
                     ahfl::cache::PersistentCacheHitKind::Hit) &&
                    (cache.lookup(key_for("c.ahfl")).kind ==
                     ahfl::cache::PersistentCacheHitKind::Hit);
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
            ahfl::cache::PersistentCache cache(test_dir);

            ahfl::cache::PersistentCacheEntry entry;
            entry.key.project_root_hash = "access";
            entry.key.source_path = "m.ahfl";
            entry.key.content_digest = "source-1";
            entry.key.producer_digest = "tc";
            entry.serialized_typed_hir = "{}";
            entry.cached_at = std::chrono::system_clock::now();
            cache.store(entry);

            const auto old_access =
                std::chrono::system_clock::now() - std::chrono::hours(1);
            cache.set_last_accessed_for_test("m.ahfl", old_access);

            ahfl::cache::CacheKey key;
            key.project_root_hash = "access";
            key.source_path = "m.ahfl";
            key.content_digest = "source-1";
            key.producer_digest = "tc";
            const auto hit = cache.lookup(key);
            valid = valid &&
                    (hit.kind == ahfl::cache::PersistentCacheHitKind::Hit);

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
            ahfl::cache::PersistentCache cache(test_dir);

            auto store_one = [&cache](std::string_view path) {
                ahfl::cache::PersistentCacheEntry entry;
                entry.key.project_root_hash = "clear";
                entry.key.source_path = std::string{path};
                entry.key.content_digest = "source-1";
                entry.key.producer_digest = "tc";
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

    // Current indexes never infer an absent last_accessed timestamp.
    {
        namespace fs = std::filesystem;
        const auto dir = fs::temp_directory_path() /
            ("ahfl_cache_current_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(dir);
        std::ofstream(dir / "index.json") << R"({"format":"ahfl.typed-hir-cache-index","entries":{"x.ahfl":{"file":"record.json","cached_at":0}}})";
        ahfl::cache::PersistentCache malformed(dir);
        check(malformed.entry_count() == 0, "persistent cache rejects incomplete index records");

        const auto producer = dir / "producer";
        std::ofstream(producer) << "build-a";
        const auto first = ahfl::cache::producer_digest(producer);
        std::ofstream(producer) << "build-b";
        const auto second = ahfl::cache::producer_digest(producer);
        check(first.size() == 64 && second.size() == 64 && first != second &&
              ahfl::cache::default_producer_digest().size() == 64,
              "producer identity hashes actual executable content");

        ahfl::cache::PersistentCacheEntry entry;
        entry.key = {.project_root_hash = "project", .source_path = "x.ahfl", .content_digest = "source-7",
                     .producer_digest = first, .configuration_digest = "config", .dependency_digest = "dependency"};
        entry.serialized_typed_hir = "{}";
        entry.cached_at = std::chrono::system_clock::now();
        malformed.store(entry);
        auto key = entry.key;
        key.producer_digest = second;
        check(malformed.lookup(key).kind == ahfl::cache::PersistentCacheHitKind::Miss,
              "changed producer misses current cache");
        key = entry.key; key.configuration_digest = "other";
        check(malformed.lookup(key).kind == ahfl::cache::PersistentCacheHitKind::Miss,
              "changed configuration misses current cache");
        key = entry.key; key.dependency_digest = "other";
        check(malformed.lookup(key).kind == ahfl::cache::PersistentCacheHitKind::Miss,
              "changed dependency misses current cache");
        for (const auto &file : fs::directory_iterator(dir)) {
            if (file.path().extension() != ".json" || file.path().filename() == "index.json") continue;
            std::ifstream stream(file.path());
            std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            const auto start = text.find("\"signature_fingerprint\":");
            const auto end = text.find(',', start);
            text.erase(start, end - start + 1);
            std::ofstream(file.path()) << text;
        }
        check(malformed.lookup(entry.key).kind == ahfl::cache::PersistentCacheHitKind::Miss,
              "missing current entry metadata is rejected");
        fs::remove_all(dir);
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
