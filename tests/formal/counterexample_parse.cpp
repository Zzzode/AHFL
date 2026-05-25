#include "formal/counterexample.hpp"
#include "formal/counterexample_json.hpp"

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
// Test parse_structured_symbol_mappings
// ============================================================================

void test_parse_mappings_basic() {
    constexpr auto model = R"(MODULE main
-- AHFL_MAP agent__MyAgent__state => agent test::MyAgent state @ test.ahfl:100-200
-- AHFL_MAP workflow__Wf__node__n1__phase => workflow test::Wf node n1 phase @ test.ahfl:300-450
VAR
)";

    auto mappings = parse_structured_symbol_mappings(model);
    check(mappings.size() == 2, "mappings.count_2");

    auto it = mappings.find("agent__MyAgent__state");
    check(it != mappings.end(), "mappings.has_agent_state");
    if (it != mappings.end()) {
        check(it->second.description == "agent test::MyAgent state", "mappings.agent_desc");
        check(it->second.source_path == "test.ahfl", "mappings.agent_path");
        check(it->second.begin_offset == 100, "mappings.agent_begin");
        check(it->second.end_offset == 200, "mappings.agent_end");
    }

    auto it2 = mappings.find("workflow__Wf__node__n1__phase");
    check(it2 != mappings.end(), "mappings.has_workflow_phase");
    if (it2 != mappings.end()) {
        check(it2->second.description == "workflow test::Wf node n1 phase", "mappings.wf_desc");
        check(it2->second.begin_offset == 300, "mappings.wf_begin");
        check(it2->second.end_offset == 450, "mappings.wf_end");
    }
}

void test_parse_mappings_no_source_location() {
    constexpr auto model = "-- AHFL_MAP some_var => some description without location\n";

    auto mappings = parse_structured_symbol_mappings(model);
    check(mappings.size() == 1, "mappings_no_loc.count_1");

    auto it = mappings.find("some_var");
    check(it != mappings.end(), "mappings_no_loc.found");
    if (it != mappings.end()) {
        check(it->second.description == "some description without location",
              "mappings_no_loc.desc");
        check(it->second.source_path.empty(), "mappings_no_loc.no_path");
    }
}

void test_parse_mappings_unknown_path() {
    constexpr auto model =
        "-- AHFL_MAP agent__X__state => agent X state @ <unknown>:50-100\n";

    auto mappings = parse_structured_symbol_mappings(model);
    check(mappings.size() == 1, "mappings_unknown.count_1");

    auto it = mappings.find("agent__X__state");
    check(it != mappings.end(), "mappings_unknown.found");
    if (it != mappings.end()) {
        check(it->second.source_path == "<unknown>", "mappings_unknown.path");
        check(it->second.begin_offset == 50, "mappings_unknown.begin");
        check(it->second.end_offset == 100, "mappings_unknown.end");
    }
}

// ============================================================================
// Test parse_counterexample_trace
// ============================================================================

void test_parse_trace_basic() {
    constexpr auto output = R"(-- specification  G (agent__MyAgent__state = Working -> X (agent__MyAgent__state = Done))  is false
-- as demonstrated by the following execution sequence
Trace Description: LTL Counterexample
Trace Type: Counterexample
  -> State: 1.1 <-
    agent__MyAgent__state = Init
    workflow__Wf__node__n1__phase = AHFL_NODE_IDLE
  -> State: 1.2 <-
    agent__MyAgent__state = Working
    workflow__Wf__node__n1__phase = AHFL_NODE_RUNNING
  -> State: 1.3 <-
    agent__MyAgent__state = Working
)";

    std::unordered_map<std::string, SourceMapping> mappings;
    mappings["agent__MyAgent__state"] = SourceMapping{
        .smv_symbol = "agent__MyAgent__state",
        .description = "agent MyAgent state",
        .source_path = "test.ahfl",
        .begin_offset = 100,
        .end_offset = 200,
    };

    auto trace = parse_counterexample_trace(output, mappings);
    check(trace.has_value(), "trace.has_value");
    if (!trace)
        return;

    check(trace->trace_description == "LTL Counterexample", "trace.description");
    check(trace->trace_type == "Counterexample", "trace.type");
    check(trace->violated_spec.find("agent__MyAgent__state") != std::string::npos,
          "trace.violated_spec_contains_symbol");
    check(trace->states.size() == 3, "trace.state_count_3");

    if (trace->states.size() >= 3) {
        check(trace->states[0].label == "1.1", "trace.state0_label");
        check(trace->states[1].label == "1.2", "trace.state1_label");
        check(trace->states[2].label == "1.3", "trace.state2_label");

        // First state should have 2 assignments
        check(trace->states[0].assignments.size() == 2, "trace.state0_assign_count");

        // Check mapping was attached
        if (!trace->states[0].assignments.empty()) {
            auto &first_assign = trace->states[0].assignments[0];
            check(first_assign.variable == "agent__MyAgent__state", "trace.state0_a0_var");
            check(first_assign.value == "Init", "trace.state0_a0_val");
            check(first_assign.mapping.has_value(), "trace.state0_a0_has_mapping");
            if (first_assign.mapping) {
                check(first_assign.mapping->description == "agent MyAgent state",
                      "trace.state0_a0_mapping_desc");
            }
        }
    }

    check(!trace->loop_start_index.has_value(), "trace.no_loop");
}

