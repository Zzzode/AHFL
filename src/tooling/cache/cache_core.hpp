#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::cache {

// Current artifact categories; cache validity comes from content identity.
inline constexpr std::string_view kCacheFormat = "ahfl.typed-hir-cache";
inline constexpr std::string_view kCacheIndexFormat = "ahfl.typed-hir-cache-index";

// Default time-to-live for cache entries (RFC 0016 "Cache lifecycle"): an
// entry whose last_accessed is older than this is evicted lazily on the next
// lookup or store.
inline constexpr std::chrono::days kDefaultTtl{7};

// Default cap on the total size of entry files in a project cache directory
// (RFC 0016 "Cache lifecycle"). When a store pushes the cache past this, the
// least-recently-accessed entries are evicted eagerly until the cache fits.
inline constexpr std::size_t kDefaultMaxCacheBytes = 1024ULL * 1024 * 1024;

// SHA-256 hex digest of the canonical (weakly-canonical, lexically-normal)
// project root path. Used to isolate cache directories per project.
[[nodiscard]] std::string project_root_hash(const std::filesystem::path &project_root);

// SHA-256 of the actual producer executable bytes. Empty means unavailable.
[[nodiscard]] std::string producer_digest(const std::filesystem::path &executable);
[[nodiscard]] std::string default_producer_digest();

// The complete current build, configuration and dependency identity.
struct CacheKey {
    std::string project_root_hash; // SHA-256 of canonical project root path
    std::string source_path;       // project-relative path (e.g. "src/main.ahfl")
    std::string content_digest;    // SHA-256 of source content
    std::string producer_digest;   // actual executable SHA-256
    std::string configuration_digest;
    std::string dependency_digest;

    [[nodiscard]] friend bool operator==(const CacheKey &lhs, const CacheKey &rhs) = default;
};

// Persistent Typed HIR artifact and its analysis metadata.
struct PersistentCacheEntry {
    CacheKey key;
    std::string source_graph_digest;      // hash of import graph structure
    std::string resolver_snapshot_digest; // hash of resolver state
    std::uint64_t signature_fingerprint{0};
    std::string serialized_typed_hir; // JSON
    // Wall-clock time so LRU/TTL policies (later slices) can reason about
    // entries across process restarts. steady_clock epochs are per-boot and
    // cannot be persisted.
    std::chrono::system_clock::time_point cached_at;
};

enum class PersistentCacheHitKind {
    Hit,
    Miss,
};

struct PersistentCacheLookupResult {
    PersistentCacheHitKind kind{PersistentCacheHitKind::Miss};
    std::optional<PersistentCacheEntry> entry;
};

// Persistent cache with atomic writes. The cache directory is project-
// specific (typically <cache-root>/<project-root-hash>/). Entries are stored
// as one JSON file per source unit, with an index.json mapping source paths
// to entry files. Single-writer model (RFC 0016 Non-Goals): no locking.
//
// Lifecycle (RFC 0016 "Cache lifecycle"): every hit refreshes the entry's
// last_accessed timestamp in the index. lookup() and store() lazily evict
// entries older than kDefaultTtl; store() additionally evicts the least-
// recently-accessed entries once the directory exceeds kDefaultMaxCacheBytes.
class PersistentCache {
  public:
    explicit PersistentCache(std::filesystem::path cache_dir);

    // Loads the entry for `key`. Returns Hit only when the file exists,
    // parses, matches the current format, and matches every CacheKey field.
    // Any corruption or mismatch is a Miss (graceful fallback). On Hit the
    // entry's last_accessed timestamp is refreshed and the index is persisted.
    // Stale entries (older than kDefaultTtl) are evicted first.
    [[nodiscard]] PersistentCacheLookupResult lookup(const CacheKey &key);

    // Atomically writes the entry and updates the index. Evicts expired
    // entries first, then enforces the size budget via LRU eviction.
    void store(const PersistentCacheEntry &entry);

    // Removes the entry for `source_path` (project-relative) and updates the
    // index. No-op when the path is not cached.
    void invalidate(const std::string &source_path);

    // Removes every entry file and the index.
    void clear();

    // Evicts entries whose last_accessed is older than `ttl`. Invoked
    // lazily by lookup() and store() with kDefaultTtl; exposed so cache
    // policies can be driven explicitly (and tested).
    void evict_expired(std::chrono::seconds ttl);

    // Evicts least-recently-accessed entries until the total size of the
    // entry files is at most `max_bytes`. Invoked eagerly by store() with
    // kDefaultMaxCacheBytes; exposed for explicit policy control and tests.
    void evict_lru(std::size_t max_bytes);

    // Overrides the last_accessed timestamp of a cached entry. Exposed for
    // cache lifecycle tests; production code never rewinds access time.
    void set_last_accessed_for_test(const std::string &source_path,
                                    std::chrono::system_clock::time_point when);

    // Returns the last_accessed timestamp of a cached entry, or nullopt when
    // the path is not cached. Exposed for cache lifecycle tests.
    [[nodiscard]] std::optional<std::chrono::system_clock::time_point>
    last_accessed_for_test(const std::string &source_path) const;

    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] const std::filesystem::path &cache_dir() const noexcept;

  private:
    struct IndexEntry {
        std::string file_name;
        std::chrono::system_clock::time_point cached_at;
        std::chrono::system_clock::time_point last_accessed;
    };

    [[nodiscard]] std::string entry_file_name(const std::string &source_path) const;
    [[nodiscard]] std::filesystem::path entry_file_path(const std::string &source_path) const;
    // Deletes the entry file and erases the index record. Returns true when
    // an entry was removed.
    [[nodiscard]] bool remove_entry(const std::string &source_path);
    void load_index();
    void save_index() const;

    std::filesystem::path cache_dir_;
    std::unordered_map<std::string, IndexEntry> index_;
};

} // namespace ahfl::cache

namespace std {

template <> struct hash<ahfl::cache::CacheKey> {
    [[nodiscard]] std::size_t operator()(const ahfl::cache::CacheKey &key) const noexcept {
        // FNV-1a over all fields with length framing so distinct fields
        // cannot collide by concatenation.
        constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kPrime = 1099511628211ULL;
        std::uint64_t hash = kOffsetBasis;
        const auto mix_bytes = [&hash](std::string_view bytes) noexcept {
            const auto length = static_cast<std::uint64_t>(bytes.size());
            for (int shift = 0; shift < 64; shift += 8) {
                hash ^= static_cast<unsigned char>((length >> shift) & 0xFFU);
                hash *= kPrime;
            }
            for (const char byte : bytes) {
                hash ^= static_cast<unsigned char>(byte);
                hash *= kPrime;
            }
        };
        mix_bytes(key.project_root_hash);
        mix_bytes(key.source_path);
        mix_bytes(key.content_digest);
        mix_bytes(key.producer_digest);
        mix_bytes(key.configuration_digest);
        mix_bytes(key.dependency_digest);
        return static_cast<std::size_t>(hash);
    }
};

} // namespace std
