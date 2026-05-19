#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace ahfl::profiling {

struct PassTimingRecord {
    std::string pass_name;
    std::chrono::nanoseconds duration;
};

class PassProfiler {
public:
    void begin_pass(std::string name);
    void end_pass();
    [[nodiscard]] std::vector<PassTimingRecord> records() const;
    [[nodiscard]] std::chrono::nanoseconds total_time() const;
    void clear();
private:
    std::vector<PassTimingRecord> records_;
    std::string current_pass_;
    std::chrono::steady_clock::time_point current_start_;
};

} // namespace ahfl::profiling