void test_parse_trace_with_loop() {
    constexpr auto output = R"(-- specification  G F (done = TRUE)  is false
Trace Description: LTL Counterexample
Trace Type: Counterexample
  -> State: 1.1 <-
    done = FALSE
  -- Loop starts here
  -> State: 1.2 <-
    done = FALSE
  -> State: 1.3 <-
    done = FALSE
)";

    std::unordered_map<std::string, SourceMapping> mappings;
    auto trace = parse_counterexample_trace(output, mappings);
    check(trace.has_value(), "trace_loop.has_value");
    if (!trace)
        return;

    check(trace->states.size() == 3, "trace_loop.state_count_3");
    check(trace->loop_start_index.has_value(), "trace_loop.has_loop");
    if (trace->loop_start_index) {
        check(*trace->loop_start_index == 1, "trace_loop.loop_at_1");
    }
}

void test_parse_trace_no_counterexample() {
    constexpr auto output = R"(-- specification  G (safe = TRUE)  is true
)";

    std::unordered_map<std::string, SourceMapping> mappings;
    auto trace = parse_counterexample_trace(output, mappings);
    check(!trace.has_value(), "no_trace.nullopt");
}

// ============================================================================
// Test explain_counterexample
// ============================================================================

void test_explain_basic() {
    CounterexampleTrace trace;
    trace.violated_spec = "G (x = 1 -> X (x = 2))";
    trace.trace_description = "LTL Counterexample";
    trace.trace_type = "Counterexample";

    SourceMapping mapping{
        .smv_symbol = "x",
        .description = "variable x",
        .source_path = "test.ahfl",
        .begin_offset = 0,
        .end_offset = 10,
    };

    trace.states.push_back(CounterexampleState{
        .label = "1.1",
        .assignments = {{.variable = "x", .value = "0", .mapping = mapping}},
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.2",
        .assignments = {{.variable = "x", .value = "1", .mapping = mapping}},
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.3",
        .assignments = {{.variable = "x", .value = "1", .mapping = mapping}},
    });

    auto explanation = explain_counterexample(trace);

    check(!explanation.summary.empty(), "explain.has_summary");
    check(explanation.violated_property == trace.violated_spec, "explain.violated_property");
    check(explanation.steps.size() == 3, "explain.step_count_3");

    // First step should mention initial state
    if (!explanation.steps.empty()) {
        check(explanation.steps[0].find("初始状态") != std::string::npos, "explain.step0_initial");
    }
    // Second step should mention the change from 0 to 1
    if (explanation.steps.size() >= 2) {
        check(explanation.steps[1].find("变为") != std::string::npos, "explain.step1_change");
    }
    // Third step: no change
    if (explanation.steps.size() >= 3) {
        check(explanation.steps[2].find("无变化") != std::string::npos, "explain.step2_no_change");
    }
}

void test_explain_with_loop() {
    CounterexampleTrace trace;
    trace.violated_spec = "G F done";
    trace.trace_description = "LTL Counterexample";
    trace.trace_type = "Counterexample";
    trace.loop_start_index = 1;

    trace.states.push_back(CounterexampleState{
        .label = "1.1",
        .assignments = {{.variable = "done", .value = "FALSE", .mapping = std::nullopt}},
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.2",
        .assignments = {{.variable = "done", .value = "FALSE", .mapping = std::nullopt}},
    });

    auto explanation = explain_counterexample(trace);

    // Should have a loop note
    check(explanation.steps.size() >= 3, "explain_loop.has_loop_note");
    if (!explanation.steps.empty()) {
        auto &last = explanation.steps.back();
        check(last.find("无限循环") != std::string::npos, "explain_loop.mentions_loop");
    }
}

// ============================================================================
// Integration test: full parse → explain → json pipeline
// ============================================================================

void test_integration_parse_explain_json() {
    // Simulate a full NuSMV model with AHFL_MAP comments
    constexpr auto model = R"(MODULE main
-- AHFL_MAP agent__TaskBot__state => agent test::TaskBot state @ test.ahfl:10-50
-- AHFL_MAP workflow__Pipeline__node__dispatch__phase => workflow test::Pipeline node dispatch phase @ test.ahfl:100-200
VAR
  agent__TaskBot__state : {AHFL_AGENT_IDLE, AHFL_AGENT_RUNNING, AHFL_AGENT_DONE};
  workflow__Pipeline__node__dispatch__phase : {AHFL_NODE_IDLE, AHFL_NODE_RUNNING, AHFL_NODE_COMPLETED};
LTLSPEC G (agent__TaskBot__state = AHFL_AGENT_RUNNING -> X (agent__TaskBot__state = AHFL_AGENT_DONE))
)";

    // Simulate NuSMV checker output with a counterexample
    constexpr auto checker_output = R"(-- specification  G (agent__TaskBot__state = AHFL_AGENT_RUNNING -> X (agent__TaskBot__state = AHFL_AGENT_DONE))  is false
