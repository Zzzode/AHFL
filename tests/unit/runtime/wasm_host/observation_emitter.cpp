// RFC 0026 KR6.8 WH-4: KAT for the observation emitter.
//
// Pins the emitted ahfl.node-observation.v1 document byte-for-byte against
// the JS oracle's emitObservation (tests/conformance/node_embedded_host.mjs:
// 531-549). The field set, sorted key order, string escaping, and raw-value
// embedding must all match exactly so the conformance comparator sees the
// same document shape regardless of which engine produced it.

#include "runtime/wasm_host/observation_emitter.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::runtime::wasm_host::emit_observation;
using ahfl::runtime::wasm_host::ObservationData;
using ahfl::runtime::wasm_host::StateEntry;

int g_checks = 0;

void check(bool condition, std::string_view label) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << label << "\n";
        std::exit(1);
    }
}

void check_eq(std::string_view actual, std::string_view expected,
              std::string_view label) {
    ++g_checks;
    if (actual != expected) {
        std::cerr << "FAIL: " << label << "\n";
        std::cerr << "  expected: " << expected << "\n";
        std::cerr << "  actual:   " << actual << "\n";
        std::exit(1);
    }
}

// ---- Agent lane: no capabilities, no output, no workflow_completed_count ----

void test_agent_minimal() {
    ObservationData data;
    data.case_name = "tests/golden/runtime/simple_agent.ahfl";
    data.scenario_name = "default";
    data.status = "completed";
    data.states = {{.agent = "SimpleAgent", .state = "Init"},
                   {.agent = "SimpleAgent", .state = "Done"}};
    data.capabilities = {};
    data.capability_arguments = {};
    data.output_json = std::nullopt;
    data.transition_count = 1;
    data.workflow_completed_count = std::nullopt;

    // Sorted keys: capability_arguments, capability_sequence, case, scenario,
    // schema, state_sequence, status, transition_count.
    static constexpr std::string_view kExpected =
        R"({"capability_arguments":[],"capability_sequence":[],)"
        R"("case":"tests/golden/runtime/simple_agent.ahfl",)"
        R"("scenario":"default","schema":"ahfl.node-observation.v1",)"
        R"("state_sequence":[{"agent":"SimpleAgent","state":"Init"},)"
        R"({"agent":"SimpleAgent","state":"Done"}],)"
        R"("status":"completed","transition_count":1})";

    check_eq(emit_observation(data), kExpected, "agent_minimal");
}

// ---- Workflow lane: capabilities, arguments, output, completed_count ----

void test_workflow_full() {
    ObservationData data;
    data.case_name = "tests/golden/runtime/e2e_multi_agent.ahfl";
    data.scenario_name = "priority_high";
    data.status = "completed";
    data.states = {
        {.agent = "ClassifyAgent", .state = "Init"},
        {.agent = "ClassifyAgent", .state = "Classifying"},
        {.agent = "HandleAgent", .state = "Init"},
        {.agent = "HandleAgent", .state = "Done"},
    };
    data.capabilities = {
        "runtime::e2e_multi_agent::ClassifyMessage",
        "runtime::e2e_multi_agent::HandleTechnical",
    };
    // Pre-serialized wire envelopes from serialize_args_for_wire_json.
    data.capability_arguments = {
        R"({"value":"My server is crashing"})",
        R"({"args":["user_456","My server is crashing"]})",
    };
    // Pre-serialized output from value_to_json.
    data.output_json =
        R"({"_type":"runtime::e2e_multi_agent::SummaryResult",)"
        R"("category":{"_enum":"runtime::e2e_multi_agent::Category",)"
        R"("_variant":"Technical"},"resolved":true,)"
        R"("summary":"Case resolved successfully"})";
    data.transition_count = 3;
    data.workflow_completed_count = 2;

    // Sorted keys: capability_arguments, capability_sequence, case,
    // output_json, scenario, schema, state_sequence, status,
    // transition_count, workflow_completed_count.
    static constexpr std::string_view kExpected =
        R"({"capability_arguments":[{"value":"My server is crashing"},)"
        R"({"args":["user_456","My server is crashing"]}],)"
        R"("capability_sequence":["runtime::e2e_multi_agent::ClassifyMessage",)"
        R"("runtime::e2e_multi_agent::HandleTechnical"],)"
        R"("case":"tests/golden/runtime/e2e_multi_agent.ahfl",)"
        R"("output_json":{"_type":"runtime::e2e_multi_agent::SummaryResult",)"
        R"("category":{"_enum":"runtime::e2e_multi_agent::Category",)"
        R"("_variant":"Technical"},"resolved":true,)"
        R"("summary":"Case resolved successfully"},)"
        R"("scenario":"priority_high","schema":"ahfl.node-observation.v1",)"
        R"("state_sequence":[{"agent":"ClassifyAgent","state":"Init"},)"
        R"({"agent":"ClassifyAgent","state":"Classifying"},)"
        R"({"agent":"HandleAgent","state":"Init"},)"
        R"({"agent":"HandleAgent","state":"Done"}],)"
        R"("status":"completed","transition_count":3,)"
        R"("workflow_completed_count":2})";

    check_eq(emit_observation(data), kExpected, "workflow_full");
}

