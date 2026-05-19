#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace ahfl::incremental {

struct CacheEntry {
    std::string module_path;
    std::uint64_t content_hash;
    std::string serialized_ir;  // JSON string
    std::chrono::system_clock::time_point cached_at;
};

enum class CacheHitKind { Hit, Miss, Stale };

struct CacheLookupResult {
    CacheHitKind kind;
    std::optional<CacheEntry> entry;
};

class IrCache {
public:
    void store(CacheEntry entry);
    [[nodiscard]] CacheLookupResult lookup(const std::string& module_path, std::uint64_t current_hash) const;
    void invalidate(const std::string& module_path);
    void clear();
    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] std::size_t total_size_bytes() const;

private:
    std::unordered_map<std::string, CacheEntry> cache_;
};

} // namespace ahfl::incremental
