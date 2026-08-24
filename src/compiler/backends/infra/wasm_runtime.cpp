#include "compiler/backends/infra/wasm_runtime.hpp"

namespace ahfl::backends {

namespace {

const char *wasi_capability_name(WasiCapability cap) {
    switch (cap) {
    case WasiCapability::FileRead:
        return "fd_read";
    case WasiCapability::FileWrite:
        return "fd_write";
    case WasiCapability::NetworkAccess:
        return "sock_accept";
    case WasiCapability::EnvironmentVars:
        return "environ_get";
    case WasiCapability::ClockAccess:
        return "clock_time_get";
    }
    return "unknown";
}

} // anonymous namespace

std::vector<WasmAbiExport> wasm_abi_exports() {
    // RFC 0019 §"WASM ↔ host 执行契约" — the stable exported ABI, in contract
    // order. Signatures are WAT fragments; `memory` and the two globals are
    // exports too but declared inline in the module header.
    return {
        {"alloc", "(param i32) (result i32)", "host allocates an input frame buffer"},
        {"dealloc", "(param i32 i32)", "host releases an output frame buffer"},
        {"run", "(param i32 i32) (result i32)", "execute to a final state; (input ptr,len) -> output ptr"},
        {"step", "(result i32)", "single state transition (DAP/playground stepping)"},
        {"current_state", "(result i32)", "current state index (maps back to the AHFL state name)"},
    };
}

std::string generate_wasi_imports(const WasiConfig &config) {
    std::string imports;
    imports += "  ;; WASI imports\n";
    for (const auto &cap : config.allowed_capabilities) {
        imports += "  (import \"wasi_snapshot_preview1\" \"";
        imports += wasi_capability_name(cap);
        imports += "\" (func $wasi_";
        imports += wasi_capability_name(cap);
        imports += " (param i32 i32 i32 i32) (result i32)))\n";
    }
    return imports;
}

bool validate_runtime_config(const WasmRuntimeConfig &config) {
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
