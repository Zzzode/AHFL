#include <tooling/incremental/ir_cache.hpp>

#include <utility>

namespace ahfl::incremental {

void IrCache::store(PersistentCacheEntry entry) {
    const auto source_path = entry.key.source_path;
    cache_[source_path] = std::move(entry);
}

PersistentCacheLookupResult IrCache::lookup(const CacheKey &key) const {
    const auto it = cache_.find(key.source_path);
    if (it == cache_.end() || it->second.key != key) {
        return PersistentCacheLookupResult{PersistentCacheHitKind::Miss, std::nullopt};
    }
    return PersistentCacheLookupResult{PersistentCacheHitKind::Hit, it->second};
}

const PersistentCacheEntry *
IrCache::find_by_source_path(const std::string &source_path) const {
    const auto it = cache_.find(source_path);
    return it == cache_.end() ? nullptr : &it->second;
}

void IrCache::invalidate(const std::string &source_path) {
    cache_.erase(source_path);
}

void IrCache::clear() {
    cache_.clear();
}

std::size_t IrCache::entry_count() const {
    return cache_.size();
}

std::size_t IrCache::total_size_bytes() const {
    std::size_t total = 0;
    for (const auto &[source_path, entry] : cache_) {
        (void)source_path;
        total += entry.serialized_typed_hir.size();
    }
    return total;
}

} // namespace ahfl::incremental
