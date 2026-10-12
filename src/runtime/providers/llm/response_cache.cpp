#include "runtime/providers/llm/response_cache.hpp"

#include "base/support/sha256.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>

namespace ahfl::llm_provider {
namespace {

[[nodiscard]] std::int64_t now_unix_seconds() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

} // namespace

std::string
response_cache_key_digest(std::string_view model, std::string_view system, std::string_view user) {
    std::string encoded;
    for (const auto field : {model, system, user}) {
        const auto length = static_cast<std::uint64_t>(field.size());
        for (int shift = 0; shift < 64; shift += 8) {
            encoded.push_back(static_cast<char>((length >> shift) & 0xffU));
        }
        encoded.append(field);
    }
    return support::sha256_hex(encoded);
}

bool is_response_cache_key_digest(std::string_view value) noexcept {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

ResponseCache::ResponseCache(std::size_t max_entries, std::chrono::seconds ttl)
    : max_entries_(max_entries), ttl_(ttl) {}

std::string ResponseCache::make_key(std::string_view model,
                                    std::string_view system,
                                    std::string_view user) const {
    return response_cache_key_digest(model, system, user);
}

void ResponseCache::evict_to_capacity_locked() {
    while (entries_.size() > max_entries_ && !insertion_order_.empty()) {
        auto oldest_key = insertion_order_.front();
        entries_.erase(oldest_key);
        insertion_order_.erase(insertion_order_.begin());
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
            .key_digest = key,
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
    std::unordered_set<std::string_view> keys;
    for (const auto &entry : entries) {
        if (!is_response_cache_key_digest(entry.key_digest) || entry.response.empty() ||
            entry.inserted_unix_seconds <= 0 || entry.inserted_unix_seconds > epoch_now ||
            !keys.insert(entry.key_digest).second) {
            return 0;
        }
    }
    std::size_t loaded = 0;
    for (const auto &snapshot_entry : entries) {
        const auto age_seconds = epoch_now - snapshot_entry.inserted_unix_seconds;
        if (age_seconds > ttl_.count()) {
            continue;
        }
        CacheEntry entry{
            .response = snapshot_entry.response,
            .inserted_at = steady_now - std::chrono::seconds{age_seconds},
            .inserted_unix_seconds = snapshot_entry.inserted_unix_seconds,
        };
        auto it = entries_.find(snapshot_entry.key_digest);
        if (it != entries_.end()) {
            it->second = std::move(entry);
        } else {
            entries_.emplace(snapshot_entry.key_digest, std::move(entry));
            insertion_order_.push_back(snapshot_entry.key_digest);
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