// ---- JSON string escaping: quotes, backslashes, control chars ----

void test_string_escaping() {
    ObservationData data;
    data.case_name = "test\"quote\\backslash\nnewline";
    data.scenario_name = "tab\there";
    data.status = "failed";
    data.states = {{.agent = "A\"B", .state = "S\\T"}};
    data.capabilities = {};
    data.capability_arguments = {};
    data.output_json = std::nullopt;
    data.transition_count = 0;
    data.workflow_completed_count = std::nullopt;

    static constexpr std::string_view kExpected =
        R"({"capability_arguments":[],"capability_sequence":[],)"
        R"("case":"test\"quote\\backslash\nnewline",)"
        R"("scenario":"tab\there","schema":"ahfl.node-observation.v1",)"
        R"("state_sequence":[{"agent":"A\"B","state":"S\\T"}],)"
        R"("status":"failed","transition_count":0})";

    check_eq(emit_observation(data), kExpected, "string_escaping");
}

// ---- Empty observation: zero states, zero capabilities ----

void test_empty() {
    ObservationData data;
    data.case_name = "empty";
    data.scenario_name = "default";
    data.status = "failed";
    data.states = {};
    data.capabilities = {};
    data.capability_arguments = {};
    data.output_json = std::nullopt;
    data.transition_count = 0;
    data.workflow_completed_count = std::nullopt;

    static constexpr std::string_view kExpected =
        R"({"capability_arguments":[],"capability_sequence":[],)"
        R"("case":"empty","scenario":"default",)"
        R"("schema":"ahfl.node-observation.v1","state_sequence":[],)"
        R"("status":"failed","transition_count":0})";

    check_eq(emit_observation(data), kExpected, "empty");
}

// ---- Suspended status with output and workflow_completed_count ----

void test_suspended() {
    ObservationData data;
    data.case_name = "suspend_case";
    data.scenario_name = "pending";
    data.status = "suspended";
    data.states = {{.agent = "Worker", .state = "Working"}};
    data.capabilities = {"cap"};
    data.capability_arguments = {R"({"value":42})"};
    data.output_json = std::nullopt;
    data.transition_count = 0;
    data.workflow_completed_count = 0;

    static constexpr std::string_view kExpected =
        R"({"capability_arguments":[{"value":42}],)"
        R"("capability_sequence":["cap"],"case":"suspend_case",)"
        R"("scenario":"pending","schema":"ahfl.node-observation.v1",)"
        R"("state_sequence":[{"agent":"Worker","state":"Working"}],)"
        R"("status":"suspended","transition_count":0,)"
        R"("workflow_completed_count":0})";

    check_eq(emit_observation(data), kExpected, "suspended");
}

// ---- Determinism: same input always produces same bytes ----

void test_determinism() {
    ObservationData data;
    data.case_name = "det";
    data.scenario_name = "s1";
    data.status = "completed";
    data.states = {{.agent = "A", .state = "S1"}, {.agent = "A", .state = "S2"}};
    data.capabilities = {"c1", "c2"};
    data.capability_arguments = {R"({})", R"({"value":1})"};
    data.output_json = "42";
    data.transition_count = 1;
    data.workflow_completed_count = 1;

    const std::string first = emit_observation(data);
    const std::string second = emit_observation(data);
    check(first == second, "determinism");
}

} // namespace

int main() {
    test_agent_minimal();
    test_workflow_full();
    test_string_escaping();
    test_empty();
    test_suspended();
    test_determinism();

    std::cout << "observation_emitter: " << g_checks << " checks passed\n";
    return 0;
}
