#include "verification/formal/incremental_verifier.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace ahfl::formal {

namespace {

/// FNV-1a 64-bit hash
uint64_t fnv1a_hash(std::string_view data) {
    constexpr uint64_t fnv_offset_basis = 14695981039346656037ULL;
    constexpr uint64_t fnv_prime = 1099511628211ULL;

    uint64_t hash = fnv_offset_basis;
    for (unsigned char c : data) {
        hash ^= static_cast<uint64_t>(c);
        hash *= fnv_prime;
    }
    return hash;
}

std::string hash_to_hex(uint64_t hash) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

} // namespace

[[nodiscard]] IncrementalState
IncrementalVerifier::compute_diff(const std::vector<PropertyHash> &previous,
                                  const std::vector<PropertyHash> &current) {
    IncrementalState state;
    state.previous_hashes = previous;

    // Build lookup maps
    std::unordered_map<std::string, std::string> prev_map;
    for (const auto &ph : previous) {
        prev_map[ph.property_name] = ph.content_hash;
    }

    std::unordered_map<std::string, std::string> curr_map;
    for (const auto &ph : current) {
        curr_map[ph.property_name] = ph.content_hash;
    }

    // Classify current properties
    for (const auto &ph : current) {
        auto it = prev_map.find(ph.property_name);
        if (it == prev_map.end()) {
            state.new_properties.push_back(ph.property_name);
        } else if (it->second == ph.content_hash) {
            state.unchanged_properties.push_back(ph.property_name);
        } else {
            state.changed_properties.push_back(ph.property_name);
        }
    }

    // Find removed properties
    for (const auto &ph : previous) {
        if (curr_map.find(ph.property_name) == curr_map.end()) {
            state.removed_properties.push_back(ph.property_name);
        }
    }

    return state;
}

[[nodiscard]] std::string
IncrementalVerifier::hash_property(std::string_view property_text) {
    return hash_to_hex(fnv1a_hash(property_text));
}

} // namespace ahfl::formal
