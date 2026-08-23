#include "tooling/profiling/memory_tracker.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace ahfl::profiling {

ProcessMemoryStats read_process_memory_stats() noexcept {
    ProcessMemoryStats stats;

#if defined(__linux__)
    // /proc/self/status reports VmRSS in kB.
    if (FILE *f = std::fopen("/proc/self/status", "r")) {
        char line[256];
        while (std::fgets(line, sizeof(line), f) != nullptr) {
            if (std::strncmp(line, "VmRSS:", 6) == 0) {
                unsigned long rss_kb = 0;
                if (std::sscanf(line + 6, "%lu", &rss_kb) == 1) {
                    stats.rss_bytes = static_cast<std::size_t>(rss_kb) * 1024;
                }
                break;
            }
        }
        std::fclose(f);
    }
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        stats.rss_bytes = static_cast<std::size_t>(info.resident_size);
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        stats.rss_bytes = static_cast<std::size_t>(pmc.WorkingSetSize);
    }
#endif

    return stats;
}

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
