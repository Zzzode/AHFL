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

    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
