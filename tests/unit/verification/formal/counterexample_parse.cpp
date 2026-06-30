#include "verification/formal/checker.hpp"
#include "verification/formal/counterexample.hpp"
#include "verification/formal/counterexample_json.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
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
    constexpr auto model = "-- AHFL_MAP agent__X__state => agent X state @ <unknown>:50-100\n";

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
    constexpr auto output =
        R"(-- specification  G (agent__MyAgent__state = Working -> X (agent__MyAgent__state = Done))  is false
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
        check(explanation.steps[0].find("initial state") != std::string::npos, "explain.step0_initial");
    }
    // Second step should mention the change from 0 to 1
    if (explanation.steps.size() >= 2) {
        check(explanation.steps[1].find("changed") != std::string::npos, "explain.step1_change");
    }
    // Third step: no change
    if (explanation.steps.size() >= 3) {
        check(explanation.steps[2].find("no change") != std::string::npos, "explain.step2_no_change");
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
        check(last.find("infinite loop") != std::string::npos, "explain_loop.mentions_loop");
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
    constexpr auto checker_output =
        R"(-- specification  G (agent__TaskBot__state = AHFL_AGENT_RUNNING -> X (agent__TaskBot__state = AHFL_AGENT_DONE))  is false
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
        check(explanation.steps.back().find("infinite loop") != std::string::npos,
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

void test_report_includes_structured_explanation() {
    FormalVerificationResult result;
    result.status = FormalVerificationStatus::Failed;
    result.checker_path = "/tmp/fake-smv";
    result.failing_specifications.push_back("-- specification LTLSPEC G FALSE is false");
    result.counterexample_excerpt.push_back("Trace Description: LTL Counterexample");
    result.structured_explanation_json = R"({"status":"failed","explanation":{"summary":"x"}})";

    std::ostringstream output;
    print_formal_verification_report(result, output);

    const auto text = output.str();
    check(text.find("error: formal verification failed") != std::string::npos,
          "report_explain.has_failure_header");
    check(text.find("counterexample explanation:") != std::string::npos,
          "report_explain.has_explanation_section");
    check(text.find("\"status\":\"failed\"") != std::string::npos, "report_explain.includes_json");
}

// ============================================================================
// h-12 (QW-3) 4-dim mapping tests (2 golden
// ============================================================================

void test_h12_D4_classify_contract() {
    // G1
    auto never_info = classify_violated_contract("never(PAID_STATE)");
    check(never_info.kind == ViolatedContractKind::Invariant, "d4.never_kind");
    check(never_info.description.find("PAID_STATE") != std::string::npos, "d4.never_desc");
    check(never_info.raw_spec == "never(PAID_STATE)", "d4.never_raw");
    // G2
    auto reach_info = classify_violated_contract("reachable(PAID_STATE)");
    check(reach_info.kind == ViolatedContractKind::Ensures, "d4.reach_kind");
    check(reach_info.description.find("progress obligation") != std::string::npos, "d4.reach_desc");
    // G3 - leading-space G
    auto g_info = classify_violated_contract("  G (agent__MyAgent__state = IDLE)");
    check(g_info.kind == ViolatedContractKind::Invariant, "d4.G_kind");
    // G4 - F eventually
    auto f_info = classify_violated_contract("F (agent__MyAgent__state = Done)");
    check(f_info.kind == ViolatedContractKind::Ensures, "d4.F_kind");
    // G5 - Custom
    auto c_info = classify_violated_contract("G F done != done");
    check(c_info.kind == ViolatedContractKind::Custom, "d4.custom_kind");
    // G6 - Empty
    auto u_info = classify_violated_contract("");
    check(u_info.kind == ViolatedContractKind::Unknown, "d4.empty_kind");
}

void test_h12_full_4dim_enhance() {
    // Trace with AHFL_MAP agent state, workflow phase, input and context
    // transitions.
    CounterexampleTrace trace;
    trace.violated_spec = "never(PAID)";
    trace.trace_description = "LTL Counterexample";
    trace.trace_type = "Counterexample";

    SourceMapping ag_map{
        .smv_symbol = "agent__OrderBot__state",
        .description = "agent shop::OrderBot state",
        .source_path = "order.ahfl",
        .begin_offset = 20,
        .end_offset = 120,
    };
    SourceMapping wf_map{
        .smv_symbol = "workflow__Checkout__node__pay__phase",
        .description = "workflow shop::Checkout pay phase",
        .source_path = "order.ahfl",
        .begin_offset = 200,
        .end_offset = 300,
    };
    SourceMapping input_map{
        .smv_symbol = "input__retry_count",
        .description = "input retry_count",
        .source_path = "order.ahfl",
        .begin_offset = 10,
        .end_offset = 18,
    };
    SourceMapping ctx_map{
        .smv_symbol = "context__balance",
        .description = "context balance",
        .source_path = "order.ahfl",
        .begin_offset = 30,
        .end_offset = 40,
    };

    // 3 states.
    trace.states.push_back(CounterexampleState{
        .label = "1.1",
        .assignments =
            {
                CounterexampleAssignment{.variable = "agent__OrderBot__state", .value = "Idle", .mapping = ag_map},
                CounterexampleAssignment{.variable = "workflow__Checkout__node__pay__phase",
                                      .value = "Idle",
                                      .mapping = wf_map},
                CounterexampleAssignment{.variable = "input__retry_count", .value = "2", .mapping = input_map},
                CounterexampleAssignment{.variable = "context__balance", .value = "0", .mapping = ctx_map},
            },
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.2",
        .assignments =
            {
                CounterexampleAssignment{.variable = "agent__OrderBot__state", .value = "Working", .mapping = ag_map},
                CounterexampleAssignment{.variable = "workflow__Checkout__node__pay__phase",
                                      .value = "Running",
                                      .mapping = wf_map},
                CounterexampleAssignment{.variable = "context__balance", .value = "0", .mapping = ctx_map},
            },
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.3",
        .assignments =
            {
                CounterexampleAssignment{.variable = "agent__OrderBot__state", .value = "PAID", .mapping = ag_map},
                CounterexampleAssignment{.variable = "workflow__Checkout__node__pay__phase",
                                      .value = "Running",
                                      .mapping = wf_map},
                // context diverged between initial and final
                CounterexampleAssignment{.variable = "context__balance", .value = "99", .mapping = ctx_map},
            },
    });

    ViolationExplanation expl;
    expl.violated_property = trace.violated_spec;
    enhance_counterexample_mapping(trace, expl);

    // D4 contract
    check(expl.violated_contract.kind == ViolatedContractKind::Invariant, "h12.d4_kind");
    check(trace.state_transitions.size() == 2, "h12.d1_len");

    // D1 (step 1) agent+wf
    const auto &step0 = trace.state_transitions[0];
    check(step0.size() == 2, "h12.d1_s0_count"); // agent + wf change
    bool has_agent_step0 = false;
    bool has_wf_step0 = false;
    for (const auto &r : step0) {
        if (r.role == "agent_state") {
            has_agent_step0 = true;
            check(r.owner_name == "OrderBot", "h12.d1_s0_owner");
            check(r.value_from == "Idle", "h12.d1_s0_from");
            check(r.value_to == "Working", "h12.d1_s0_to");
            check(r.source_path == "order.ahfl", "h12.d1_path");
        }
        if (r.role == "workflow_phase") {
            has_wf_step0 = true;
            check(r.owner_name == "Checkout.node.pay", "h12.d1_s0_wfowner");
        }
    }
    check(has_agent_step0, "h12.d1_s0_agent");
    check(has_wf_step0, "h12.d1_s0_wf");

    // D1 step 2: agent Idle→Working→PAID; wf stays, context change.
    const auto &step1 = trace.state_transitions[1];
    check(step1.size() == 2, "h12.d1_s1_count");
    for (const auto &r : step1) {
        if (r.role == "agent_state") {
            check(r.value_from == "Working", "h12.d1_s1_from");
            check(r.value_to == "PAID", "h12.d1_s1_to");
        }
    }

    // D2 trigger_input
    check(trace.trigger_input.size() == 1, "h12.d2_count");
    check(trace.trigger_input[0].logical_path == "input.retry_count", "h12.d2_path");
    check(trace.trigger_input[0].value == "2", "h12.d2_value");
    check(trace.trigger_input[0].initial_value.empty(), "h12.d2_no_initial");

    // D3 faulty_ctx
    check(trace.faulty_ctx_fields.size() == 1, "h12.d3_count");
    check(trace.faulty_ctx_fields[0].logical_path == "context.balance", "h12.d3_path");
    check(trace.faulty_ctx_fields[0].value == "99", "h12.d3_final");
    check(trace.faulty_ctx_fields[0].initial_value == "0", "h12.d3_initial");

    // JSON round-trip: serialize and verify 4dim fields appear.
    const auto json = counterexample_to_json(trace, expl);
    check(json.find("\"violated_contract\"") != std::string::npos, "h12.json.d4_in_json");
    check(json.find("\"invariant\"") != std::string::npos, "h12.json.d4_kind_in_json");
    check(json.find("\"state_transitions\"") != std::string::npos, "h12.json.d1_in_json");
    check(json.find("\"trigger_input\"") != std::string::npos, "h12.json.d2_in_json");
    check(json.find("\"faulty_ctx_fields\"") != std::string::npos, "h12.json.d3_in_json");
    check(json.find("\"OrderBot\"") != std::string::npos, "h12.json.d1_owner_in_json");
    check(json.find("\"input.retry_count\"") != std::string::npos, "h12.json.d2_path_in_json");
    check(json.find("\"context.balance\"") != std::string::npos, "h12.json.d3_path_in_json");
}

// ============================================================================
// h-12 D5: action_trace_alignment
// ============================================================================

void test_h12_D5_action_trace_alignment() {
    // Build a 3-state synthetic trace that includes AHFL transition
    // `_fired` / `_guard` variables with the documented mangling pattern.
    CounterexampleTrace trace;
    trace.violated_spec = "never(PAYMENT_TIMEOUT)";
    trace.trace_description = "LTL Counterexample";
    trace.trace_type = "Counterexample";

    SourceMapping trans_fire1{
        .smv_symbol = "__ahfl_transition__Checkout__node__pay__authorize_fired",
        .description = "transition authorize fires",
        .source_path = "order.ahfl",
        .begin_offset = 310,
        .end_offset = 340,
    };
    SourceMapping trans_guard1{
        .smv_symbol = "__ahfl_transition__Checkout__node__pay__authorize_guard",
        .description = "transition authorize guard",
        .source_path = "order.ahfl",
        .begin_offset = 290,
        .end_offset = 310,
    };
    SourceMapping trans_fire2{
        .smv_symbol = "__ahfl_transition__Checkout__node__ship__dispatch_fired",
        .description = "transition dispatch fires",
        .source_path = "order.ahfl",
        .begin_offset = 410,
        .end_offset = 440,
    };
    SourceMapping trans_guard2{
        .smv_symbol = "__ahfl_transition__Checkout__node__ship__dispatch_guard",
        .description = "transition dispatch guard",
        .source_path = "order.ahfl",
        .begin_offset = 390,
        .end_offset = 410,
    };

    // State 1: initial state (no actions fired).
    trace.states.push_back(CounterexampleState{
        .label = "1.1",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_fired",
                    .value = "FALSE",
                    .mapping = trans_fire1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_guard",
                    .value = "FALSE",
                    .mapping = trans_guard1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_fired",
                    .value = "FALSE",
                    .mapping = trans_fire2,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_guard",
                    .value = "TRUE",
                    .mapping = trans_guard2,
                },
            },
    });
    // State 2: step 0→1 fires authorize (guard=true) and dispatch stays off.
    trace.states.push_back(CounterexampleState{
        .label = "1.2",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_fired",
                    .value = "TRUE",
                    .mapping = trans_fire1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_guard",
                    .value = "TRUE",
                    .mapping = trans_guard1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_fired",
                    .value = "FALSE",
                    .mapping = trans_fire2,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_guard",
                    .value = "TRUE",
                    .mapping = trans_guard2,
                },
            },
    });
    // State 3: step 1→2 fires dispatch (guard=TRUE matches the variable name).
    trace.states.push_back(CounterexampleState{
        .label = "1.3",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_fired",
                    .value = "FALSE",
                    .mapping = trans_fire1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__pay__authorize_guard",
                    .value = "FALSE",
                    .mapping = trans_guard1,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_fired",
                    .value = "TRUE",
                    .mapping = trans_fire2,
                },
                CounterexampleAssignment{
                    .variable = "__ahfl_transition__Checkout__node__ship__dispatch_guard",
                    .value = "TRUE",
                    .mapping = trans_guard2,
                },
            },
    });

    ViolationExplanation expl;
    expl.violated_property = trace.violated_spec;
    enhance_counterexample_mapping(trace, expl);

    // 3 states → 2 steps.
    check(trace.action_trace.size() == 2, "d5.action_trace_len");
    check(trace.state_transitions.size() == trace.action_trace.size(),
          "d5.parallel_to_state_transitions");

    // Step 0: authorize fired, guard=TRUE.
    const auto &s0 = trace.action_trace[0];
    check(s0.size() == 1, "d5.s0_count");
    if (!s0.empty()) {
        check(s0[0].action_name == "authorize", "d5.s0_action");
        // logical_path must mention the owning workflow "Checkout".
        check(s0[0].logical_path.find("Checkout") != std::string::npos,
              "d5.s0_path_has_wf");
        // Also must mention the node.
        check(s0[0].logical_path.find("pay") != std::string::npos,
              "d5.s0_path_has_node");
        check(s0[0].guard_value == true, "d5.s0_guard_true");
        check(s0[0].source.source_path == "order.ahfl", "d5.s0_source_path");
    }

    // Step 1: dispatch fired, guard=TRUE.
    const auto &s1 = trace.action_trace[1];
    check(s1.size() == 1, "d5.s1_count");
    if (!s1.empty()) {
        check(s1[0].action_name == "dispatch", "d5.s1_action");
        check(s1[0].logical_path.find("Checkout") != std::string::npos,
              "d5.s1_path_has_wf");
        check(s1[0].logical_path.find("ship") != std::string::npos,
              "d5.s1_path_has_node");
        // Guard value must match the sibling `_guard` variable.
        check(s1[0].guard_value == true, "d5.s1_guard_true");
    }

    // JSON serialization must include the new field.
    const auto json = counterexample_to_json(trace, expl);
    check(json.find("\"action_trace\"") != std::string::npos, "d5.json.has_action_trace");
    check(json.find("\"logical_path\"") != std::string::npos, "d5.json.has_logical_path");
    check(json.find("\"action_name\"") != std::string::npos, "d5.json.has_action_name");
    check(json.find("\"guard_value\"") != std::string::npos, "d5.json.has_guard_value");
    check(json.find("\"authorize\"") != std::string::npos, "d5.json.authorize_present");
    check(json.find("\"dispatch\"") != std::string::npos, "d5.json.dispatch_present");
}

