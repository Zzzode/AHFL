#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ahfl::incremental {

// Schema version for the on-disk Typed HIR cache envelope (RFC 0016). Any
// mismatch is a cache miss, so bumping this string invalidates every cache
// produced by an older build.
inline constexpr std::string_view kCacheSchemaVersion = "AHFL_TYPED_HIR_CACHE_V1";

// Schema version for the per-project index.json that maps source paths to
// cache entry files.
inline constexpr std::string_view kCacheIndexSchemaVersion =
    "AHFL_TYPED_HIR_CACHE_INDEX_V1";

// FNV-1a 64-bit content hash. This matches the hashing used by IrCache and
// the LSP DocumentStore so cache identity is consistent across tooling.
[[nodiscard]] std::uint64_t fnv1a64(std::string_view bytes) noexcept;

// SHA-256 hex digest of the canonical (weakly-canonical, lexically-normal)
// project root path. Used to isolate cache directories per project.
[[nodiscard]] std::string project_root_hash(const std::filesystem::path &project_root);

// Default toolchain fingerprint: cache schema version + C++ standard identity.
// Compiler upgrades or stdlib changes shift this string and invalidate caches.
[[nodiscard]] std::string default_toolchain_fingerprint();

// Unified cache identity (RFC 0016). Two cache entries describe the same
// artifact iff all four fields compare equal. This is the identity shared by
// the standalone incremental compiler and the LSP AnalysisService, allowing
// both to reuse the same cache directory.
struct CacheKey {
    std::string project_root_hash;     // SHA-256 of canonical project root path
    std::string source_path;           // project-relative path (e.g. "src/main.ahfl")
    std::uint64_t content_hash{0};     // FNV-1a of source content
    std::string toolchain_fingerprint; // compiler version + flags + stdlib identity

    [[nodiscard]] friend bool operator==(const CacheKey &lhs,
                                         const CacheKey &rhs) = default;
};

// On-disk cache envelope (RFC 0016 "Typed HIR cache envelope"). Distinct from
// the in-memory ir_cache.hpp CacheEntry: this is the persistent, schema-
// versioned record. The source_graph_revision and resolver_snapshot_version
// fields are stored for downstream validation; PersistentCache::lookup only
// validates the schema version and the CacheKey.
struct PersistentCacheEntry {
    CacheKey key;
    std::string source_graph_revision;     // hash of import graph structure
    std::string resolver_snapshot_version; // hash of resolver state
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
class PersistentCache {
  public:
    explicit PersistentCache(std::filesystem::path cache_dir);

    // Loads the entry for `key`. Returns Hit only when the file exists,
    // parses, matches the schema version, and matches every CacheKey field.
    // Any corruption or mismatch is a Miss (graceful fallback).
    [[nodiscard]] PersistentCacheLookupResult lookup(const CacheKey &key) const;

    // Atomically writes the entry and updates the index.
    void store(const PersistentCacheEntry &entry);

    // Removes the entry for `source_path` (project-relative) and updates the
    // index. No-op when the path is not cached.
    void invalidate(const std::string &source_path);

    // Removes every entry file and the index.
    void clear();

    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] const std::filesystem::path &cache_dir() const noexcept;

  private:
    struct IndexEntry {
        std::string file_name;
        std::chrono::system_clock::time_point cached_at;
    };

    [[nodiscard]] std::string entry_file_name(const std::string &source_path) const;
    [[nodiscard]] std::filesystem::path entry_file_path(const std::string &source_path) const;
    void load_index();
    void save_index() const;

    std::filesystem::path cache_dir_;
    std::unordered_map<std::string, IndexEntry> index_;
};

} // namespace ahfl::incremental

namespace std {

template <> struct hash<ahfl::incremental::CacheKey> {
    [[nodiscard]] std::size_t
    operator()(const ahfl::incremental::CacheKey &key) const noexcept {
        // FNV-1a over the four fields with length framing so distinct fields
        // cannot collide by concatenation.
        constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kPrime = 1099511628211ULL;
        std::uint64_t hash = kOffsetBasis;
        const auto mix_bytes = [&hash](std::string_view bytes) noexcept {
            for (const char byte : bytes) {
                hash ^= static_cast<unsigned char>(byte);
                hash *= kPrime;
            }
        };
        const auto mix_number = [&hash](std::uint64_t value) noexcept {
            for (int shift = 0; shift < 64; shift += 8) {
                hash ^= static_cast<unsigned char>((value >> shift) & 0xFFU);
                hash *= kPrime;
            }
        };
        mix_bytes(key.project_root_hash);
        mix_bytes(key.source_path);
        mix_number(key.content_hash);
        mix_bytes(key.toolchain_fingerprint);
        return static_cast<std::size_t>(hash);
    }
};

} // namespace std
