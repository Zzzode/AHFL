#include <cstdio>
#include <chrono>
#include <string>
#include <thread>

#include "tooling/profiling/pass_timing.hpp"
#include "tooling/profiling/memory_tracker.hpp"
#include "tooling/profiling/hotspot.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

static void test_pass_timing() {
    ahfl::profiling::PassProfiler profiler;

    profiler.begin_pass("lexer");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.end_pass();

    profiler.begin_pass("parser");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    profiler.end_pass();

    auto records = profiler.records();
    check(records.size() == 2, "two passes recorded");
    check(records[0].pass_name == "lexer", "first pass is lexer");
    check(records[1].pass_name == "parser", "second pass is parser");
    check(records[0].duration.count() > 0, "lexer duration > 0");
    check(profiler.total_time().count() > 0, "total time > 0");
}

static void test_memory_tracking() {
    ahfl::profiling::MemoryTracker tracker;

    tracker.record_allocation("ast_nodes", 1024);
    tracker.record_allocation("symbol_table", 512);
    check(tracker.current_usage() == 1536, "current usage is 1536");
    check(tracker.peak_usage() == 1536, "peak usage is 1536");
    check(tracker.allocation_count() == 2, "allocation count is 2");

    tracker.record_deallocation("ast_nodes", 1024);
    check(tracker.current_usage() == 512, "current usage after dealloc is 512");
    check(tracker.peak_usage() == 1536, "peak usage remains 1536 after dealloc");
}

static void test_hotspot_ranking() {
    ahfl::profiling::HotspotAnalyzer analyzer;

    analyzer.record_call("type_check", std::chrono::nanoseconds(500));
    analyzer.record_call("type_check", std::chrono::nanoseconds(300));
    analyzer.record_call("codegen", std::chrono::nanoseconds(1000));
    analyzer.record_call("parse", std::chrono::nanoseconds(200));

    auto hotspots = analyzer.top_hotspots(2);
    check(hotspots.size() == 2, "top_hotspots(2) returns 2 entries");
    check(hotspots[0].name == "codegen", "top hotspot is codegen (1000ns)");
    check(hotspots[1].name == "type_check", "second hotspot is type_check (800ns total)");
    check(hotspots[1].call_count == 2, "type_check was called 2 times");
    check(analyzer.entry_count() == 3, "analyzer has 3 unique entries");
}

static void test_empty_profiler() {
    ahfl::profiling::PassProfiler profiler;
    check(profiler.records().empty(), "empty profiler has no records");
    check(profiler.total_time().count() == 0, "empty profiler has zero total time");

    ahfl::profiling::MemoryTracker tracker;
    check(tracker.current_usage() == 0, "fresh tracker has zero usage");
    check(tracker.peak_usage() == 0, "fresh tracker has zero peak");
    check(tracker.allocation_count() == 0, "fresh tracker has zero alloc count");

    ahfl::profiling::HotspotAnalyzer analyzer;
    check(analyzer.entry_count() == 0, "fresh analyzer has zero entries");
    auto hotspots = analyzer.top_hotspots(5);
    check(hotspots.empty(), "fresh analyzer returns empty hotspots");
}

int main() {
    std::printf("=== Profiling Tests ===\n\n");

    test_pass_timing();
    test_memory_tracking();
    test_hotspot_ranking();
    test_empty_profiler();

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
