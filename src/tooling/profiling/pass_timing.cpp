#include "tooling/profiling/pass_timing.hpp"

#include <utility>

namespace ahfl::profiling {

void PassProfiler::begin_pass(std::string name) {
    current_pass_ = std::move(name);
    current_start_ = std::chrono::steady_clock::now();
}

void PassProfiler::end_pass() {
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - current_start_);
    records_.push_back(PassTimingRecord{std::move(current_pass_), duration});
    current_pass_.clear();
}

std::vector<PassTimingRecord> PassProfiler::records() const {
    return records_;
}

std::chrono::nanoseconds PassProfiler::total_time() const {
    std::chrono::nanoseconds total{0};
    for (const auto &rec : records_) {
        total += rec.duration;
    }
    return total;
}

void PassProfiler::clear() {
    records_.clear();
    current_pass_.clear();
}

} // namespace ahfl::profiling
