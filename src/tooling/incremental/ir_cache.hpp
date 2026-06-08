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
    // Aggregated fingerprint of every declaration the type checker
    // produced for this module's TypeEnvironment, derived from
    // TypeEnvironment::signature_fingerprint(). Used by downstream
    // dependents to detect "shape" changes that survive identical
    // content_hash (e.g. whitespace-only edits flip content_hash but
    // not the fingerprint, while semantic edits flip both).
    std::uint64_t signature_fingerprint{0};
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
