#include "ahfl/runtime/parallel_scheduler.hpp"
#include "ahfl/runtime/data_pipeline.hpp"
#include <cstdio>
#include <stdexcept>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

int main() {
    // Test 1: Simple linear DAG
    {
        ahfl::runtime::ParallelScheduler sched(2);
        sched.add_node({"A", {}, [] { return "a_out"; }});
        sched.add_node({"B", {"A"}, [] { return "b_out"; }});
        sched.add_node({"C", {"B"}, [] { return "c_out"; }});

        check(sched.validate_dag(), "linear DAG is valid");
        auto result = sched.execute();
        check(result.success, "linear DAG executes successfully");
        check(result.node_results.size() == 3, "all nodes executed");
    }

    // Test 2: Cycle detection
    {
        ahfl::runtime::ParallelScheduler sched(2);
        sched.add_node({"A", {"C"}, nullptr});
        sched.add_node({"B", {"A"}, nullptr});
        sched.add_node({"C", {"B"}, nullptr});

        check(!sched.validate_dag(), "cyclic DAG detected");
    }

    // Test 3: Fail-fast propagation
    {
        ahfl::runtime::ParallelScheduler sched(2, ahfl::runtime::FailurePropagationMode::FailFast);
        sched.add_node({"A", {}, []() -> std::string { throw std::runtime_error("fail"); }});
        sched.add_node({"B", {"A"}, [] { return "b"; }});

        auto result = sched.execute();
        check(!result.success, "fail-fast stops on error");
    }

    // Test 4: Data pipeline
    {
        ahfl::runtime::DataPipeline pipeline;
        pipeline.publish("node_a", "hello");
        check(pipeline.has_value("node_a"), "pipeline has published value");
        check(!pipeline.has_value("node_b"), "pipeline lacks unpublished value");
        auto val = pipeline.consume("node_a");
        check(val.has_value() && *val == "hello", "pipeline consume returns correct value");
        check(pipeline.size() == 1, "pipeline size is correct");
    }

    // Test 5: Diamond DAG
    {
        ahfl::runtime::ParallelScheduler sched(4);
        sched.add_node({"root", {}, [] { return "r"; }});
        sched.add_node({"left", {"root"}, [] { return "l"; }});
        sched.add_node({"right", {"root"}, [] { return "r"; }});
        sched.add_node({"join", {"left", "right"}, [] { return "j"; }});

        check(sched.validate_dag(), "diamond DAG is valid");
        auto result = sched.execute();
        check(result.success, "diamond DAG executes successfully");
        check(result.node_results.size() == 4, "all diamond nodes executed");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
