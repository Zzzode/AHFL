#include "tooling/dap/dap_server.hpp"
#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/debug_session.hpp"
#include "base/json/json_value.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char *name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

namespace {

constexpr std::string_view kSimpleWorkflowSource = R"(module dap::simple_workflow;

struct Payload {
    tag: String;
}

struct Greeting {
    message: String;
}

agent GreeterAgent {
    input: Payload;
    context: Unit;
    output: Greeting;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for GreeterAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Greeting {
            message: "hello",
        };
    }
}

workflow GreetingWorkflow {
    input: Payload;
    output: Greeting;

    node greet: GreeterAgent(input);

    return: greet;
}
)";

// Write the simple workflow fixture next to the test binary and return its
// path. Tests run with the project root as working directory, but the temp
// directory keeps the source tree clean.
[[nodiscard]] std::string write_simple_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "simple_workflow.ahfl";
    std::ofstream out(path);
    out << kSimpleWorkflowSource;
    return path.string();
}

constexpr std::string_view kCapabilityWorkflowSource = R"(module dap::cap_workflow;

struct Payload {
    tag: String;
}

capability DoWork(request: Payload) -> Payload;
capability OtherWork(request: Payload) -> Payload;

agent WorkerAgent {
    input: Payload;
    context: Unit;
    output: Payload;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [DoWork, OtherWork];

    transition Init -> Done;
}

flow for WorkerAgent {
    state Init {
        goto Done;
    }

    state Done {
        let arg = Payload { tag: input.tag };
        return DoWork(arg);
    }
}

workflow WorkerWorkflow {
    input: Payload;
    output: Payload;

    node worker: WorkerAgent(input);

    return: worker;
}
)";

[[nodiscard]] std::string write_capability_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "capability_workflow.ahfl";
    std::ofstream out(path);
    out << kCapabilityWorkflowSource;
    return path.string();
}

// Three-state fixture (Init -> Middle -> Done) so stepping has a transition
// to land on between the breakpoint state and the final state (RFC 0015
// Slice 4).
constexpr std::string_view kStepperWorkflowSource = R"(module dap::stepper_workflow;

struct Payload {
    tag: String;
}

struct Ack {
    message: String;
}

agent StepperAgent {
    input: Payload;
    context: Unit;
    output: Ack;
    states: [Init, Middle, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Middle;
    transition Middle -> Done;
}

flow for StepperAgent {
    state Init {
        goto Middle;
    }

    state Middle {
        goto Done;
    }

    state Done {
        return Ack {
            message: "stepped",
        };
    }
}

workflow StepperWorkflow {
    input: Payload;
    output: Ack;

    node stepper: StepperAgent(input);

    return: stepper;
}
)";

[[nodiscard]] std::string write_stepper_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "stepper_workflow.ahfl";
    std::ofstream out(path);
    out << kStepperWorkflowSource;
    return path.string();
}

// Fixture with a structured (nested struct) node input so the variables
// request has real structured values to expand (RFC 0015 Slice 5).
constexpr std::string_view kVarsWorkflowSource = R"(module dap::vars_workflow;

struct Inner {
    x: Int;
}

struct Data {
    inner: Inner;
    tag: String;
}

agent VarsAgent {
    input: Data;
    context: Unit;
    output: Data;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for VarsAgent {
    state Init {
        goto Done;
    }

    state Done {
        return input;
    }
}

workflow VarsWorkflow {
    input: Data;
    output: Data;

    node first: VarsAgent(input);

    node second: VarsAgent(input) after [first];

    return: second;
}
)";

[[nodiscard]] std::string write_vars_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "vars_workflow.ahfl";
    std::ofstream out(path);
    out << kVarsWorkflowSource;
    return path.string();
}

// WH-8 (kr68 section 12.9.10 new test 6): a program with a good workflow
// and a sibling that fails wasm codegen. The bad workflow uses a nested
// constructor in its frame, which the wasm codegen rejects
// (UNSUPPORTED_WORKFLOW_FRAME). The facade constructor compiles ALL
// workflows; the sibling failure must surface as a launch failure.
constexpr std::string_view kSiblingFailWorkflowSource = R"(module dap::sibling_fail;

struct Payload {
    tag: String;
}

struct Greeting {
    message: String;
}

struct Inner {
    x: Int;
}

struct Data {
    inner: Inner;
    tag: String;
}

agent GreeterAgent {
    input: Payload;
    context: Unit;
    output: Greeting;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for GreeterAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Greeting {
            message: "hello",
        };
    }
}

workflow GoodWorkflow {
    input: Payload;
    output: Greeting;

    node greet: GreeterAgent(input);

    return: greet;
}

agent DataAgent {
    input: Data;
    context: Unit;
    output: Data;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for DataAgent {
    state Init {
        goto Done;
    }

    state Done {
        return input;
    }
}

workflow BadWorkflow {
    input: Data;
    output: Data;

    node data: DataAgent(Data {
        inner: Inner { x: 42 },
        tag: "bad",
    });

    return: data;
}
)";

[[nodiscard]] std::string write_sibling_fail_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "sibling_fail_workflow.ahfl";
    std::ofstream out(path);
    out << kSiblingFailWorkflowSource;
    return path.string();
}

// WH-8: the wasm3-backed runtime requires a non-empty struct input (the
// layout root must be non-empty). Every fixture's launch config therefore
// carries a matching "input" value.
constexpr std::string_view kSimpleInputJson =
    R"({"_type":"dap::simple_workflow::Payload","tag":"start"})";
constexpr std::string_view kCapabilityInputJson =
    R"({"_type":"dap::cap_workflow::Payload","tag":"start"})";
constexpr std::string_view kStepperInputJson =
    R"({"_type":"dap::stepper_workflow::Payload","tag":"start"})";
constexpr std::string_view kVarsInputJson =
    R"({"_type":"dap::vars_workflow::Data","inner":{"_type":"dap::vars_workflow::Inner","x":42},"tag":"hello"})";
constexpr std::string_view kSiblingFailInputJson =
    R"({"_type":"dap::sibling_fail::Payload","tag":"start"})";

[[nodiscard]] std::string launch_config(const std::string &program_path,
                                        const std::string &workflow_name,
                                        const std::string &input_json = "") {
    auto body = ahfl::json::JsonValue::make_object();
    body->set("program", ahfl::json::JsonValue::make_string(program_path));
    body->set("workflow", ahfl::json::JsonValue::make_string(workflow_name));
    if (!input_json.empty()) {
        auto parsed = ahfl::json::parse_json(input_json);
        if (parsed.has_value() && *parsed) {
            body->set("input", std::move(*parsed));
        }
    }
    return ahfl::json::serialize_json(*body);
}

// Build a DAP `evaluate` request body with an expression and optional frameId.
[[nodiscard]] std::string evaluate_config(const std::string &expression, int frame_id = 1) {
    auto body = ahfl::json::JsonValue::make_object();
    body->set("expression", ahfl::json::JsonValue::make_string(expression));
    body->set("frameId", ahfl::json::JsonValue::make_int(frame_id));
    return ahfl::json::serialize_json(*body);
}

