#include <tooling/incremental/ir_cache.hpp>

namespace ahfl::incremental {

void IrCache::store(CacheEntry entry) {
    cache_[entry.module_path] = std::move(entry);
}

[[nodiscard]] CacheLookupResult IrCache::lookup(const std::string &module_path,
                                                std::uint64_t current_hash) const {
    auto it = cache_.find(module_path);
    if (it == cache_.end()) {
        return CacheLookupResult{CacheHitKind::Miss, std::nullopt};
    }
    if (it->second.content_hash != current_hash) {
        return CacheLookupResult{CacheHitKind::Stale, it->second};
    }
    return CacheLookupResult{CacheHitKind::Hit, it->second};
}

void IrCache::invalidate(const std::string &module_path) {
    cache_.erase(module_path);
}

void IrCache::clear() {
    cache_.clear();
}

[[nodiscard]] std::size_t IrCache::entry_count() const {
    return cache_.size();
}

[[nodiscard]] std::size_t IrCache::total_size_bytes() const {
    std::size_t total = 0;
    for (const auto &[path, entry] : cache_) {
        (void)path;
        total += entry.serialized_ir.size();
    }
    return total;
}

} // namespace ahfl::incremental
