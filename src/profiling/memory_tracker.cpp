#include "profiling/memory_tracker.hpp"

#include <algorithm>

namespace ahfl::profiling {

void MemoryTracker::record_allocation(std::string /*tag*/, std::size_t bytes) {
    current_ += bytes;
    ++alloc_count_;
    peak_ = std::max(peak_, current_);
}

void MemoryTracker::record_deallocation(std::string /*tag*/, std::size_t bytes) {
    if (bytes <= current_) {
        current_ -= bytes;
    } else {
        current_ = 0;
    }
}

std::size_t MemoryTracker::current_usage() const {
    return current_;
}

std::size_t MemoryTracker::peak_usage() const {
    return peak_;
}

std::size_t MemoryTracker::allocation_count() const {
    return alloc_count_;
}

void MemoryTracker::reset() {
    current_ = 0;
    peak_ = 0;
    alloc_count_ = 0;
}

} // namespace ahfl::profiling
