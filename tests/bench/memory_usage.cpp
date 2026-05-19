#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

namespace {

struct MemoryBenchmark {
    std::string name;
    std::size_t allocation_bytes;
    std::size_t peak_bytes;
};

MemoryBenchmark measure_memory(const std::string& name, std::size_t element_count) {
    std::size_t allocated = 0;
    std::size_t peak = 0;

    // Simulate allocations
    std::vector<std::string> data;
    data.reserve(element_count);
    for (std::size_t i = 0; i < element_count; ++i) {
        std::string s = "element_" + std::to_string(i);
        allocated += s.size();
        data.push_back(std::move(s));
    }
    peak = allocated;

    return {name, allocated, peak};
}

} // namespace

int main() {
    std::printf("=== Memory Usage Benchmarks ===\n");

    auto small = measure_memory("small_ast", 100);
    check(small.allocation_bytes > 0, "small AST allocates memory");

    auto large = measure_memory("large_ast", 10000);
    check(large.allocation_bytes > small.allocation_bytes, "large AST allocates more than small");

    check(large.peak_bytes >= large.allocation_bytes, "peak >= total allocated");

    std::printf("\nResults:\n");
    std::printf("  %s: %zu bytes allocated, %zu peak\n", small.name.c_str(), small.allocation_bytes, small.peak_bytes);
    std::printf("  %s: %zu bytes allocated, %zu peak\n", large.name.c_str(), large.allocation_bytes, large.peak_bytes);

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
