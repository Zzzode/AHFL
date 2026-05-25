#include "llm_provider/response_cache.hpp"

#include <algorithm>
#include <functional>

namespace ahfl::llm_provider {

ResponseCache::ResponseCache(std::size_t max_entries, std::chrono::seconds ttl)
    : max_entries_(max_entries), ttl_(ttl) {}

std::string ResponseCache::make_key(std::string_view model, std::string_view system,
                                    std::string_view user) const {
    // Simple hash key: model + "|" + system + "|" + user
    std::string key;
    key.reserve(model.size() + system.size() + user.size() + 2);
    key += model;
    key += '|';
    key += system;
    key += '|';
    key += user;
    return key;
}

std::optional<std::string> ResponseCache::get(std::string_view model, std::string_view system,
                                              std::string_view user) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = make_key(model, system, user);
    auto it = entries_.find(key);
    if (it == entries_.end()) {
        return std::nullopt;
    }

    // Check TTL
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.inserted_at > ttl_) {
        // Expired — remove
        entries_.erase(it);
        insertion_order_.erase(
            std::remove(insertion_order_.begin(), insertion_order_.end(), key),
            insertion_order_.end());
        return std::nullopt;
    }

    return it->second.response;
}

void ResponseCache::put(std::string_view model, std::string_view system, std::string_view user,
                        std::string response) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = make_key(model, system, user);

    // Evict oldest if at capacity
    while (entries_.size() >= max_entries_ && !insertion_order_.empty()) {
        auto &oldest_key = insertion_order_.front();
        entries_.erase(oldest_key);
        insertion_order_.erase(insertion_order_.begin());
    }

    CacheEntry entry;
    entry.response = std::move(response);
    entry.inserted_at = std::chrono::steady_clock::now();

    auto [it, inserted] = entries_.emplace(key, std::move(entry));
    if (!inserted) {
        it->second = std::move(entry);
    } else {
        insertion_order_.push_back(key);
    }
}

std::size_t ResponseCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.size();
}

void ResponseCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    insertion_order_.clear();
}

} // namespace ahfl::llm_provider
