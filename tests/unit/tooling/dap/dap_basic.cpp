#include "tooling/dap/dap_server.hpp"
#include "tooling/dap/breakpoints.hpp"
#include "tooling/dap/state_inspector.hpp"
#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    test_count++;
    if (condition) {
        pass_count++;
    } else {
        std::printf("FAIL: %s\n", name);
    }
}

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
    
    std::printf("%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
