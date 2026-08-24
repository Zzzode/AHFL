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

    // Test 4 (RFC 0019 slice 1): the stable host ABI is emitted and exported.
    {
        ahfl::backends::WasmAgentConfig config;
        config.agent_name = "abi_agent";
        config.states = {"init", "done"};
        config.transitions = {{"init", "done"}};

        auto mod = ahfl::backends::generate_wasm(config);
        const auto &wat = mod.wat_source;

        bool has_version =
            wat.find("(global $ahfl_abi_version (export \"ahfl_abi_version\") i32") !=
            std::string::npos;
        bool has_alloc = wat.find("(func $alloc (export \"alloc\")") != std::string::npos;
        bool has_dealloc = wat.find("(func $dealloc (export \"dealloc\")") != std::string::npos;
        bool has_run = wat.find("(func $run (export \"run\")") != std::string::npos;
        bool has_step = wat.find("(func $step (export \"step\")") != std::string::npos;
        bool has_current =
            wat.find("(func $current_state (export \"current_state\")") != std::string::npos;

        check(has_version && has_alloc && has_dealloc && has_run && has_step && has_current,
              "generate_wasm emits the RFC 0019 stable host ABI");

        // The module's registered export list mirrors the emitted ABI.
        const auto has_export = [&](const std::string &name) {
            for (const auto &e : mod.exports) {
                if (e == name) {
                    return true;
                }
            }
            return false;
        };
        check(has_export("ahfl_abi_version") && has_export("alloc") && has_export("dealloc") &&
                  has_export("run") && has_export("step") && has_export("current_state"),
              "module export list registers the ABI symbols");
    }

    // Test 5 (RFC 0019 slice 1): the ABI contract catalogue is the single SoT.
    {
        auto abi = ahfl::backends::wasm_abi_exports();
        bool has_run = false;
        bool has_alloc = false;
        for (const auto &e : abi) {
            if (e.name == "run") {
                has_run = true;
            }
            if (e.name == "alloc") {
                has_alloc = true;
            }
        }
        check(abi.size() == 5 && has_run && has_alloc &&
                  ahfl::backends::WasmAbiContract::kVersion == 1 &&
                  ahfl::backends::WasmAbiContract::kFrameFormat == "value_json",
              "wasm_abi_exports catalogue + WasmAbiContract constants");
    }

    // Test 6 (RFC 0019 slice 2): least-privilege effect -> WASI projection.
    {
        using ahfl::backends::WasmCapabilityEffect;
        using ahfl::backends::WasiCapability;

        auto has_cap = [](const ahfl::backends::WasiConfig &c, WasiCapability cap) {
            for (auto v : c.allowed_capabilities) {
                if (v == cap) {
                    return true;
                }
            }
            return false;
        };

        // Pure agent (no capabilities) -> zero WASI capability.
        auto pure = ahfl::backends::project_wasi_config({});
        check(pure.allowed_capabilities.empty(), "pure agent projects to zero WASI capability");

        // Read-only capability -> still empty (not a WASI resource).
        auto read_only = ahfl::backends::project_wasi_config({WasmCapabilityEffect::Read});
        check(read_only.allowed_capabilities.empty(),
              "read-only capability projects to zero WASI capability");

        // External side effect -> NetworkAccess (external-effect channel).
        auto external =
            ahfl::backends::project_wasi_config({WasmCapabilityEffect::ExternalSideEffect});
        check(external.allowed_capabilities.size() == 1 &&
                  has_cap(external, WasiCapability::NetworkAccess),
              "external side effect projects to NetworkAccess");

        // Unknown effect -> conservative NetworkAccess.
        auto unknown = ahfl::backends::project_wasi_config({WasmCapabilityEffect::Unknown});
        check(has_cap(unknown, WasiCapability::NetworkAccess),
              "unknown effect conservatively projects to NetworkAccess");

        // Mixed read + durable write -> single NetworkAccess (deduped severity).
        auto mixed = ahfl::backends::project_wasi_config(
            {WasmCapabilityEffect::Read, WasmCapabilityEffect::DurableWrite});
        check(mixed.allowed_capabilities.size() == 1 &&
                  has_cap(mixed, WasiCapability::NetworkAccess),
              "mixed read + durable write projects to a single NetworkAccess");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