// ============================================================================
// h-12 D6: natural_language_summary for all 4 ViolatedContractKind values
// ============================================================================

namespace {

// Helper: run enhance and return natural_language_summary for a trace with
// `spec` plus optional D3 faulty_ctx_fields population.
std::string run_d6_summary(std::string_view spec, ViolatedContractKind expected_kind) {
    CounterexampleTrace trace;
    trace.violated_spec = std::string(spec);
    trace.states.push_back(CounterexampleState{
        .label = "1.1",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "context__balance",
                    .value = "0",
                    .mapping = SourceMapping{.smv_symbol = "context__balance",
                                             .description = "context balance",
                                             .source_path = "t.ahfl",
                                             .begin_offset = 10,
                                             .end_offset = 20},
                },
            },
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.2",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "context__balance",
                    .value = "999",
                    .mapping = SourceMapping{.smv_symbol = "context__balance",
                                             .description = "context balance",
                                             .source_path = "t.ahfl",
                                             .begin_offset = 10,
                                             .end_offset = 20},
                },
            },
    });
    trace.states.push_back(CounterexampleState{
        .label = "1.3",
        .assignments =
            {
                CounterexampleAssignment{
                    .variable = "context__balance",
                    .value = "999",
                    .mapping = SourceMapping{.smv_symbol = "context__balance",
                                             .description = "context balance",
                                             .source_path = "t.ahfl",
                                             .begin_offset = 10,
                                             .end_offset = 20},
                },
            },
    });

    ViolationExplanation expl;
    expl.violated_property = trace.violated_spec;
    enhance_counterexample_mapping(trace, expl);

    check(expl.violated_contract.kind == expected_kind,
          std::string("d6.kind_match for ") + std::string(spec));
    return expl.natural_language_summary;
}

