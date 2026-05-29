#include <cstdio>
#include <string>

#include "compiler/backends/infra/wasm_backend.hpp"
#include "compiler/backends/infra/wasm_runtime.hpp"

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("=== WASM Backend Tests ===\n\n");

    // Test 1: generate_wasm produces valid WAT with module header
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "test_agent";
        config.states = {"idle", "running", "done"};
        config.transitions = {{"idle", "running"}, {"running", "done"}};
        config.capabilities = {"http_call"};

        auto mod = ahfl::backends::generate_wasm(config);

        bool has_module = mod.wat_source.find("(module $test_agent") != std::string::npos;
        bool has_export = mod.wat_source.find("(export \"get_state\")") != std::string::npos;
        bool has_transition = mod.wat_source.find("(export \"transition\")") != std::string::npos;

        check(has_module && has_export && has_transition,
              "generate_wasm produces valid WAT with module header");
    }

    // Test 2: generate_wasi_imports produces import section for allowed capabilities
    {
        ahfl::backends::WasiConfig wasi_config;
        wasi_config.allowed_capabilities = {
            ahfl::backends::WasiCapability::FileRead,
            ahfl::backends::WasiCapability::NetworkAccess
        };

        auto imports = ahfl::backends::generate_wasi_imports(wasi_config);

        bool has_fd_read = imports.find("fd_read") != std::string::npos;
        bool has_sock = imports.find("sock_accept") != std::string::npos;
        bool has_wasi_import = imports.find("wasi_snapshot_preview1") != std::string::npos;

        check(has_fd_read && has_sock && has_wasi_import,
              "generate_wasi_imports produces import section for allowed capabilities");
    }

    // Test 3: generate_wasm with empty agent produces minimal module
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "empty_agent";

        auto mod = ahfl::backends::generate_wasm(config);

        bool has_module = mod.wat_source.find("(module $empty_agent") != std::string::npos;
        bool has_memory = mod.wat_source.find("(memory (export \"memory\") 1)") != std::string::npos;
        bool no_states = mod.wat_source.find("state table: 0 states") != std::string::npos;

        check(has_module && has_memory && no_states,
              "generate_wasm with empty agent produces minimal module");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
