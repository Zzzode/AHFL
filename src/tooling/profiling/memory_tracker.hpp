#pragma once

#include <cstddef>
#include <string>

namespace ahfl::profiling {

struct AllocationRecord {
    std::string tag;
    std::size_t bytes;
};

class MemoryTracker {
  public:
    void record_allocation(std::string tag, std::size_t bytes);
    void record_deallocation(std::string tag, std::size_t bytes);
    [[nodiscard]] std::size_t current_usage() const;
    [[nodiscard]] std::size_t peak_usage() const;
    [[nodiscard]] std::size_t allocation_count() const;
    void reset();

  private:
    std::size_t current_ = 0;
    std::size_t peak_ = 0;
    std::size_t alloc_count_ = 0;
};

} // namespace ahfl::profiling
