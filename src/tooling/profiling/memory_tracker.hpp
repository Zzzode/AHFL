#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace ahfl::profiling {

struct AllocationRecord {
    std::string tag;
    std::size_t bytes;
};

/// Platform-specific process memory statistics. Fields are std::nullopt when
/// the platform does not expose the metric. The structural proxy fields in
/// MemoryReportSnapshot remain the cross-platform comparable baseline; these
/// are supplementary observation only.
struct ProcessMemoryStats {
    /// Resident set size in bytes (physical memory currently held by the
    /// process). Read from /proc/self/status on Linux, task_info on macOS,
    /// GetProcessMemoryInfo on Windows.
    std::optional<std::size_t> rss_bytes;
};

/// Read the current process's memory statistics. Returns a stats object with
/// nullopt fields on platforms where the metric is unavailable. Never throws.
[[nodiscard]] ProcessMemoryStats read_process_memory_stats() noexcept;

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