-- as demonstrated by the following execution sequence
Trace Description: LTL Counterexample
Trace Type: Counterexample
  -> State: 1.1 <-
    agent__TaskBot__state = AHFL_AGENT_IDLE
    workflow__Pipeline__node__dispatch__phase = AHFL_NODE_IDLE
  -> State: 1.2 <-
    agent__TaskBot__state = AHFL_AGENT_RUNNING
    workflow__Pipeline__node__dispatch__phase = AHFL_NODE_RUNNING
  -- Loop starts here
  -> State: 1.3 <-
    agent__TaskBot__state = AHFL_AGENT_RUNNING
    workflow__Pipeline__node__dispatch__phase = AHFL_NODE_RUNNING
)";

    // Step 1: Parse structured symbol mappings from the model
    auto mappings = parse_structured_symbol_mappings(model);
    check(mappings.size() == 2, "integration.mapping_count_2");
    check(mappings.count("agent__TaskBot__state") == 1, "integration.has_agent_mapping");
    check(mappings.count("workflow__Pipeline__node__dispatch__phase") == 1,
          "integration.has_workflow_mapping");

    // Step 2: Parse the counterexample trace
    auto trace = parse_counterexample_trace(checker_output, mappings);
    check(trace.has_value(), "integration.trace_parsed");
    if (!trace)
        return;

    check(trace->states.size() == 3, "integration.trace_state_count_3");
    check(trace->loop_start_index.has_value(), "integration.trace_has_loop");
    if (trace->loop_start_index) {
        check(*trace->loop_start_index == 2, "integration.loop_at_2");
    }
    check(!trace->violated_spec.empty(), "integration.has_violated_spec");

    // Verify mappings were attached to assignments
    if (!trace->states[0].assignments.empty()) {
        check(trace->states[0].assignments[0].mapping.has_value(),
              "integration.state0_a0_has_mapping");
        if (trace->states[0].assignments[0].mapping) {
            check(trace->states[0].assignments[0].mapping->source_path == "test.ahfl",
                  "integration.state0_a0_source_path");
        }
    }

    // Step 3: Generate explanation
    auto explanation = explain_counterexample(*trace);
    check(!explanation.summary.empty(), "integration.has_summary");
    check(!explanation.violated_property.empty(), "integration.has_violated_property");
    check(!explanation.steps.empty(), "integration.has_steps");
    // Should mention the loop
    if (!explanation.steps.empty()) {
        check(explanation.steps.back().find("无限循环") != std::string::npos,
              "integration.loop_note");
    }

    // Step 4: Serialize to JSON
    auto json = counterexample_to_json(*trace, explanation);
    check(!json.empty(), "integration.json_not_empty");

    // Verify JSON contains expected top-level fields
    check(json.find("\"status\"") != std::string::npos, "integration.json_has_status");
    check(json.find("\"failed\"") != std::string::npos, "integration.json_status_failed");
    check(json.find("\"violated_specification\"") != std::string::npos,
          "integration.json_has_violated_spec");
    check(json.find("\"explanation\"") != std::string::npos, "integration.json_has_explanation");
    check(json.find("\"summary\"") != std::string::npos, "integration.json_has_summary");
    check(json.find("\"steps\"") != std::string::npos, "integration.json_has_steps");
    check(json.find("\"trace\"") != std::string::npos, "integration.json_has_trace");
    check(json.find("\"states\"") != std::string::npos, "integration.json_has_states");
    check(json.find("\"assignments\"") != std::string::npos, "integration.json_has_assignments");
    check(json.find("\"loop_start_index\"") != std::string::npos,
          "integration.json_has_loop_start");

    // Verify source mapping information is in JSON
    check(json.find("\"source\"") != std::string::npos, "integration.json_has_source");
    check(json.find("\"test.ahfl\"") != std::string::npos, "integration.json_has_source_path");
    check(json.find("\"description\"") != std::string::npos, "integration.json_has_description");
    check(json.find("agent test::TaskBot state") != std::string::npos,
          "integration.json_has_agent_desc");
}

} // anonymous namespace

int main() {
    test_parse_mappings_basic();
    test_parse_mappings_no_source_location();
    test_parse_mappings_unknown_path();

    test_parse_trace_basic();
    test_parse_trace_with_loop();
    test_parse_trace_no_counterexample();

    test_explain_basic();
    test_explain_with_loop();

    test_integration_parse_explain_json();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
