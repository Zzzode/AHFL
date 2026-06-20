#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ahfl::llm_provider {

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

    /// Clear all entries.
    void clear();

  private:
    struct CacheEntry {
        std::string response;
        std::chrono::steady_clock::time_point inserted_at;
    };

    [[nodiscard]] std::string
    make_key(std::string_view model, std::string_view system, std::string_view user) const;

    std::size_t max_entries_;
    std::chrono::seconds ttl_;
    std::unordered_map<std::string, CacheEntry> entries_;
    std::vector<std::string> insertion_order_;
    mutable std::mutex mutex_;
};

} // namespace ahfl::llm_provider
