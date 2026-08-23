#pragma once

#include <cstddef>
#include <string>
#include <tooling/incremental/cache_core.hpp>
#include <unordered_map>

namespace ahfl::incremental {

// In-memory cache tier for the incremental compiler (RFC 0016 "Unified cache
// core"). This is a thin layer over the CacheCore identity model: it stores
// the same PersistentCacheEntry envelopes keyed by the same CacheKey identity
// as the disk-backed PersistentCache, so the two caches never diverge on what
// "the same artifact" means. The only difference is durability — IrCache lives
// for one compiler process and performs no I/O, while PersistentCache persists
// across restarts.
//
// The map is keyed by the project-relative source path (CacheKey::source_path),
// mirroring PersistentCache's on-disk index which stores one entry file per
// source unit. Lookups validate the full CacheKey (content hash included), so
// a content change is a miss even when a stale entry is still resident.
class IrCache {
  public:
    // Inserts or replaces the entry identified by `entry.key.source_path`.
    void store(PersistentCacheEntry entry);

    // Hit only when a resident entry's key compares equal to `key` (all four
    // CacheKey fields, including content_hash). A resident entry with a
    // different content hash is a miss (the content changed); read the stale
    // entry via find_by_source_path when the previous envelope is needed.
    [[nodiscard]] PersistentCacheLookupResult lookup(const CacheKey &key) const;

    // Returns the resident entry for `source_path` regardless of content hash,
    // or nullptr when absent. Used to read the previous signature fingerprint
    // before a recompile overwrites the entry (RFC 0016 downstream skip).
    [[nodiscard]] const PersistentCacheEntry *
    find_by_source_path(const std::string &source_path) const;

    // Removes the entry for `source_path`. No-op when absent.
    void invalidate(const std::string &source_path);

    void clear();
    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] std::size_t total_size_bytes() const;

  private:
    std::unordered_map<std::string, PersistentCacheEntry> cache_;
};

} // namespace ahfl::incremental
