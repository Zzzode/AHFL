#include "backends/wasm_runtime.hpp"

namespace ahfl::backends {

namespace {

const char* wasi_capability_name(WasiCapability cap) {
    switch (cap) {
        case WasiCapability::FileRead: return "fd_read";
        case WasiCapability::FileWrite: return "fd_write";
        case WasiCapability::NetworkAccess: return "sock_accept";
        case WasiCapability::EnvironmentVars: return "environ_get";
        case WasiCapability::ClockAccess: return "clock_time_get";
    }
    return "unknown";
}

} // anonymous namespace

std::string generate_wasi_imports(const WasiConfig& config) {
    std::string imports;
    imports += "  ;; WASI imports\n";
    for (const auto& cap : config.allowed_capabilities) {
        imports += "  (import \"wasi_snapshot_preview1\" \"";
        imports += wasi_capability_name(cap);
        imports += "\" (func $wasi_";
        imports += wasi_capability_name(cap);
        imports += " (param i32 i32 i32 i32) (result i32)))\n";
    }
    return imports;
}

bool validate_runtime_config(const WasmRuntimeConfig& config) {
    if (config.max_memory_pages == 0) {
        return false;
    }
    if (config.max_stack_depth == 0) {
        return false;
    }
    if (config.max_memory_pages > 65536) {
        return false;
    }
    return true;
}

} // namespace ahfl::backends
