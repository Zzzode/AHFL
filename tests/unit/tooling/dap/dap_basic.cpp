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

struct Empty {}

struct Greeting {
    message: String;
}

agent GreeterAgent {
    input: Empty;
    context: Empty;
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
    input: Empty;
    output: Greeting;

    node greet: GreeterAgent(Empty {});

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

struct Empty {}

struct Result {
    message: String;
}

capability DoWork() -> Unit;
capability OtherWork() -> Unit;

agent WorkerAgent {
    input: Empty;
    context: Empty;
    output: Result;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [DoWork, OtherWork];

    transition Init -> Done;
}

flow for WorkerAgent {
    state Init {
        let _ = DoWork();
        goto Done;
    }

    state Done {
        return Result {
            message: "done",
        };
    }
}

workflow WorkerWorkflow {
    input: Empty;
    output: Result;

    node worker: WorkerAgent(Empty {});

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

struct Empty {}

struct Ack {
    message: String;
}

agent StepperAgent {
    input: Empty;
    context: Empty;
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
    input: Empty;
    output: Ack;

    node stepper: StepperAgent(Empty {});

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

struct Empty {}

struct Inner {
    x: Int;
}

struct Data {
    inner: Inner;
    tag: String;
}

agent VarsAgent {
    input: Data;
    context: Empty;
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
        return Data {
            inner: input.inner,
            tag: input.tag,
        };
    }
}

workflow VarsWorkflow {
    input: Empty;
    output: Data;

    node first: VarsAgent(Data {
        inner: Inner { x: 42 },
        tag: "hello",
    });

    node second: VarsAgent(Data {
        inner: Inner { x: 7 },
        tag: "world",
    }) after [first];

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

[[nodiscard]] std::string launch_config(const std::string &program_path,
                                        const std::string &workflow_name) {
    auto body = ahfl::json::JsonValue::make_object();
    body->set("program", ahfl::json::JsonValue::make_string(program_path));
    body->set("workflow", ahfl::json::JsonValue::make_string(workflow_name));
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

} // namespace

int main() {
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::stepper_workflow::StepperWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::simple_workflow::GreetingWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow");
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
        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
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
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow");
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
        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
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
        launch_req.body = launch_config(fixture, "dap::cap_workflow::WorkerWorkflow");
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
        launch_req.body = launch_config(fixture, "dap::vars_workflow::VarsWorkflow");
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
        check(wait_for_stopped_reason(frames, frames_mutex, "breakpoint",
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

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
