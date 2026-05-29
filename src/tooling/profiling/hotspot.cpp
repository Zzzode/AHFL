#include "tooling/profiling/hotspot.hpp"

#include <algorithm>
#include <utility>

namespace ahfl::profiling {

void HotspotAnalyzer::record_call(std::string name, std::chrono::nanoseconds duration) {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        HotspotEntry entry;
        entry.name = name;
        entry.total_time = duration;
        entry.call_count = 1;
        entries_.emplace(std::move(name), std::move(entry));
    } else {
        it->second.total_time += duration;
        ++it->second.call_count;
    }
}

std::vector<HotspotEntry> HotspotAnalyzer::top_hotspots(std::size_t n) const {
    std::vector<HotspotEntry> result;
    result.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end(), [](const HotspotEntry& a, const HotspotEntry& b) {
        return a.total_time > b.total_time;
    });
    if (result.size() > n) {
        result.resize(n);
    }
    return result;
}

std::size_t HotspotAnalyzer::entry_count() const {
    return entries_.size();
}

void HotspotAnalyzer::clear() {
    entries_.clear();
}

} // namespace ahfl::profiling