// Count newlines in a string (line count = 1 + newlines, clamped to 1).
std::size_t count_lines(const std::string &s) {
    std::size_t n = 1;
    for (char c : s)
        if (c == '\n') ++n;
    return n;
}

} // namespace (inner)

void test_h12_D6_natural_language_summary() {
    // Kind 1: Invariant (via never() pattern).
    {
        const auto s = run_d6_summary("never(BAD_BALANCE)", ViolatedContractKind::Invariant);
        check(!s.empty(), "d6.inv.non_empty");
        check(s.find("violated") != std::string::npos, "d6.inv.has_violated");
        check(s.find("bounds") != std::string::npos, "d6.inv.has_bounds");
        check(count_lines(s) <= 4, "d6.inv.at_most_4_lines");
    }
    // Kind 2: Ensures (via reachable() pattern).
    {
        const auto s = run_d6_summary("reachable(PAID)", ViolatedContractKind::Ensures);
        check(!s.empty(), "d6.ens.non_empty");
        check(s.find("failed") != std::string::npos, "d6.ens.has_failed");
        check(s.find("post-condition") != std::string::npos,
              "d6.ens.has_post_condition");
        check(count_lines(s) <= 4, "d6.ens.at_most_4_lines");
    }
    // Kind 3: Custom (e.g. a combined G F that isn't a pure safety/eventuality).
    {
        const auto s = run_d6_summary("G F p != q", ViolatedContractKind::Custom);
        check(!s.empty(), "d6.cus.non_empty");
        check(s.find("failed") != std::string::npos ||
                  s.find("violated") != std::string::npos,
              "d6.cus.has_failed_or_violated");
        check(s.find("custom") != std::string::npos, "d6.cus.mentions_custom");
        check(count_lines(s) <= 4, "d6.cus.at_most_4_lines");
    }
    // Kind 4: Unknown (empty spec).
    {
        const auto s = run_d6_summary("", ViolatedContractKind::Unknown);
        check(!s.empty(), "d6.unk.non_empty");
        check(s.find("failed") != std::string::npos, "d6.unk.has_failed");
        check(count_lines(s) <= 4, "d6.unk.at_most_4_lines");
    }

    // JSON serialization must carry natural_language_summary.
    CounterexampleTrace trace;
    trace.violated_spec = "never(X)";
    trace.states.push_back(CounterexampleState{.label = "1.1", .assignments = {}});
    trace.states.push_back(CounterexampleState{.label = "1.2", .assignments = {}});
    ViolationExplanation expl;
    expl.violated_property = trace.violated_spec;
    enhance_counterexample_mapping(trace, expl);
    const auto json = counterexample_to_json(trace, expl);
    check(json.find("\"natural_language_summary\"") != std::string::npos,
          "d6.json.has_field");
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
    test_report_includes_structured_explanation();

    test_h12_D4_classify_contract();
    test_h12_full_4dim_enhance();
    test_h12_D5_action_trace_alignment();
    test_h12_D6_natural_language_summary();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
