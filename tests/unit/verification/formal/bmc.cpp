#include "verification/formal/bmc.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace ahfl::formal;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

// ============================================================================
// run_bmc tests
// ============================================================================

void test_bmc_safe_no_violation() {
    BmcStateMachine machine;
    machine.name = "simple";
    machine.states = {"idle", "working", "done"};
    machine.initial_state = "idle";
    machine.final_states = {"done"};
    machine.transitions = {
        {"idle", "working"},
        {"working", "done"},
    };
    machine.properties = {"never(done)"}; // "done" IS reachable, but "never" checks safety

    // "never(done)" means done should never be reached. Since done IS reachable, expect Unsafe.
    BmcOptions options;
    options.max_bound = 5;

    auto result = run_bmc(machine, options);
    check(result.status == BmcStatus::Unsafe, "bmc_safe.unsafe_when_reachable");
    check(result.counterexample.has_value(), "bmc_safe.has_counterexample");
    check(result.elapsed_ms >= 0.0, "bmc_safe.has_timing");
}

void test_bmc_safe_unreachable_state() {
    BmcStateMachine machine;
    machine.name = "disconnected";
    machine.states = {"idle", "working", "orphan"};
    machine.initial_state = "idle";
    machine.final_states = {};
    machine.transitions = {
        {"idle", "working"},
        {"working", "idle"},
    };
    // "orphan" is unreachable
    machine.properties = {"never(orphan)"};

    BmcOptions options;
    options.max_bound = 10;

    auto result = run_bmc(machine, options);
    check(result.status == BmcStatus::Safe, "bmc_unreachable.safe");
    check(!result.counterexample.has_value(), "bmc_unreachable.no_counterexample");
    check(result.bound_reached == 10, "bmc_unreachable.bound_reached");
}

void test_bmc_reachable_property() {
    BmcStateMachine machine;
    machine.name = "reachability";
    machine.states = {"a", "b", "c"};
    machine.initial_state = "a";
    machine.final_states = {"c"};
    machine.transitions = {
        {"a", "b"},
        {"b", "c"},
    };
    machine.properties = {"reachable(c)"};

    BmcOptions options;
    options.max_bound = 5;

    auto result = run_bmc(machine, options);
    check(result.status == BmcStatus::Safe, "bmc_reachable.safe_when_reachable");
}

void test_bmc_empty_machine() {
    BmcStateMachine machine;
    machine.name = "empty";
    machine.states = {};
    machine.initial_state = "";
    machine.transitions = {};
    machine.properties = {"never(x)"};

    BmcOptions options;

    auto result = run_bmc(machine, options);
    check(result.status == BmcStatus::Error, "bmc_empty.reports_error");
    check(!result.error_message.empty(), "bmc_empty.has_actionable_message");
}

void test_bmc_bad_state_beyond_bound() {
    BmcStateMachine machine;
    machine.name = "deep";
    machine.states = {"s0", "s1", "s2", "s3"};
    machine.initial_state = "s0";
    machine.transitions = {
        {"s0", "s1"},
        {"s1", "s2"},
        {"s2", "s3"},
    };
    // "s3" is reachable, but only after 3 transitions.
    machine.properties = {"never(s3)"};

    BmcOptions options;
    options.max_bound = 2;
    check(run_bmc(machine, options).status == BmcStatus::Safe,
          "bmc_beyond_bound.safe_when_bound_too_shallow");

    options.max_bound = 3;
    auto reached = run_bmc(machine, options);
    check(reached.status == BmcStatus::Unsafe, "bmc_beyond_bound.unsafe_at_depth");
    check(reached.bound_reached == 3, "bmc_beyond_bound.depth_is_step_count");
    check(reached.counterexample.has_value() &&
              reached.counterexample->trace_states ==
                  std::vector<std::string>{"s0", "s1", "s2", "s3"},
          "bmc_beyond_bound.trace_is_shortest_path");
}

} // anonymous namespace

int main() {
    test_bmc_safe_no_violation();
    test_bmc_safe_unreachable_state();
    test_bmc_reachable_property();
    test_bmc_empty_machine();
    test_bmc_bad_state_beyond_bound();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
