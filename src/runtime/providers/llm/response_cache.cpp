#include "runtime/providers/llm/response_cache.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

namespace ahfl::llm_provider {
namespace {

[[nodiscard]] std::uint64_t update_fnv1a(std::uint64_t hash, std::string_view bytes) {
    constexpr std::uint64_t prime = 1099511628211ULL;
    for (const unsigned char ch : bytes) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= prime;
    }
    return hash;
}

[[nodiscard]] std::string hex64(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string output(16, '0');
    for (std::size_t index = output.size(); index > 0; --index) {
        output[index - 1] = digits[value & 0x0F];
        value >>= 4;
    }
    return output;
}

[[nodiscard]] std::int64_t now_unix_seconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

} // namespace

std::string response_cache_key_fingerprint(std::string_view model,
                                           std::string_view system,
                                           std::string_view user) {
    constexpr std::string_view separator{"\0", 1};
    std::uint64_t hash = 14695981039346656037ULL;
    hash = update_fnv1a(hash, kResponseCacheKeyVersion);
    hash = update_fnv1a(hash, separator);
    hash = update_fnv1a(hash, model);
    hash = update_fnv1a(hash, separator);
    hash = update_fnv1a(hash, system);
    hash = update_fnv1a(hash, separator);
    hash = update_fnv1a(hash, user);
    return hex64(hash);
}

ResponseCache::ResponseCache(std::size_t max_entries, std::chrono::seconds ttl)
    : max_entries_(max_entries), ttl_(ttl) {}

std::string ResponseCache::make_key(std::string_view model,
                                    std::string_view system,
                                    std::string_view user) const {
    return response_cache_key_fingerprint(model, system, user);
}

void ResponseCache::evict_to_capacity_locked() {
    while (entries_.size() > max_entries_ && !insertion_order_.empty()) {
        auto oldest_key = insertion_order_.front();
        entries_.erase(oldest_key);
        insertion_order_.erase(insertion_order_.begin());
    }
}

void ResponseCache::prune_expired_locked(std::chrono::steady_clock::time_point now) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (now - it->second.inserted_at > ttl_) {
            const auto key = it->first;
            it = entries_.erase(it);
            insertion_order_.erase(
                std::remove(insertion_order_.begin(), insertion_order_.end(), key),
                insertion_order_.end());
        } else {
            ++it;
        }
    }
}

std::optional<std::string>
ResponseCache::get(std::string_view model, std::string_view system, std::string_view user) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = make_key(model, system, user);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }

    auto now = std::chrono::steady_clock::now();
    if (now - it->second.inserted_at > ttl_) {
        entries_.erase(it);
        insertion_order_.erase(std::remove(insertion_order_.begin(), insertion_order_.end(), key),
                               insertion_order_.end());
        return std::nullopt;
    }

    return it->second.response;
}

void ResponseCache::put(std::string_view model,
                        std::string_view system,
                        std::string_view user,
                        std::string response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = make_key(model, system, user);

    CacheEntry entry;
    entry.response = std::move(response);
    entry.inserted_at = std::chrono::steady_clock::now();
    entry.inserted_unix_seconds = now_unix_seconds();

    auto it = entries_.find(key);
    if (it != entries_.end()) {
        it->second = std::move(entry);
    } else {
        entries_.emplace(key, std::move(entry));
        insertion_order_.push_back(key);
    }
    evict_to_capacity_locked();
}

std::size_t ResponseCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

std::vector<ResponseCacheSnapshotEntry> ResponseCache::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<ResponseCacheSnapshotEntry> entries;
    entries.reserve(entries_.size());
    for (const auto &key : insertion_order_) {
        auto it = entries_.find(key);
        if (it == entries_.end()) {
            continue;
        }
        if (now - it->second.inserted_at > ttl_) {
            continue;
        }
        entries.push_back(ResponseCacheSnapshotEntry{
            .key_fingerprint = key,
            .response = it->second.response,
            .inserted_unix_seconds = it->second.inserted_unix_seconds,
        });
    }
    return entries;
}

std::size_t ResponseCache::load_snapshot(const std::vector<ResponseCacheSnapshotEntry> &entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto steady_now = std::chrono::steady_clock::now();
    const auto epoch_now = now_unix_seconds();
    std::size_t loaded = 0;
    for (const auto &snapshot_entry : entries) {
        if (snapshot_entry.key_fingerprint.empty() || snapshot_entry.response.empty()) {
            continue;
        }
        const auto age_seconds = epoch_now - snapshot_entry.inserted_unix_seconds;
        if (age_seconds < 0 || age_seconds > ttl_.count()) {
            continue;
        }
        CacheEntry entry{
            .response = snapshot_entry.response,
            .inserted_at = steady_now - std::chrono::seconds{age_seconds},
            .inserted_unix_seconds = snapshot_entry.inserted_unix_seconds,
        };
        auto it = entries_.find(snapshot_entry.key_fingerprint);
        if (it != entries_.end()) {
            it->second = std::move(entry);
        } else {
            entries_.emplace(snapshot_entry.key_fingerprint, std::move(entry));
            insertion_order_.push_back(snapshot_entry.key_fingerprint);
        }
        ++loaded;
    }
    evict_to_capacity_locked();
    return loaded;
}

void ResponseCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    insertion_order_.clear();
}

} // namespace ahfl::llm_provider
