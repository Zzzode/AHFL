#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::profiling {

struct HotspotEntry {
    std::string name;
    std::chrono::nanoseconds total_time;
    std::size_t call_count;
};

class HotspotAnalyzer {
  public:
    void record_call(std::string name, std::chrono::nanoseconds duration);
    [[nodiscard]] std::vector<HotspotEntry> top_hotspots(std::size_t n = 10) const;
    [[nodiscard]] std::size_t entry_count() const;
    void clear();

  private:
    std::unordered_map<std::string, HotspotEntry> entries_;
};

} // namespace ahfl::profiling