[[nodiscard]] bool frame_has_event(const std::vector<std::string> &frames,
                                   std::string_view event_type) {
    const std::string needle = R"("event":")" + std::string(event_type) + R"(")";
    for (const auto &frame : frames) {
        if (frame.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// WH-8: check whether any captured frame is an unsolicited "breakpoint"
// event with the given verified flag and a message containing `needle`.
[[nodiscard]] bool frame_has_breakpoint_event(const std::vector<std::string> &frames,
                                              bool verified,
                                              std::string_view message_needle) {
    const std::string verified_str =
        verified ? R"("verified":true)" : R"("verified":false)";
    for (const auto &frame : frames) {
        if (frame.find(R"("event":"breakpoint")") != std::string::npos &&
            frame.find(verified_str) != std::string::npos &&
            frame.find(message_needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// WH-8: count "stopped" events in the captured frames.
[[nodiscard]] std::size_t count_stopped_events(const std::vector<std::string> &frames) {
    std::size_t count = 0;
    for (const auto &frame : frames) {
        if (frame.find(R"("event":"stopped")") != std::string::npos) {
            ++count;
        }
    }
    return count;
}

// Poll the captured event frames until an event of the given type appears or
// the timeout elapses.
template <typename Rep, typename Period>
[[nodiscard]] bool wait_for_event(const std::vector<std::string> &frames,
                                  std::mutex &mutex,
                                  std::string_view event_type,
                                  std::chrono::duration<Rep, Period> timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        {
            std::lock_guard lock(mutex);
            if (frame_has_event(frames, event_type)) {
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Poll the captured event frames until a `stopped` event with the given
// reason appears among frames at or after `start_index`, or the timeout
// elapses. Used to distinguish step pauses from earlier breakpoint pauses.
template <typename Rep, typename Period>
[[nodiscard]] bool wait_for_stopped_reason(const std::vector<std::string> &frames,
                                           std::mutex &mutex,
                                           std::string_view reason,
                                           std::chrono::duration<Rep, Period> timeout,
                                           std::size_t start_index = 0) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const std::string needle = R"("reason":")" + std::string(reason) + R"(")";
    while (true) {
        {
            std::lock_guard lock(mutex);
            for (std::size_t i = start_index; i < frames.size(); ++i) {
                if (frames[i].find(R"("event":"stopped")") != std::string::npos &&
                    frames[i].find(needle) != std::string::npos) {
                    return true;
                }
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Find the 1-based line number of the first line containing `needle` in a
// source string. Returns -1 when not found.
[[nodiscard]] int find_source_line(std::string_view source, std::string_view needle) {
    int line = 1;
    std::size_t pos = 0;
    while (pos < source.size()) {
        const auto line_end = source.find('\n', pos);
        const auto segment = line_end == std::string_view::npos
                                 ? source.substr(pos)
                                 : source.substr(pos, line_end - pos);
        if (segment.find(needle) != std::string_view::npos) {
            return line;
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        pos = line_end + 1;
        ++line;
    }
    return -1;
}

[[nodiscard]] std::string set_breakpoints_config(const std::string &source_path,
                                                 const std::vector<int> &lines) {
    auto body = ahfl::json::JsonValue::make_object();
    body->set("path", ahfl::json::JsonValue::make_string(source_path));
    auto lines_array = ahfl::json::JsonValue::make_array();
    for (const int line : lines) {
        lines_array->array_items.push_back(ahfl::json::JsonValue::make_int(line));
    }
    body->set("lines", std::move(lines_array));
    return ahfl::json::serialize_json(*body);
}

// Send a DAP request through the server and return the response body.
[[nodiscard]] std::string dap_command(ahfl::dap::DapServer &server,
                                      std::string_view command,
                                      std::string_view body_json = "{}") {
    ahfl::dap::DapMessage req;
    req.command = std::string(command);
    req.body = std::string(body_json);
    return server.handle_request(req).body;
}

[[nodiscard]] std::string json_string_or(const ahfl::json::JsonValue &object,
                                         std::string_view key,
                                         std::string_view fallback) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::string(fallback);
    }
    const auto value = field->as_string();
    return value ? std::string(*value) : std::string(fallback);
}

[[nodiscard]] int json_int_or(const ahfl::json::JsonValue &object, std::string_view key, int fallback) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return fallback;
    }
    const auto value = field->as_int();
    return value ? static_cast<int>(*value) : fallback;
}

// Find a variable by name in a DAP `variables` response. Returns nullptr if
// the response is not a variables response or the variable is absent.
[[nodiscard]] const ahfl::json::JsonValue *
find_variable(const ahfl::json::JsonValue &variables_response, std::string_view name) {
    const auto *vars = variables_response.get("variables");
    if (vars == nullptr || !vars->is_array()) {
        return nullptr;
    }
    for (const auto &var : vars->array_items) {
        if (!var || !var->is_object()) {
            continue;
        }
        if (json_string_or(*var, "name", "") == name) {
            return var.get();
        }
    }
    return nullptr;
}

// Find a scope by name in a DAP `scopes` response. Returns nullptr if absent.
[[nodiscard]] const ahfl::json::JsonValue *
find_scope(const ahfl::json::JsonValue &scopes_response, std::string_view name) {
    const auto *scopes = scopes_response.get("scopes");
    if (scopes == nullptr || !scopes->is_array()) {
        return nullptr;
    }
    for (const auto &scope : scopes->array_items) {
        if (!scope || !scope->is_object()) {
            continue;
        }
        if (json_string_or(*scope, "name", "") == name) {
            return scope.get();
        }
    }
    return nullptr;
}

// Find a stack frame by 1-based id in a DAP `stackTrace` response.
[[nodiscard]] const ahfl::json::JsonValue *
find_frame(const ahfl::json::JsonValue &stack_trace_response, int frame_id) {
    const auto *frames = stack_trace_response.get("stackFrames");
    if (frames == nullptr || !frames->is_array()) {
        return nullptr;
    }
    for (const auto &frame : frames->array_items) {
        if (!frame || !frame->is_object()) {
            continue;
        }
        if (json_int_or(*frame, "id", -1) == frame_id) {
            return frame.get();
        }
    }
    return nullptr;
}

// Find the 1-based line number of the nth occurrence of `needle` in a
// source string. Returns -1 when not found. Used when two agents share a
// state name and the second agent's handler line is needed (T4).
[[nodiscard]] int find_nth_source_line(std::string_view source,
                                       std::string_view needle, int nth) {
    int line = 1;
    int found = 0;
    std::size_t pos = 0;
    while (pos < source.size()) {
        const auto line_end = source.find('\n', pos);
        const auto segment = line_end == std::string_view::npos
                                 ? source.substr(pos)
                                 : source.substr(pos, line_end - pos);
        if (segment.find(needle) != std::string_view::npos) {
            ++found;
            if (found == nth) {
                return line;
            }
        }
        if (line_end == std::string_view::npos) {
            break;
        }
        pos = line_end + 1;
        ++line;
    }
    return -1;
}

// T3 fixture: two nodes on ONE identity-final agent (caps [], WireJson lane)
// with distinguishable outputs. The agent transforms the input (x + 1), so a
// chain (first <- input, second <- first) yields x=1 then x=2. The Done state
// returns the transformed input, so evaluate("output") after each node's
// post-mortem pause carries that node's x value.
constexpr std::string_view kTwoNodeWorkflowSource = R"(module dap::two_node_workflow;

struct Data {
    x: Int;
}

agent DataAgent {
    input: Data;
    context: Unit;
    output: Data;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for DataAgent {
    state Init {
        goto Done;
    }

    state Done {
        return Data {
            x: input.x + 1,
        };
    }
}

workflow TwoNodeWorkflow {
    input: Data;
    output: Data;

    node first: DataAgent(input);
    node second: DataAgent(first) after [first];

    return: second;
}
)";

[[nodiscard]] std::string write_two_node_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "two_node_workflow.ahfl";
    std::ofstream out(path);
    out << kTwoNodeWorkflowSource;
    return path.string();
}

constexpr std::string_view kTwoNodeInputJson =
    R"({"_type":"dap::two_node_workflow::Data","x":0})";

// T4 fixture: two agents declaring the same state name (Done) on different
// lines in one file. AgentA has a capability (so the workflow is P6 and a
// capability breakpoint can pause it); AgentB has no capabilities. A late
// setBreakpoints on AgentB's Done line must classify correctly (not "not a
// state handler") — this exercises F4.3's (agent, state) record and F5's
// late classification.
constexpr std::string_view kTwoAgentWorkflowSource = R"(module dap::two_agent_workflow;

struct Payload {
    tag: String;
}

capability DoWork(request: Payload) -> Payload;

agent AgentA {
    input: Payload;
    context: Unit;
    output: Payload;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [DoWork];

    transition Init -> Done;
}

flow for AgentA {
    state Init {
        goto Done;
    }

    state Done {
        let arg = Payload { tag: input.tag };
        return DoWork(arg);
    }
}

agent AgentB {
    input: Payload;
    context: Unit;
    output: Payload;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for AgentB {
    state Init {
        goto Done;
    }

    state Done {
        return input;
    }
}

workflow TwoAgentWorkflow {
    input: Payload;
    output: Payload;

    node a: AgentA(input);
    node b: AgentB(input) after [a];

    return: b;
}
)";

[[nodiscard]] std::string write_two_agent_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "two_agent_workflow.ahfl";
    std::ofstream out(path);
    out << kTwoAgentWorkflowSource;
    return path.string();
}

constexpr std::string_view kTwoAgentInputJson =
    R"({"_type":"dap::two_agent_workflow::Payload","tag":"start"})";

// T6a fixture (F6.1 MIXED): modeled on kTwoAgentWorkflowSource, but BOTH
// agents' flow declarations (and thus their Done handlers) are written on a
// single physical line — the grammar separates adjacent topLevelDecls and
// stateHandlers by whitespace only, so this is legal. AgentA has a
// capability (P6 lane; Done is LIVE at the capability boundary); AgentB has
// none (P6 lane; no-capability post-mortem). The per-record verdicts on the
// shared line therefore DIFFER, so the classifier must return the shared-
// line message instead of attributing one arbitrary handler's liveness.
constexpr std::string_view kSharedLineMixedWorkflowSource = R"(module dap::shared_line_mixed;

struct Payload {
    tag: String;
}

capability DoWork(request: Payload) -> Payload;

agent AgentA {
    input: Payload;
    context: Unit;
    output: Payload;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [DoWork];

    transition Init -> Done;
}

agent AgentB {
    input: Payload;
    context: Unit;
    output: Payload;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for AgentA { state Init { goto Done; } state Done { let arg = Payload { tag: input.tag }; return DoWork(arg); } } flow for AgentB { state Init { goto Done; } state Done { return input; } }

workflow SharedLineMixedWorkflow {
    input: Payload;
    output: Payload;

    node a: AgentA(input);
    node b: AgentB(input) after [a];

    return: b;
}
)";

[[nodiscard]] std::string write_shared_line_mixed_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "shared_line_mixed_workflow.ahfl";
    std::ofstream out(path);
    out << kSharedLineMixedWorkflowSource;
    return path.string();
}

constexpr std::string_view kSharedLineMixedInputJson =
    R"({"_type":"dap::shared_line_mixed::Payload","tag":"start"})";

// T6b fixture (F6.1 UNANIMOUS): two distinct no-cap identity-final agents
// (caps [] -> WireJson lane), one node per agent with an after-dependency,
// and BOTH agents' flow declarations (and Done handlers) on ONE physical
// line. Every record on the shared line classifies to the same WireJson
// verdict, so the classifier must collapse them to that single verdict —
// proving unanimous records do not trip the shared-line branch.
constexpr std::string_view kSharedLineUnanimousWorkflowSource = R"(module dap::shared_line_unanimous;

struct Data {
    x: Int;
}

agent AgentA {
    input: Data;
    context: Unit;
    output: Data;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

agent AgentB {
    input: Data;
    context: Unit;
    output: Data;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];

    transition Init -> Done;
}

flow for AgentA { state Init { goto Done; } state Done { return input; } } flow for AgentB { state Init { goto Done; } state Done { return input; } }

workflow SharedLineUnanimousWorkflow {
    input: Data;
    output: Data;

    node a: AgentA(input);
    node b: AgentB(input) after [a];

    return: b;
}
)";

[[nodiscard]] std::string write_shared_line_unanimous_workflow_fixture() {
    const auto dir = std::filesystem::temp_directory_path() / "ahfl_dap_tests";
    std::filesystem::create_directories(dir);
    const auto path = dir / "shared_line_unanimous_workflow.ahfl";
    std::ofstream out(path);
    out << kSharedLineUnanimousWorkflowSource;
    return path.string();
}

constexpr std::string_view kSharedLineUnanimousInputJson =
    R"({"_type":"dap::shared_line_unanimous::Data","x":0})";

} // namespace

int main() {
    // F3: WASM=OFF gate. The dap test target receives
    // AHFL_ENABLE_BACKEND_WASM=1 transitively in ON builds. Under OFF, the
    // DebugSession launch path refuses with a stderr "debug adapter requires
    // WASM backend support" message, so OFF runs cover non-execution
    // behavior only (server init, breakpoint management, event framing,
    // evaluate-with-no-session, and the launch-refusal test T5). Every
    // execution-dependent test (launch/run/continue/disconnect/stopped/
    // frames/variables from a run) is wrapped in `if constexpr
    // (kBackendWasm)`; assertions are NOT deleted, only gated.
#ifdef AHFL_ENABLE_BACKEND_WASM
    constexpr bool kBackendWasm = true;
#else
    constexpr bool kBackendWasm = false;
#endif

    // Test 1: DAP server initialize
    {
        ahfl::dap::DapServer server;
        check(!server.is_initialized(), "server not initialized initially");

        ahfl::dap::DapMessage req;
        req.type = ahfl::dap::DapMessageType::Request;
        req.command = "initialize";

        auto resp = server.handle_request(req);
        check(server.is_initialized(), "server initialized after request");
        check(resp.command == "initialize", "response command matches");
        check(!resp.body.empty(), "response has body");
    }

    // Test 2: Breakpoint management
    {
        ahfl::dap::BreakpointManager mgr;

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Processing";
        int id = mgr.add_breakpoint(bp);

        check(mgr.count() == 1, "breakpoint added");
        check(mgr.get_breakpoint(id).has_value(), "breakpoint retrievable");

        auto hits = mgr.check_state_breakpoints("agent1", "Processing");
        check(hits.size() == 1, "state breakpoint triggers");

        auto no_hits = mgr.check_state_breakpoints("agent1", "Done");
        check(no_hits.empty(), "no hit for different state");

        mgr.remove_breakpoint(id);
        check(mgr.count() == 0, "breakpoint removed");
    }

    // Test 3: DAP disconnect
    {
        ahfl::dap::DapServer server;

        ahfl::dap::DapMessage init_req;
        init_req.command = "initialize";
        (void)server.handle_request(init_req);
        check(server.is_initialized(), "initialized before disconnect");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        check(!server.is_initialized(), "not initialized after disconnect");
    }

    // Test 4: send_event produces a Content-Length framed event message
    {
        ahfl::dap::DapServer server;
        std::string captured;
        server.set_event_output([&captured](std::string_view framed) {
            captured = std::string(framed);
        });

        auto body = ahfl::json::JsonValue::make_object();
        body->set("reason", ahfl::json::JsonValue::make_string("breakpoint"));
        body->set("threadId", ahfl::json::JsonValue::make_int(1));
        server.send_event("stopped", std::move(body));

        const auto header_end = captured.find("\r\n\r\n");
        check(header_end != std::string::npos, "event frame has header terminator");
        check(captured.starts_with("Content-Length: "), "event frame starts with Content-Length");

        const auto length_text = captured.substr(std::string_view("Content-Length: ").size(),
                                                 header_end - std::string_view("Content-Length: ").size());
        const auto content_length = std::stoul(length_text);
        const auto json = captured.substr(header_end + 4);
        check(json.size() == content_length, "event frame Content-Length matches body size");
        check(json.find(R"("seq":1)") != std::string::npos, "event has seq");
        check(json.find(R"("type":"event")") != std::string::npos, "event has type event");
        check(json.find(R"("event":"stopped")") != std::string::npos,
              "event uses event field (not command)");
        check(json.find(R"("reason":"breakpoint")") != std::string::npos, "event body preserved");
    }

    // Test 5: send_event without a sink is a no-op (does not crash)
    {
        ahfl::dap::DapServer server;
        server.send_event("terminated", ahfl::json::JsonValue::make_object());
        check(true, "send_event without sink does not crash");
    }

    // Tests 6-23: execution-dependent (launch/run/continue/disconnect).
    // Gated under WASM=OFF (F3).
    if constexpr (kBackendWasm) {

    // Test 6: DebugSession launch runs a workflow and emits terminated
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        const auto resp = server.handle_request(launch_req);
        check(resp.command == "launch", "launch response command matches");

        check(wait_for_event(frames, frames_mutex, "initialized", std::chrono::seconds(5)),
              "initialized event emitted after launch");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after workflow completes");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        check(!server.is_initialized(), "server not initialized after disconnect");
    }

    // Test 7: state breakpoint pauses execution and emits stopped
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on state breakpoint hit");

        // The frame stack must reflect the paused state.
        const auto stack_body = dap_command(server, "stackTrace");
        const auto stack_parsed = ahfl::json::parse_json(stack_body);
        bool saw_done = false;
        if (stack_parsed.has_value() && *stack_parsed && (*stack_parsed)->is_object()) {
            const auto *stack_frames = (*stack_parsed)->get("stackFrames");
            if (stack_frames != nullptr && stack_frames->is_array()) {
                for (const auto &frame : stack_frames->array_items) {
                    if (frame && frame->is_object() &&
                        json_string_or(*frame, "name", "").find("Done") != std::string::npos) {
                        saw_done = true;
                    }
                }
            }
        }
        check(saw_done, "stackTrace shows agent paused in Done state");

        // The workflow must not have terminated while paused.
        {
            std::lock_guard lock(frames_mutex);
            check(!frame_has_event(frames, "terminated"),
                  "no terminated event while breakpoint is active");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 8: disconnect while paused unblocks the worker cleanly
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before disconnect");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted on disconnect");
    }

    // Test 9: capability breakpoint pauses execution and emits stopped
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::cap_workflow::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on capability breakpoint hit");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_breakpoint_reason = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"stopped")") != std::string::npos &&
                    frame.find(R"("reason":"breakpoint")") != std::string::npos) {
                    saw_breakpoint_reason = true;
                }
            }
            check(saw_breakpoint_reason,
                  "stopped event on capability breakpoint carries reason breakpoint");
            check(!frame_has_event(frames, "terminated"),
                  "no terminated event while capability breakpoint is active");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 10: capability breakpoint on a different capability does not stop
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::cap_workflow::OtherWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        // The workflow calls DoWork, not OtherWork: it must run to completion
        // without ever stopping.
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted when capability breakpoint misses");

        {
            std::lock_guard lock(frames_mutex);
            check(!frame_has_event(frames, "stopped"),
                  "no stopped event when capability breakpoint does not match");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 11: line breakpoint verification reflects IR-mappable lines
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        // Wait for the workflow to finish so the breakable line set is
        // definitely registered before setBreakpoints arrives.
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted before line breakpoint verification");

        const int done_line = find_source_line(kSimpleWorkflowSource, "state Done");
        const int empty_line = 2; // blank line in the fixture, no IR declaration
        check(done_line > 0, "found state Done line in fixture");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {done_line, empty_line});
        const auto bp_resp = server.handle_request(bp_req);

        const std::string verified_true =
            R"("verified":true,"line":)" + std::to_string(done_line);
        const std::string verified_false =
            R"("verified":false,"line":)" + std::to_string(empty_line);
        check(bp_resp.body.find(verified_true) != std::string::npos,
              "line breakpoint on IR-mapped state handler verifies as true");
        check(bp_resp.body.find(verified_false) != std::string::npos,
              "line breakpoint on empty line verifies as false");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 12: line breakpoint on a state handler line pauses execution
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        const int done_line = find_source_line(kSimpleWorkflowSource, "state Done");
        check(done_line > 0, "found state Done line in fixture");

        // Set the line breakpoint before launch: it is stored (enabled) even
        // though verification happens later once the IR is compiled.
        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {done_line});
        (void)server.handle_request(bp_req);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on line breakpoint hit");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_line_breakpoint = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"stopped")") != std::string::npos &&
                    frame.find("Line breakpoint") != std::string::npos) {
                    saw_line_breakpoint = true;
                }
            }
            check(saw_line_breakpoint,
                  "stopped event on line breakpoint carries line breakpoint description");
            check(!frame_has_event(frames, "terminated"),
                  "no terminated event while line breakpoint is active");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue from line breakpoint");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 13: next (step over) pauses at the next state transition with reason "step"
    {
        const auto fixture = write_stepper_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Init";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow", std::string(kStepperInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on Init state breakpoint");

        const std::size_t frames_before_step = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();

        ahfl::dap::DapMessage next_req;
        next_req.command = "next";
        (void)server.handle_request(next_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "step", std::chrono::seconds(5),
                                      frames_before_step),
              "stopped event with reason step emitted after next");

        // The step must land on Middle, the transition after Init.
        const auto stack_body = dap_command(server, "stackTrace");
        const auto stack_parsed = ahfl::json::parse_json(stack_body);
        bool saw_middle = false;
        if (stack_parsed.has_value() && *stack_parsed && (*stack_parsed)->is_object()) {
            const auto *stack_frames = (*stack_parsed)->get("stackFrames");
            if (stack_frames != nullptr && stack_frames->is_array()) {
                for (const auto &frame : stack_frames->array_items) {
                    if (frame && frame->is_object() &&
                        json_string_or(*frame, "name", "").find("Middle") != std::string::npos) {
                        saw_middle = true;
                    }
                }
            }
        }
        check(saw_middle, "stackTrace shows agent paused in Middle after step over");

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue from step");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 14: stepIn pauses at the next state transition with reason "step"
    // (identical to step over until capability step-into lands)
    {
        const auto fixture = write_stepper_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Init";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow", std::string(kStepperInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on Init state breakpoint before stepIn");

        const std::size_t frames_before_step = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();

        ahfl::dap::DapMessage step_in_req;
        step_in_req.command = "stepIn";
        (void)server.handle_request(step_in_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "step", std::chrono::seconds(5),
                                      frames_before_step),
              "stopped event with reason step emitted after stepIn");

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue from stepIn");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 15: without a pending step, state transitions never emit step pauses
    {
        const auto fixture = write_stepper_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow", std::string(kStepperInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted for unbroken run");

        {
            std::lock_guard lock(frames_mutex);
            check(!frame_has_event(frames, "stopped"),
                  "no stopped event without breakpoints or pending steps");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 16: stackTrace returns real frames with source locations
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before stackTrace");

        const auto stack_body = dap_command(server, "stackTrace");
        const auto stack_parsed = ahfl::json::parse_json(stack_body);
        const bool parsed_ok =
            stack_parsed.has_value() && *stack_parsed && (*stack_parsed)->is_object();
        check(parsed_ok, "stackTrace response parses as JSON object");

        if (parsed_ok) {
            const auto &stack = **stack_parsed;
            check(json_int_or(stack, "totalFrames", 0) >= 2,
                  "stackTrace reports at least two frames");

            const auto *top = find_frame(stack, 1);
            check(top != nullptr, "stackTrace has a top frame with id 1");
            if (top != nullptr) {
                check(json_string_or(*top, "name", "").find("Done") != std::string::npos,
                      "top frame names the Done state");
                const auto *source = top->get("source");
                check(source != nullptr && source->is_object(), "top frame carries a source");
                if (source != nullptr && source->is_object()) {
                    check(json_string_or(*source, "path", "") == fixture,
                          "top frame source path matches the fixture");
                }
                const int done_line = find_source_line(kSimpleWorkflowSource, "state Done");
                check(json_int_or(*top, "line", 0) == done_line,
                      "top frame line matches the Done handler line");
            }
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after stackTrace test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 17: scopes returns the RFC 0015 scope set with deterministic references
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before scopes");

        const auto scopes_body = dap_command(server, "scopes", R"({"frameId":1})");
        const auto scopes_parsed = ahfl::json::parse_json(scopes_body);
        const bool parsed_ok =
            scopes_parsed.has_value() && *scopes_parsed && (*scopes_parsed)->is_object();
        check(parsed_ok, "scopes response parses as JSON object");

        if (parsed_ok) {
            const auto &scopes = **scopes_parsed;
            check(find_scope(scopes, "Agent State") != nullptr, "scopes include Agent State");
            check(find_scope(scopes, "Context") != nullptr, "scopes include Context");
            check(find_scope(scopes, "Input/Output") != nullptr, "scopes include Input/Output");
            check(find_scope(scopes, "Workflow") != nullptr, "scopes include Workflow");

            const auto *agent_state = find_scope(scopes, "Agent State");
            const auto *context = find_scope(scopes, "Context");
            const auto *input_output = find_scope(scopes, "Input/Output");
            const auto *workflow = find_scope(scopes, "Workflow");
            check(agent_state != nullptr &&
                      json_int_or(*agent_state, "variablesReference", 0) == 101,
                  "Agent State scope uses variablesReference 101");
            check(context != nullptr && json_int_or(*context, "variablesReference", 0) == 201,
                  "Context scope uses variablesReference 201");
            check(input_output != nullptr &&
                      json_int_or(*input_output, "variablesReference", 0) == 301,
                  "Input/Output scope uses variablesReference 301");
            check(workflow != nullptr && json_int_or(*workflow, "variablesReference", 0) == 400,
                  "Workflow scope uses variablesReference 400");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after scopes test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 18: variables expands the live agent input as a structured value
    {
        const auto fixture = write_vars_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before variables");

        const auto vars_body = dap_command(server, "variables", R"({"variablesReference":301})");
        const auto vars_parsed = ahfl::json::parse_json(vars_body);
        const bool parsed_ok =
            vars_parsed.has_value() && *vars_parsed && (*vars_parsed)->is_object();
        check(parsed_ok, "variables(301) response parses as JSON object");

        if (parsed_ok) {
            const auto &vars = **vars_parsed;
            const auto *input = find_variable(vars, "input");
            check(input != nullptr, "Input/Output scope exposes the agent input");
            if (input != nullptr) {
                check(json_string_or(*input, "value", "").find("hello") != std::string::npos,
                      "input value carries the live structured data");
                check(json_int_or(*input, "variablesReference", 0) > 0,
                      "structured input gets a chained variablesReference");
            }
        }

        // The Done breakpoint hits once per node; resume past the first node
        // and then past the second node's Done pause before termination.
        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();
        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "second stopped event at the second node Done state");
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after variables test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 19: nested struct values expand through chained variablesReference
    {
        const auto fixture = write_vars_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before nested expansion");

        // First expansion: the agent input struct registers a chained reference.
        const auto input_vars_body =
            dap_command(server, "variables", R"({"variablesReference":301})");
        const auto input_vars = ahfl::json::parse_json(input_vars_body);
        int input_ref = 0;
        if (input_vars.has_value() && *input_vars && (*input_vars)->is_object()) {
            if (const auto *input = find_variable(**input_vars, "input"); input != nullptr) {
                input_ref = json_int_or(*input, "variablesReference", 0);
            }
        }
        check(input_ref >= 1000, "input struct registered a chained variablesReference");

        // Second expansion: the Data struct fields.
        int inner_ref = 0;
        if (input_ref >= 1000) {
            const auto data_body = dap_command(
                server, "variables",
                R"({"variablesReference":)" + std::to_string(input_ref) + "}");
            const auto data_vars = ahfl::json::parse_json(data_body);
            if (data_vars.has_value() && *data_vars && (*data_vars)->is_object()) {
                const auto &data = **data_vars;
                const auto *inner = find_variable(data, "inner");
                const auto *tag = find_variable(data, "tag");
                check(inner != nullptr, "Data struct exposes inner field");
                check(tag != nullptr, "Data struct exposes tag field");
                if (inner != nullptr) {
                    inner_ref = json_int_or(*inner, "variablesReference", 0);
                    check(inner_ref > 0, "inner struct gets its own variablesReference");
                }
                if (tag != nullptr) {
                    check(json_string_or(*tag, "value", "").find("hello") != std::string::npos,
                          "tag scalar value is the live string");
                    check(json_int_or(*tag, "variablesReference", -1) == 0,
                          "scalar tag has no chained reference");
                }
            } else {
                check(false, "variables(input_ref) response parses");
            }
        }

        // Third expansion: the Inner struct field reaches the leaf Int.
        if (inner_ref > 0) {
            const auto inner_body = dap_command(
                server, "variables",
                R"({"variablesReference":)" + std::to_string(inner_ref) + "}");
            const auto inner_vars = ahfl::json::parse_json(inner_body);
            if (inner_vars.has_value() && *inner_vars && (*inner_vars)->is_object()) {
                if (const auto *x = find_variable(**inner_vars, "x"); x != nullptr) {
                    check(json_string_or(*x, "value", "") == "42",
                          "nested x field expands to the live Int value");
                } else {
                    check(false, "Inner struct exposes x field");
                }
            } else {
                check(false, "variables(inner_ref) response parses");
            }
        }

        // The Done breakpoint hits once per node; resume past both pauses.
        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();
        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "second stopped event at the second node Done state");
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after nested expansion test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 20: a capability breakpoint pauses with the capability frame on top
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::cap_workflow::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on capability breakpoint");

        const auto stack_body = dap_command(server, "stackTrace");
        const auto stack_parsed = ahfl::json::parse_json(stack_body);
        if (stack_parsed.has_value() && *stack_parsed && (*stack_parsed)->is_object()) {
            if (const auto *top = find_frame(**stack_parsed, 1); top != nullptr) {
                check(json_string_or(*top, "name", "").find("DoWork") != std::string::npos,
                      "top frame names the invoked capability");
            } else {
                check(false, "stackTrace has a top frame at capability breakpoint");
            }
        } else {
            check(false, "stackTrace response parses at capability breakpoint");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after capability frame test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 21: the Workflow scope exposes completed node results
    {
        const auto fixture = write_vars_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
        (void)server.handle_request(launch_req);

        // The Done breakpoint hits once per node: pause at the first node.
        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "first stopped event at the first node Done state");

        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        // Pause again at the second node's Done state.
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "second stopped event at the second node Done state");

        // The first node has completed by now, so its result is in the Workflow scope.
        const auto vars_body = dap_command(server, "variables", R"({"variablesReference":400})");
        const auto vars_parsed = ahfl::json::parse_json(vars_body);
        if (vars_parsed.has_value() && *vars_parsed && (*vars_parsed)->is_object()) {
            if (const auto *first = find_variable(**vars_parsed, "first"); first != nullptr) {
                check(json_string_or(*first, "value", "").find("hello") != std::string::npos,
                      "Workflow scope carries the first node result value");
            } else {
                check(false, "Workflow scope exposes the first node result");
            }
        } else {
            check(false, "variables(400) response parses at the second node stop");
        }

        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after node results test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 22: evaluate resolves value paths against the paused context
    {
        const auto fixture = write_vars_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before evaluate");

        // input.tag: a field access on the live agent input struct.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("input.tag"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok, "evaluate(input.tag) parses as JSON object");
            if (ok) {
                check(json_string_or(**parsed, "result", "").find("hello") != std::string::npos,
                      "evaluate(input.tag) returns the live field value");
                check((*parsed)->get("error") == nullptr,
                      "evaluate(input.tag) has no error");
            }
        }

        // Unqualified field: the runtime flattens input struct fields into the
        // input scope, so `tag` names the same value as `input.tag`.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("tag"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && json_string_or(**parsed, "result", "").find("hello") != std::string::npos,
                  "evaluate(tag) resolves an unqualified input field");
        }

        // Nested field chain reaches the leaf Int and yields a scalar (no
        // chained reference).
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("input.inner.x"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && json_string_or(**parsed, "result", "") == "42",
                  "evaluate(input.inner.x) walks the nested field chain");
            if (ok) {
                check(json_int_or(**parsed, "variablesReference", -1) == 0,
                      "scalar evaluate result has no chained reference");
            }
        }

        // A structured result registers a chained variablesReference.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("input.inner"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && json_int_or(**parsed, "variablesReference", 0) >= 1000,
                  "structured evaluate result gets a chained variablesReference");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "second stopped event after evaluate test");
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after evaluate test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    // Test 23: evaluate reports errors for unresolvable and malformed input
    {
        const auto fixture = write_vars_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::State;
        bp.condition = "Done";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted before evaluate error checks");

        // Unknown field on a struct: error.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("input.missing"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && (*parsed)->get("error") != nullptr,
                  "evaluate(input.missing) returns an error");
        }

        // Unknown root identifier: error.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("nope"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && (*parsed)->get("error") != nullptr,
                  "evaluate(nope) returns an error for an unknown identifier");
        }

        // Non-path expression (call syntax): error, not a crash.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config("foo()"));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && (*parsed)->get("error") != nullptr,
                  "evaluate(foo()) rejects unsupported expression syntax");
        }

        // Empty expression: error.
        {
            const auto body = dap_command(server, "evaluate", evaluate_config(""));
            const auto parsed = ahfl::json::parse_json(body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            check(ok && (*parsed)->get("error") != nullptr,
                  "evaluate(\"\") rejects an empty expression");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "second stopped event after evaluate error test");
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after evaluate error test");

        ahfl::dap::DapMessage disc_req2;
        disc_req2.command = "disconnect";
        (void)server.handle_request(disc_req2);
    }

    } // end if constexpr (kBackendWasm) — Tests 6-23

    // Test 24: evaluate with no active session returns an error
    {
        ahfl::dap::DapServer server;
        const auto body = dap_command(server, "evaluate", evaluate_config("input.tag"));
        const auto parsed = ahfl::json::parse_json(body);
        const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
        check(ok && (*parsed)->get("error") != nullptr,
              "evaluate with no active session returns an error");
    }

    // Tests 25-32 + T1-T4: execution-dependent (launch/run/continue/
    // disconnect). Gated under WASM=OFF (F3).
    if constexpr (kBackendWasm) {

    // Test 25: a successful capability call emits an `output` event on the
    // "stdout" category carrying the serialized result value (RFC 0015 Slice 7).
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        // The workflow calls DoWork(); the debug session's stub invoker
        // succeeds with a None value, so an `output` event on "stdout" must be
        // emitted before the workflow terminates.
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after capability output test");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stdout_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stdout")") != std::string::npos) {
                    saw_stdout_output = true;
                }
            }
            check(saw_stdout_output,
                  "successful capability call emits an output event on stdout");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 26: a launch config missing required fields emits an `output` event
    // on the "stderr" category (RFC 0015 Slice 7).
    {
        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        // Empty launch config: no "program" / "workflow" fields.
        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = "{}";
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after invalid launch config");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stderr_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stderr")") != std::string::npos) {
                    saw_stderr_output = true;
                }
            }
            check(saw_stderr_output,
                  "invalid launch config emits an output event on stderr");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 27: a launch config with "program" but no "workflow" defaults to
    // the first workflow declaration in the compiled program (RFC 0015
    // Slice 8: the VS Code contribution documents "workflow" as optional).
    {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        auto body = ahfl::json::JsonValue::make_object();
        body->set("program", ahfl::json::JsonValue::make_string(fixture));
        auto input_parsed = ahfl::json::parse_json(std::string(kSimpleInputJson));
        if (input_parsed.has_value() && *input_parsed) {
            body->set("input", std::move(*input_parsed));
        }
        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = ahfl::json::serialize_json(*body);
        const auto resp = server.handle_request(launch_req);
        check(resp.command == "launch", "launch response command matches");

        check(wait_for_event(frames, frames_mutex, "initialized", std::chrono::seconds(5)),
              "initialized event emitted after launch without workflow field");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after default workflow completes");

        // No stderr output event should have been emitted for a missing
        // workflow field.
        {
            std::lock_guard lock(frames_mutex);
            bool saw_stderr_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stderr")") != std::string::npos) {
                    saw_stderr_output = true;
                }
            }
            check(!saw_stderr_output,
                  "no stderr output when workflow field is omitted");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 28 (WH-8 new test 1): a capability breakpoint pauses LIVE — the
    // capability has not been invoked yet at pause time (no stdout output
    // event), and the stack carries a Capability frame on top.
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::cap_workflow::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
              "stopped event emitted on LIVE capability breakpoint");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stdout_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stdout")") != std::string::npos) {
                    saw_stdout_output = true;
                }
            }
            check(!saw_stdout_output,
                  "no capability output event at LIVE pause (capability not yet invoked)");
        }

        const auto stack_body = dap_command(server, "stackTrace");
        const auto stack_parsed = ahfl::json::parse_json(stack_body);
        if (stack_parsed.has_value() && *stack_parsed && (*stack_parsed)->is_object()) {
            if (const auto *top = find_frame(**stack_parsed, 1); top != nullptr) {
                check(json_string_or(*top, "name", "").find("DoWork") != std::string::npos,
                      "LIVE capability pause shows Capability frame on top");
            } else {
                check(false, "stackTrace has a top frame at LIVE capability pause");
            }
        } else {
            check(false, "stackTrace response parses at LIVE capability pause");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated after continue from LIVE capability pause");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 29 (WH-8 new test 2): state breakpoint liveness classification.
    // The deferred-binding unsolicited "breakpoint" event carries the
    // descriptor-refined verified flag and message per the 7-row liveness
    // table (kr68 section 12.9.14.4).
    {
        // Part A: P6 + has_capability + walk-index <= last_cap_walk_index
        // → LIVE (verified:true + live pause message).
        {
            const auto fixture = write_capability_workflow_fixture();

            ahfl::dap::DapServer server;
            std::vector<std::string> frames;
            std::mutex frames_mutex;
            server.set_event_output([&](std::string_view framed) {
                std::lock_guard lock(frames_mutex);
                frames.push_back(std::string(framed));
            });

            const int init_line = find_source_line(kCapabilityWorkflowSource, "state Init");
            check(init_line > 0, "found state Init line in capability fixture");

            ahfl::dap::DapMessage bp_req;
            bp_req.command = "setBreakpoints";
            bp_req.body = set_breakpoints_config(fixture, {init_line});
            (void)server.handle_request(bp_req);

            ahfl::dap::DapMessage launch_req;
            launch_req.command = "launch";
            launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
            (void)server.handle_request(launch_req);

            check(wait_for_event(frames, frames_mutex, "initialized", std::chrono::seconds(5)),
                  "initialized after launch (classification part A)");

            {
                std::lock_guard lock(frames_mutex);
                check(frame_has_breakpoint_event(frames, true, "live pause"),
                      "P6 has-cap state breakpoint classified as LIVE");
            }

            // The LIVE breakpoint pauses at Init; wait for the stopped
            // event, then continue to completion.
            check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
                  "stopped at LIVE state breakpoint (classification part A)");
            ahfl::dap::DapMessage cont_req;
            cont_req.command = "continue";
            (void)server.handle_request(cont_req);
            check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
                  "terminated after continue from LIVE state breakpoint");

            ahfl::dap::DapMessage disc_req;
            disc_req.command = "disconnect";
            (void)server.handle_request(disc_req);
        }

        // Part B: P6 + no capability → POST-MORTEM (verified:false +
        // no-capability-boundary message).
        {
            const auto fixture = write_simple_workflow_fixture();

            ahfl::dap::DapServer server;
            std::vector<std::string> frames;
            std::mutex frames_mutex;
            server.set_event_output([&](std::string_view framed) {
                std::lock_guard lock(frames_mutex);
                frames.push_back(std::string(framed));
            });

            const int done_line = find_source_line(kSimpleWorkflowSource, "state Done");
            check(done_line > 0, "found state Done line in simple fixture");

            ahfl::dap::DapMessage bp_req;
            bp_req.command = "setBreakpoints";
            bp_req.body = set_breakpoints_config(fixture, {done_line});
            (void)server.handle_request(bp_req);

            ahfl::dap::DapMessage launch_req;
            launch_req.command = "launch";
            launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
            (void)server.handle_request(launch_req);

            check(wait_for_event(frames, frames_mutex, "initialized", std::chrono::seconds(5)),
                  "initialized after launch (classification part B)");

            {
                std::lock_guard lock(frames_mutex);
                check(frame_has_breakpoint_event(frames, false, "no capability boundary"),
                      "P6 no-cap state breakpoint classified as POST-MORTEM");
            }

            // The POST-MORTEM breakpoint pauses after the run completes;
            // continue to let the session finish.
            check(wait_for_event(frames, frames_mutex, "stopped", std::chrono::seconds(5)),
                  "stopped event at POST-MORTEM state breakpoint (part B)");
            ahfl::dap::DapMessage cont_req;
            cont_req.command = "continue";
            (void)server.handle_request(cont_req);
            check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
                  "terminated after continue from POST-MORTEM breakpoint (part B)");

            ahfl::dap::DapMessage disc_req;
            disc_req.command = "disconnect";
            (void)server.handle_request(disc_req);
        }

        // Part C: WireJson lane → POST-MORTEM (verified:false + WireJson
        // lane message). The vars workflow uses an identity-final agent
        // with no bridge calls, so the codegen assigns WireJson.
        {
            const auto fixture = write_vars_workflow_fixture();

            ahfl::dap::DapServer server;
            std::vector<std::string> frames;
            std::mutex frames_mutex;
            server.set_event_output([&](std::string_view framed) {
                std::lock_guard lock(frames_mutex);
                frames.push_back(std::string(framed));
            });

            const int done_line = find_source_line(kVarsWorkflowSource, "state Done");
            check(done_line > 0, "found state Done line in vars fixture");

            ahfl::dap::DapMessage bp_req;
            bp_req.command = "setBreakpoints";
            bp_req.body = set_breakpoints_config(fixture, {done_line});
            (void)server.handle_request(bp_req);

            ahfl::dap::DapMessage launch_req;
            launch_req.command = "launch";
            launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow", std::string(kVarsInputJson));
            (void)server.handle_request(launch_req);

            check(wait_for_event(frames, frames_mutex, "initialized", std::chrono::seconds(5)),
                  "initialized after launch (classification part C)");

            {
                std::lock_guard lock(frames_mutex);
                check(frame_has_breakpoint_event(frames, false, "WireJson"),
                      "WireJson-lane state breakpoint classified as POST-MORTEM");
            }

            // The POST-MORTEM breakpoint pauses after the run completes
            // (twice — once per node); continue until terminated. Track
            // the frame offset so the second wait only sees NEW stopped
            // events.
            std::size_t frame_offset = 0;
            for (int i = 0; i < 2; ++i) {
                check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                              std::chrono::seconds(5), frame_offset),
                      "stopped event at POST-MORTEM state breakpoint (part C)");
                {
                    std::lock_guard lock(frames_mutex);
                    frame_offset = frames.size();
                }
                ahfl::dap::DapMessage cont_req;
                cont_req.command = "continue";
                (void)server.handle_request(cont_req);
            }
            check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
                  "terminated after POST-MORTEM breakpoint continues (part C)");

            ahfl::dap::DapMessage disc_req;
            disc_req.command = "disconnect";
            (void)server.handle_request(disc_req);
        }
    }

    // Test 30 (WH-8 new test 3): cancellation — disconnect during a run
    // joins the worker within bounded time. The capability workflow checks
    // cancellation at the import boundary; the no-import workflow runs to
    // completion. Both must not hang.
    {
        // Capability workflow: cancellation checked at the import boundary.
        {
            const auto fixture = write_capability_workflow_fixture();

            ahfl::dap::DapServer server;
            std::vector<std::string> frames;
            std::mutex frames_mutex;
            server.set_event_output([&](std::string_view framed) {
                std::lock_guard lock(frames_mutex);
                frames.push_back(std::string(framed));
            });

            ahfl::dap::DapMessage launch_req;
            launch_req.command = "launch";
            launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
            (void)server.handle_request(launch_req);

            const auto start = std::chrono::steady_clock::now();
            ahfl::dap::DapMessage disc_req;
            disc_req.command = "disconnect";
            (void)server.handle_request(disc_req);
            const auto elapsed = std::chrono::steady_clock::now() - start;

            check(elapsed < std::chrono::seconds(5),
                  "disconnect during capability workflow run joins within bounded time");
            check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
                  "terminated after cancellation disconnect (capability workflow)");
        }

        // No-import workflow: runs to completion, then joins.
        {
            const auto fixture = write_simple_workflow_fixture();

            ahfl::dap::DapServer server;
            std::vector<std::string> frames;
            std::mutex frames_mutex;
            server.set_event_output([&](std::string_view framed) {
                std::lock_guard lock(frames_mutex);
                frames.push_back(std::string(framed));
            });

            ahfl::dap::DapMessage launch_req;
            launch_req.command = "launch";
            launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
            (void)server.handle_request(launch_req);

            const auto start = std::chrono::steady_clock::now();
            ahfl::dap::DapMessage disc_req;
            disc_req.command = "disconnect";
            (void)server.handle_request(disc_req);
            const auto elapsed = std::chrono::steady_clock::now() - start;

            check(elapsed < std::chrono::seconds(5),
                  "disconnect during no-import workflow run joins within bounded time");
            check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
                  "terminated after cancellation disconnect (no-import workflow)");
        }
    }

    // Test 31 (WH-8 new test 6): sibling codegen failure — a program with
    // a good workflow and a sibling that fails wasm codegen must surface
    // the failure as a launch error naming the failing workflow.
    {
        const auto fixture = write_sibling_fail_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::sibling_fail::GoodWorkflow", std::string(kSiblingFailInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated after sibling codegen failure");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stderr = false;
            bool mentions_bad = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stderr")") != std::string::npos) {
                    saw_stderr = true;
                    if (frame.find("BadWorkflow") != std::string::npos) {
                        mentions_bad = true;
                    }
                }
            }
            check(saw_stderr,
                  "sibling codegen failure emits stderr output event");
            check(mentions_bad,
                  "sibling codegen failure diagnostic names the failing workflow");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 32 (WH-8 new tests 8+9): live state pause + dedup. A line
    // breakpoint on the Init state of the capability workflow fires LIVE
    // at the import boundary (before DoWork is dispatched). The stopped
    // event carries reason "breakpoint" (not "pause"). At pause time no
    // capability output event exists. After continue, exactly one stopped
    // event is recorded — the post-run hook dedup-skips the occurrence
    // already fired live.
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        const int init_line = find_source_line(kCapabilityWorkflowSource, "state Init");
        check(init_line > 0, "found state Init line for live state pause test");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {init_line});
        (void)server.handle_request(bp_req);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
                                      std::chrono::seconds(5)),
              "live state pause fires stopped with reason breakpoint");

        {
            std::lock_guard lock(frames_mutex);
            // No capability output at pause: the live hook fires before
            // the capability is dispatched.
            bool saw_stdout_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stdout")") != std::string::npos) {
                    saw_stdout_output = true;
                }
            }
            check(!saw_stdout_output,
                  "no capability output at live state pause (cap not yet called)");
            check(count_stopped_events(frames) == 1,
                  "exactly one stopped event at live state pause (dedup)");
        }

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated after continue from live state pause");

        {
            std::lock_guard lock(frames_mutex);
            check(count_stopped_events(frames) == 1,
                  "exactly one stopped event after run (post-run dedup)");
        }

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // T1 (F1a): cancellation after a blocking LIVE state pause. A line
    // breakpoint on Init (LIVE — before the capability boundary) pauses the
    // worker inside the state_entered_live_hook. Disconnect while paused;
    // the F1(a) second check (after the has_trace block) must catch the
    // cancellation and abort before DoWork is dispatched. Assert no
    // capability-result stdout output event was ever emitted.
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        const int init_line = find_source_line(kCapabilityWorkflowSource, "state Init");
        check(init_line > 0, "T1: found state Init line");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {init_line});
        (void)server.handle_request(bp_req);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
                                      std::chrono::seconds(5)),
              "T1: stopped at live Init line breakpoint");

        const auto start = std::chrono::steady_clock::now();
        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        check(elapsed < std::chrono::seconds(5),
              "T1: disconnect joins within bounded time");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T1: terminated after disconnect");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stdout_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stdout")") != std::string::npos) {
                    saw_stdout_output = true;
                }
            }
            check(!saw_stdout_output,
                  "T1: no capability-result stdout output ever emitted (DoWork never ran)");
        }
    }

    // T2 (F1b): cancellation after a blocking capability pause. A capability
    // breakpoint on DoWork pauses the worker inside the
    // capability_invoked_hook. Disconnect while paused; the F1(b) check
    // (after the hook, before the invoker) must catch the cancellation and
    // return a Cancelled Error result without calling the invoker. Assert no
    // capability-result stdout output event was ever emitted.
    {
        const auto fixture = write_capability_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::cap_workflow::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow", std::string(kCapabilityInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
                                      std::chrono::seconds(5)),
              "T2: stopped at capability breakpoint");

        const auto start = std::chrono::steady_clock::now();
        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        check(elapsed < std::chrono::seconds(5),
              "T2: disconnect joins within bounded time");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T2: terminated after disconnect");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_stdout_output = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"output")") != std::string::npos &&
                    frame.find(R"("category":"stdout")") != std::string::npos) {
                    saw_stdout_output = true;
                }
            }
            check(!saw_stdout_output,
                  "T2: no capability-result stdout output ever emitted (DoWork never ran)");
        }
    }

    // T3 (F2): per-node interleaving of node_completed then state hooks.
    // Two nodes on ONE identity-final agent (caps [], WireJson lane) with
    // distinguishable literal inputs (x:1 vs x:2). A line breakpoint on the
    // Done state line fires post-mortem for each node. At the FIRST stopped,
    // the stack carries node `first` and evaluate("output") returns x == 1;
    // after continue, the second stopped carries node `second` and output
    // x == 2. This fails on the old batched code (last-wins) and passes
    // after F2.
    {
        const auto fixture = write_two_node_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        const int done_line = find_source_line(kTwoNodeWorkflowSource, "state Done");
        check(done_line > 0, "T3: found state Done line");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {done_line});
        (void)server.handle_request(bp_req);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::two_node_workflow::TwoNodeWorkflow", std::string(kTwoNodeInputJson));
        (void)server.handle_request(launch_req);

        // First stopped: node `first` (post-mortem, reason "pause").
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5)),
              "T3: first stopped at node first Done state");

        // Stack trace: a frame carries node name "first".
        {
            const auto stack_body = dap_command(server, "stackTrace");
            const auto parsed = ahfl::json::parse_json(stack_body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            bool found_first = false;
            if (ok) {
                const auto *sfs = (*parsed)->get("stackFrames");
                if (sfs != nullptr && sfs->is_array()) {
                    for (const auto &sf : sfs->array_items) {
                        if (!sf || !sf->is_object()) continue;
                        if (json_string_or(*sf, "name", "") == "first") {
                            found_first = true;
                        }
                    }
                }
            }
            check(found_first, "T3: stack carries node frame 'first' at first stopped");
        }

        // evaluate("output") -> x == 1.
        {
            const auto eval_body = dap_command(server, "evaluate", evaluate_config("output"));
            const auto parsed = ahfl::json::parse_json(eval_body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            bool x_is_1 = false;
            if (ok) {
                const auto *result = (*parsed)->get("result");
                if (result != nullptr) {
                    const auto result_str = result->as_string();
                    if (result_str.has_value()) {
                        const auto inner = ahfl::json::parse_json(*result_str);
                        if (inner.has_value() && *inner && (*inner)->is_object()) {
                            const auto *x = (*inner)->get("x");
                            if (x != nullptr) {
                                const auto x_val = x->as_int();
                                x_is_1 = x_val.has_value() && *x_val == 1;
                            }
                        }
                    }
                }
            }
            check(x_is_1, "T3: evaluate(output) x == 1 at first stopped");
        }

        // Continue -> second stopped: node `second`.
        const std::size_t frames_before_continue = [&] {
            std::lock_guard lock(frames_mutex);
            return frames.size();
        }();
        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5), frames_before_continue),
              "T3: second stopped at node second Done state");

        // Stack trace: a frame carries node name "second".
        {
            const auto stack_body = dap_command(server, "stackTrace");
            const auto parsed = ahfl::json::parse_json(stack_body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            bool found_second = false;
            if (ok) {
                const auto *sfs = (*parsed)->get("stackFrames");
                if (sfs != nullptr && sfs->is_array()) {
                    for (const auto &sf : sfs->array_items) {
                        if (!sf || !sf->is_object()) continue;
                        if (json_string_or(*sf, "name", "") == "second") {
                            found_second = true;
                        }
                    }
                }
            }
            check(found_second, "T3: stack carries node frame 'second' at second stopped");
        }

        // evaluate("output") -> x == 2.
        {
            const auto eval_body = dap_command(server, "evaluate", evaluate_config("output"));
            const auto parsed = ahfl::json::parse_json(eval_body);
            const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
            bool x_is_2 = false;
            if (ok) {
                const auto *result = (*parsed)->get("result");
                if (result != nullptr) {
                    const auto result_str = result->as_string();
                    if (result_str.has_value()) {
                        const auto inner = ahfl::json::parse_json(*result_str);
                        if (inner.has_value() && *inner && (*inner)->is_object()) {
                            const auto *x = (*inner)->get("x");
                            if (x != nullptr) {
                                const auto x_val = x->as_int();
                                x_is_2 = x_val.has_value() && *x_val == 2;
                            }
                        }
                    }
                }
            }
            check(x_is_2, "T3: evaluate(output) x == 2 at second stopped");
        }

        // Continue -> terminated.
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T3: terminated after second node pause");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // T4 (F4.3 + F5): two agents declaring the same state name (Done) on
    // different lines. A capability breakpoint pauses the run at DoWork;
    // while paused, setBreakpoints on AgentB's Done line (the SECOND
    // "state Done" line). The unsolicited breakpoint event must classify
    // the line correctly (NOT "not a state handler") — this exercises
    // F4.3's (agent, state) record reverse lookup and F5's late
    // classification.
    {
        const auto fixture = write_two_agent_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        // Capability breakpoint to pause the run.
        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::two_agent_workflow::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::two_agent_workflow::TwoAgentWorkflow", std::string(kTwoAgentInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
                                      std::chrono::seconds(5)),
              "T4: stopped at capability breakpoint");

        // While paused, setBreakpoints on AgentB's Done line (the second
        // "state Done" line in the source).
        const int agent_b_done_line = find_nth_source_line(kTwoAgentWorkflowSource, "state Done", 2);
        check(agent_b_done_line > 0, "T4: found second state Done line (AgentB)");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {agent_b_done_line});
        (void)server.handle_request(bp_req);

        // Wait for the unsolicited breakpoint event with the refined
        // classification. AgentB has no capabilities, so the P6-lane
        // classifier returns "state has no capability boundary in its
        // runner; pauses after run completes (post-mortem)".
        check(wait_for_event(frames, frames_mutex, "breakpoint", std::chrono::seconds(5)),
              "T4: unsolicited breakpoint event for late setBreakpoints");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_correct_classification = false;
            bool saw_not_state_handler = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"breakpoint")") != std::string::npos) {
                    if (frame.find("not a state handler") != std::string::npos) {
                        saw_not_state_handler = true;
                    }
                    if (frame.find("state has no capability boundary in its runner") != std::string::npos) {
                        saw_correct_classification = true;
                    }
                }
            }
            check(!saw_not_state_handler,
                  "T4: late setBreakpoints on AgentB line does NOT say 'not a state handler'");
            check(saw_correct_classification,
                  "T4: late setBreakpoints on AgentB line classified as no-capability post-mortem");
        }

        // Continue from the capability breakpoint. The workflow runs to
        // completion. The late line breakpoint on AgentB's Done line fires
        // post-mortem (the classification message above says "pauses after
        // run completes"), so the session pauses once more before
        // terminating.
        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);
        check(wait_for_stopped_reason(frames, frames_mutex, "pause",
                                      std::chrono::seconds(5)),
              "T4: post-mortem pause at AgentB Done line breakpoint");

        // Continue from the post-mortem pause; the session terminates.
        ahfl::dap::DapMessage cont_req2;
        cont_req2.command = "continue";
        (void)server.handle_request(cont_req2);
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T4: terminated after continue");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // T6a (F6.1 MIXED): two agents' Done handlers share ONE physical line.
    // AgentA has a capability (Done is LIVE on the P6 lane); AgentB has none
    // (no-capability post-mortem). A capability breakpoint pauses the run at
    // DoWork; while paused, late setBreakpoints on the shared line must
    // classify to the shared-line message (verified=false) instead of
    // attributing one arbitrary handler's liveness.
    {
        const auto fixture = write_shared_line_mixed_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        // Capability breakpoint to pause the run.
        ahfl::dap::Breakpoint bp;
        bp.kind = ahfl::dap::BreakpointKind::Capability;
        bp.condition = "dap::shared_line_mixed::DoWork";
        bp.enabled = true;
        (void)server.breakpoint_manager().add_breakpoint(bp);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::shared_line_mixed::SharedLineMixedWorkflow", std::string(kSharedLineMixedInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
                                      std::chrono::seconds(5)),
              "T6a: stopped at capability breakpoint");

        // The shared line: both flow declarations (and both Done handlers)
        // are on one physical line.
        const int shared_line = find_source_line(kSharedLineMixedWorkflowSource, "state Done");
        check(shared_line > 0, "T6a: found shared state Done line");

        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {shared_line});
        (void)server.handle_request(bp_req);

        // The unsolicited breakpoint event must be verified=false with the
        // shared-line message byte-exact.
        check(wait_for_event(frames, frames_mutex, "breakpoint", std::chrono::seconds(5)),
              "T6a: unsolicited breakpoint event for late setBreakpoints");

        {
            std::lock_guard lock(frames_mutex);
            constexpr std::string_view kSharedLineMessage =
                "line is shared by multiple state handlers with differing "
                "pause behavior; put each handler on its own line to bind "
                "a specific one";
            bool saw_shared_line = false;
            bool saw_live = false;
            bool saw_no_cap = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"breakpoint")") == std::string::npos) {
                    continue;
                }
                if (frame.find(kSharedLineMessage) != std::string::npos &&
                    frame.find(R"("verified":false)") != std::string::npos) {
                    saw_shared_line = true;
                }
                if (frame.find("state entered before capability boundary") !=
                    std::string::npos) {
                    saw_live = true;
                }
                if (frame.find("state has no capability boundary in its runner") !=
                    std::string::npos) {
                    saw_no_cap = true;
                }
            }
            check(saw_shared_line,
                  "T6a: shared line classified with the shared-line message (verified=false)");
            check(!saw_live,
                  "T6a: shared line does NOT attribute AgentA's live verdict");
            check(!saw_no_cap,
                  "T6a: shared line does NOT attribute AgentB's no-cap verdict");
        }

        // Continue and disconnect bounded (the line breakpoint may fire
        // post-mortem for AgentB's handlers; disconnect force-stops).
        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        const auto start = std::chrono::steady_clock::now();
        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(elapsed < std::chrono::seconds(5),
              "T6a: disconnect joins within bounded time");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T6a: terminated after disconnect");
    }

    // T6b (F6.1 UNANIMOUS): two no-cap agents (WireJson lane) with both
    // Done handlers on ONE physical line. Every record on the shared line
    // classifies to the same WireJson verdict, so the classifier collapses
    // them to that single verdict — the shared-line branch must NOT fire.
    {
        const auto fixture = write_shared_line_unanimous_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        const int shared_line = find_source_line(kSharedLineUnanimousWorkflowSource, "state Done");
        check(shared_line > 0, "T6b: found shared state Done line");

        // Pre-launch setBreakpoints: the launch-time
        // classify_and_report_breakpoints sends the unsolicited event once
        // the facade descriptor exists.
        ahfl::dap::DapMessage bp_req;
        bp_req.command = "setBreakpoints";
        bp_req.body = set_breakpoints_config(fixture, {shared_line});
        (void)server.handle_request(bp_req);

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::shared_line_unanimous::SharedLineUnanimousWorkflow", std::string(kSharedLineUnanimousInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "breakpoint", std::chrono::seconds(5)),
              "T6b: unsolicited breakpoint event for the shared line");

        {
            std::lock_guard lock(frames_mutex);
            constexpr std::string_view kWireJsonMessage =
                "state hooks are post-run on the WireJson lane (no trace "
                "ring); pauses after run completes (post-mortem)";
            bool saw_wire_json = false;
            bool saw_shared_line = false;
            for (const auto &frame : frames) {
                if (frame.find(R"("event":"breakpoint")") == std::string::npos) {
                    continue;
                }
                if (frame.find(kWireJsonMessage) != std::string::npos &&
                    frame.find(R"("verified":false)") != std::string::npos) {
                    saw_wire_json = true;
                }
                if (frame.find("line is shared by multiple") !=
                    std::string::npos) {
                    saw_shared_line = true;
                }
            }
            check(saw_wire_json,
                  "T6b: unanimous shared line collapses to the WireJson verdict");
            check(!saw_shared_line,
                  "T6b: unanimous shared line does NOT trip the shared-line branch");
        }

        // The WireJson lane runs to completion (post-mortem pauses only);
        // disconnect bounded and wait for terminated.
        const auto start = std::chrono::steady_clock::now();
        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(elapsed < std::chrono::seconds(5),
              "T6b: disconnect joins within bounded time");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T6b: terminated after disconnect");
    }

    } // end if constexpr (kBackendWasm) — Tests 25-32 + T1-T4 + T6a-T6b

    // T5 (F3): WASM=OFF launch refusal. Under OFF, the DebugSession launch
    // path refuses with a stderr "debug adapter requires WASM backend
    // support" message, then emits terminated. A subsequent threads request
    // still responds; disconnect works.
    if constexpr (!kBackendWasm) {
        const auto fixture = write_simple_workflow_fixture();

        ahfl::dap::DapServer server;
        std::vector<std::string> frames;
        std::mutex frames_mutex;
        server.set_event_output([&](std::string_view framed) {
            std::lock_guard lock(frames_mutex);
            frames.push_back(std::string(framed));
        });

        ahfl::dap::DapMessage launch_req;
        launch_req.command = "launch";
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow", std::string(kSimpleInputJson));
        (void)server.handle_request(launch_req);

        check(wait_for_event(frames, frames_mutex, "output", std::chrono::seconds(5)),
              "T5: output event emitted on OFF launch refusal");
        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "T5: terminated event emitted on OFF launch refusal");

        {
            std::lock_guard lock(frames_mutex);
            bool saw_refusal = false;
            for (const auto &frame : frames) {
                if (frame.find("debug adapter requires WASM backend support") != std::string::npos) {
                    saw_refusal = true;
                }
            }
            check(saw_refusal,
                  "T5: stderr output contains byte-pinned refusal text");
        }

        // A subsequent threads request still responds.
        const auto threads_body = dap_command(server, "threads");
        const auto parsed = ahfl::json::parse_json(threads_body);
        const bool ok = parsed.has_value() && *parsed && (*parsed)->is_object();
        check(ok && (*parsed)->get("threads") != nullptr,
              "T5: threads request responds after OFF launch refusal");

        // Disconnect works (does not crash).
        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
        check(true, "T5: disconnect after OFF launch refusal does not crash");
    }

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
