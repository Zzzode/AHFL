#include "verification/formal/incremental_verifier.hpp"
#include "verification/formal/process_launcher.hpp"
#include "verification/formal/state_space_estimator.hpp"

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
// process_launcher tests
// ============================================================================

void test_find_executable_echo() {
    auto result = find_executable("echo");
    check(result.has_value(), "find_exec.echo_found");
    if (result) {
        check(!result->empty(), "find_exec.echo_not_empty");
    }
}

void test_find_executable_nonexistent() {
    auto result = find_executable("ahfl_nonexistent_binary_xyz_123");
    check(!result.has_value(), "find_exec.nonexistent_not_found");
}

void test_launch_process_echo() {
    ProcessConfig config;
    config.executable = "echo";
    config.arguments = {"hello", "world"};
    config.timeout = std::chrono::seconds{5};

    auto result = launch_process(config);
    check(result.exit_code == 0, "launch.echo_exit_0");
    check(result.stdout_output.find("hello world") != std::string::npos,
          "launch.echo_output");
    check(!result.timed_out, "launch.echo_no_timeout");
}

void test_launch_process_failure() {
    ProcessConfig config;
    config.executable = "false"; // returns exit 1
    config.timeout = std::chrono::seconds{5};

    auto result = launch_process(config);
    check(result.exit_code != 0, "launch.false_nonzero");
    check(!result.timed_out, "launch.false_no_timeout");
}

// ============================================================================
// incremental_verifier tests
// ============================================================================

void test_hash_property_deterministic() {
    auto h1 = IncrementalVerifier::hash_property("G (state = safe)");
    auto h2 = IncrementalVerifier::hash_property("G (state = safe)");
    check(h1 == h2, "hash.deterministic");
    check(!h1.empty(), "hash.not_empty");
    check(h1.size() == 16, "hash.is_16_hex_chars");
}

void test_hash_property_different_inputs() {
    auto h1 = IncrementalVerifier::hash_property("G (x = 1)");
    auto h2 = IncrementalVerifier::hash_property("G (x = 2)");
    check(h1 != h2, "hash.different_inputs_different_hashes");
}

void test_compute_diff_all_categories() {
    IncrementalVerifier verifier;

    std::vector<PropertyHash> previous = {
        {"prop_unchanged", "abc123"},
        {"prop_changed", "old_hash"},
        {"prop_removed", "xyz789"},
    };

    std::vector<PropertyHash> current = {
        {"prop_unchanged", "abc123"},
        {"prop_changed", "new_hash"},
        {"prop_new", "fresh_hash"},
    };

    auto state = verifier.compute_diff(previous, current);

    check(state.unchanged_properties.size() == 1, "diff.unchanged_count_1");
    check(state.changed_properties.size() == 1, "diff.changed_count_1");
    check(state.new_properties.size() == 1, "diff.new_count_1");
    check(state.removed_properties.size() == 1, "diff.removed_count_1");

    check(state.unchanged_properties[0] == "prop_unchanged", "diff.unchanged_name");
    check(state.changed_properties[0] == "prop_changed", "diff.changed_name");
    check(state.new_properties[0] == "prop_new", "diff.new_name");
    check(state.removed_properties[0] == "prop_removed", "diff.removed_name");
}

void test_compute_diff_empty() {
    IncrementalVerifier verifier;

    std::vector<PropertyHash> previous;
    std::vector<PropertyHash> current = {
        {"p1", "hash1"},
        {"p2", "hash2"},
    };

    auto state = verifier.compute_diff(previous, current);
    check(state.new_properties.size() == 2, "diff_empty.all_new");
    check(state.unchanged_properties.empty(), "diff_empty.no_unchanged");
    check(state.changed_properties.empty(), "diff_empty.no_changed");
    check(state.removed_properties.empty(), "diff_empty.no_removed");
}

// ============================================================================
// state_space_estimator tests
// ============================================================================

void test_estimate_single_agent() {
    std::vector<AgentMetrics> agents = {
        {"agent1", 5, 8, 3},
    };

    auto estimate = estimate_state_space(agents);
    check(estimate.estimated_states == 5, "estimate_single.states_5");
    check(estimate.num_agents == 1, "estimate_single.num_agents_1");
    check(estimate.max_states_per_agent == 5, "estimate_single.max_5");
    check(estimate.total_transitions == 8, "estimate_single.transitions_8");
    check(estimate.likely_tractable, "estimate_single.tractable");
    check(estimate.estimation_method == "multiplicative", "estimate_single.method");
}

void test_estimate_multiple_agents_tractable() {
    std::vector<AgentMetrics> agents = {
        {"a1", 10, 20, 2},
        {"a2", 8, 12, 3},
        {"a3", 5, 6, 1},
    };

    auto estimate = estimate_state_space(agents);
    check(estimate.estimated_states == 10 * 8 * 5, "estimate_multi.states_400");
    check(estimate.num_agents == 3, "estimate_multi.num_agents_3");
    check(estimate.max_states_per_agent == 10, "estimate_multi.max_10");
    check(estimate.total_transitions == 20 + 12 + 6, "estimate_multi.transitions_38");
    check(estimate.likely_tractable, "estimate_multi.tractable");
}

void test_estimate_intractable() {
    std::vector<AgentMetrics> agents = {
        {"a1", 1000, 5000, 10},
        {"a2", 2000, 3000, 5},
    };

    auto estimate = estimate_state_space(agents);
    check(estimate.estimated_states == 1000 * 2000, "estimate_big.states_2000000");
    check(!estimate.likely_tractable, "estimate_big.intractable");
    check(!estimate.warning.empty(), "estimate_big.has_warning");
}

void test_estimate_empty() {
    std::vector<AgentMetrics> agents;

    auto estimate = estimate_state_space(agents);
    check(estimate.estimated_states == 0, "estimate_empty.states_0");
    check(estimate.num_agents == 0, "estimate_empty.num_agents_0");
    check(estimate.likely_tractable, "estimate_empty.tractable");
}

} // anonymous namespace

int main() {
    test_find_executable_echo();
    test_find_executable_nonexistent();
    test_launch_process_echo();
    test_launch_process_failure();
    test_hash_property_deterministic();
    test_hash_property_different_inputs();
    test_compute_diff_all_categories();
    test_compute_diff_empty();
    test_estimate_single_agent();
    test_estimate_multiple_agents_tractable();
    test_estimate_intractable();
    test_estimate_empty();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
