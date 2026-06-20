#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name);
    } else {
        std::printf("  FAIL: %s\n", name);
    }
}

namespace {

struct CompileTimeBenchmark {
    std::string name;
    std::size_t input_lines;
    std::chrono::nanoseconds duration;
};

CompileTimeBenchmark measure_compile_time(const std::string &name, std::size_t lines) {
    auto start = std::chrono::steady_clock::now();

    // Simulate compilation work proportional to input size
    std::atomic<std::size_t> work{0};
    for (std::size_t i = 0; i < lines * 100; ++i) {
        work.fetch_add(i, std::memory_order_relaxed);
    }
    (void)work.load(std::memory_order_relaxed);

    auto end = std::chrono::steady_clock::now();
    return {name, lines, std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)};
}

} // namespace

int main() {
    std::printf("=== Compile Time Benchmarks ===\n");

    auto small = measure_compile_time("small_module", 100);
    check(small.duration.count() > 0, "small module has positive duration");

    auto medium = measure_compile_time("medium_module", 1000);
    check(medium.duration.count() > 0, "medium module has positive duration");

    auto large = measure_compile_time("large_module", 10000);
    check(large.duration >= small.duration, "large module takes >= small module time");

    std::printf("\nResults:\n");
    std::printf("  %s: %zu lines in %lld ns\n",
                small.name.c_str(),
                small.input_lines,
                static_cast<long long>(small.duration.count()));
    std::printf("  %s: %zu lines in %lld ns\n",
                medium.name.c_str(),
                medium.input_lines,
                static_cast<long long>(medium.duration.count()));
    std::printf("  %s: %zu lines in %lld ns\n",
                large.name.c_str(),
                large.input_lines,
                static_cast<long long>(large.duration.count()));

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
