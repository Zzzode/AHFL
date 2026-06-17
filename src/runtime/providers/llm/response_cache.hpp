#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::llm_provider {

inline constexpr std::string_view kResponseCacheKeyVersion{"ahfl.llm.response-cache-key.v1"};
inline constexpr std::string_view kResponseCacheSnapshotSchema{"ahfl.llm_response_cache.v0"};

[[nodiscard]] std::string response_cache_key_fingerprint(std::string_view model,
                                                         std::string_view system,
                                                         std::string_view user);

struct ResponseCacheSnapshotEntry {
    std::string key_fingerprint;
    std::string response;
    std::int64_t inserted_unix_seconds{0};
};

/// LRU response cache with TTL-based expiration.
class ResponseCache {
  public:
    explicit ResponseCache(std::size_t max_entries = 128,
                           std::chrono::seconds ttl = std::chrono::seconds{300});

    /// Get a cached response. Returns nullopt on miss or expiry.
    [[nodiscard]] std::optional<std::string>
    get(std::string_view model, std::string_view system, std::string_view user);

    /// Store a response in cache.
    void put(std::string_view model,
             std::string_view system,
             std::string_view user,
             std::string response);

    /// Current number of entries.
    [[nodiscard]] std::size_t size() const;

    /// Export non-expired entries for optional persistent cache storage.
    [[nodiscard]] std::vector<ResponseCacheSnapshotEntry> snapshot() const;

    /// Load entries from optional persistent cache storage. Expired entries are ignored.
    [[nodiscard]] std::size_t load_snapshot(const std::vector<ResponseCacheSnapshotEntry> &entries);

    /// Clear all entries.
    void clear();

  private:
    struct CacheEntry {
        std::string response;
        std::chrono::steady_clock::time_point inserted_at;
        std::int64_t inserted_unix_seconds{0};
    };

    [[nodiscard]] std::string
    make_key(std::string_view model, std::string_view system, std::string_view user) const;
    void evict_to_capacity_locked();
    void prune_expired_locked(std::chrono::steady_clock::time_point now);

    std::size_t max_entries_;
    std::chrono::seconds ttl_;
    std::unordered_map<std::string, CacheEntry> entries_;
    std::vector<std::string> insertion_order_;
    mutable std::mutex mutex_;
};

} // namespace ahfl::llm_provider
