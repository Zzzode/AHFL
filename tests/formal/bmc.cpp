#include "ahfl/formal/bmc.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

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
    check(result.status == BmcStatus::Safe || result.status == BmcStatus::Error,
          "bmc_empty.safe_or_error");
}

// ============================================================================
// run_k_induction tests
// ============================================================================

void test_k_induction_safe() {
    BmcStateMachine machine;
    machine.name = "safe_loop";
    machine.states = {"a", "b"};
    machine.initial_state = "a";
    machine.final_states = {};
    machine.transitions = {
        {"a", "b"},
        {"b", "a"},
    };
    machine.properties = {"never(orphan)"};

    BmcOptions options;
    options.max_bound = 5;

    auto result = run_k_induction(machine, options);
    check(result.status == BmcStatus::Safe, "k_ind.safe");
}

void test_k_induction_unsafe() {
    BmcStateMachine machine;
    machine.name = "unsafe";
    machine.states = {"a", "b", "c"};
    machine.initial_state = "a";
    machine.final_states = {};
    machine.transitions = {
        {"a", "b"},
        {"b", "c"},
    };
    machine.properties = {"never(c)"};

    BmcOptions options;
    options.max_bound = 5;

    auto result = run_k_induction(machine, options);
    check(result.status == BmcStatus::Unsafe, "k_ind.unsafe");
    check(result.counterexample.has_value(), "k_ind.has_cex");
}

// ============================================================================
// run_cegar tests
// ============================================================================

void test_cegar_returns_unknown() {
    BmcStateMachine machine;
    machine.name = "test";
    machine.states = {"a", "b"};
    machine.initial_state = "a";
    machine.transitions = {{"a", "b"}};
    machine.properties = {"never(b)"};

    BmcOptions options;
    auto result = run_cegar(machine, options);
    check(result.status == BmcStatus::Unknown, "cegar.returns_unknown");
}

} // anonymous namespace

int main() {
    test_bmc_safe_no_violation();
    test_bmc_safe_unreachable_state();
    test_bmc_reachable_property();
    test_bmc_empty_machine();
    test_k_induction_safe();
    test_k_induction_unsafe();
    test_cegar_returns_unknown();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
