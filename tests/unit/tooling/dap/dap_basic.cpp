#include "tooling/dap/dap_server.hpp"
#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/debug_session.hpp"
#include "tooling/dap/state_inspector.hpp"
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

    // Test 3: State inspector
    {
        ahfl::dap::StateInspector inspector;
        inspector.set_agent_state("myAgent", "Init", {{"key", "value"}});
        inspector.set_input("myAgent", {{"request", "hello"}});
        inspector.set_output("myAgent", {{"response", "world"}});

        auto scopes = inspector.get_scopes("myAgent");
        check(scopes.size() == 3, "three scopes: Input, Context, Output");

        auto frames = inspector.get_stack_frames();
        check(frames.size() == 1, "one stack frame");
        check(frames[0].state_name == "Init", "frame has correct state");
    }

    // Test 4: DAP disconnect
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

    // Test 5: send_event produces a Content-Length framed event message
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

    // Test 6: send_event without a sink is a no-op (does not crash)
    {
        ahfl::dap::DapServer server;
        server.send_event("terminated", ahfl::json::JsonValue::make_object());
        check(true, "send_event without sink does not crash");
    }

    // Test 7: DebugSession launch runs a workflow and emits terminated
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

    // Test 8: state breakpoint pauses execution and emits stopped
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

        // The inspector must reflect the paused state.
        const auto inspector_frames = server.state_inspector().get_stack_frames();
        bool saw_done = false;
        for (const auto &frame : inspector_frames) {
            if (frame.state_name == "Done") {
                saw_done = true;
            }
        }
        check(saw_done, "inspector shows agent paused in Done state");

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

    // Test 9: disconnect while paused unblocks the worker cleanly
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

    // Test 10: capability breakpoint pauses execution and emits stopped
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

    // Test 11: capability breakpoint on a different capability does not stop
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

    // Test 12: line breakpoint verification reflects IR-mappable lines
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

    // Test 13: line breakpoint on a state handler line pauses execution
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

    // Test 14: next (step over) pauses at the next state transition with reason "step"
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
        const auto inspector_frames = server.state_inspector().get_stack_frames();
        bool saw_middle = false;
        for (const auto &frame : inspector_frames) {
            if (frame.state_name == "Middle") {
                saw_middle = true;
            }
        }
        check(saw_middle, "inspector shows agent paused in Middle after step over");

        ahfl::dap::DapMessage cont_req;
        cont_req.command = "continue";
        (void)server.handle_request(cont_req);

        check(wait_for_event(frames, frames_mutex, "terminated", std::chrono::seconds(5)),
              "terminated event emitted after continue from step");

        ahfl::dap::DapMessage disc_req;
        disc_req.command = "disconnect";
        (void)server.handle_request(disc_req);
    }

    // Test 15: stepIn pauses at the next state transition with reason "step"
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

    // Test 16: without a pending step, state transitions never emit step pauses
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

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
