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

struct SmvSizeBenchmark {
    std::string name;
    std::size_t agent_count;
    std::size_t state_count;
    std::size_t output_bytes;
};

SmvSizeBenchmark measure_smv_size(const std::string& name, std::size_t agents, std::size_t states_per_agent) {
    // Simulate SMV output generation
    std::string output;
    output += "MODULE main\n";

    for (std::size_t a = 0; a < agents; ++a) {
        output += "VAR agent_" + std::to_string(a) + " : {";
        for (std::size_t s = 0; s < states_per_agent; ++s) {
            if (s > 0) output += ", ";
            output += "s" + std::to_string(s);
        }
        output += "};\n";
    }

    output += "ASSIGN\n";
    for (std::size_t a = 0; a < agents; ++a) {
        output += "  init(agent_" + std::to_string(a) + ") := s0;\n";
    }

    return {name, agents, states_per_agent, output.size()};
}

} // namespace

int main() {
    std::printf("=== SMV Output Size Benchmarks ===\n");

    auto small = measure_smv_size("single_agent", 1, 3);
    check(small.output_bytes > 0, "single agent produces output");

    auto medium = measure_smv_size("multi_agent", 5, 5);
    check(medium.output_bytes > small.output_bytes, "multi-agent produces more output");

    auto large = measure_smv_size("large_system", 20, 10);
    check(large.output_bytes > medium.output_bytes, "large system produces most output");

    std::printf("\nResults:\n");
    std::printf("  %s: %zu agents x %zu states = %zu bytes\n",
        small.name.c_str(), small.agent_count, small.state_count, small.output_bytes);
    std::printf("  %s: %zu agents x %zu states = %zu bytes\n",
        medium.name.c_str(), medium.agent_count, medium.state_count, medium.output_bytes);
    std::printf("  %s: %zu agents x %zu states = %zu bytes\n",
        large.name.c_str(), large.agent_count, large.state_count, large.output_bytes);

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
