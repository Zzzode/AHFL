#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::backends {

enum class WasiCapability {
    FileRead,
    FileWrite,
    NetworkAccess,
    EnvironmentVars,
    ClockAccess
};

struct WasiConfig {
    std::vector<WasiCapability> allowed_capabilities;
    std::vector<std::string> preopen_dirs;
    std::vector<std::pair<std::string, std::string>> env_vars;
};

struct WasmRuntimeConfig {
    std::size_t max_memory_pages = 256;
    std::size_t max_stack_depth = 1024;
    WasiConfig wasi;
};

// RFC 0019: the stable WASM ↔ host ABI contract. The compiled agent module
// exports these functions/globals with fixed signatures so any host (a wasi
// embedder or a browser JS host) can drive it uniformly. Codegen of the agent
// body is out of scope (RFC 0019 Non-Goal 1); this describes the boundary.
//
// input/context/output cross the boundary as length-prefixed value_json byte
// streams (RFC 0019 OQ1). Ownership (OQ2): the host calls `alloc` to obtain a
// buffer, writes the input frame, passes (ptr,len) to `run`; the module returns
// the output frame pointer, which the host reads then releases via `dealloc`.
struct WasmAbiContract {
    // ABI version; bumped when the frame layout or serialization format changes
    // (RFC 0019 OQ4 — Component Model would be a future major version).
    static constexpr int kVersion = 1;

    // Serialization format tag for the boundary frames.
    static constexpr std::string_view kFrameFormat = "value_json";
};

// A single export in the ABI contract: the exported name plus its WAT signature
// fragment, kept together so the emitter and any host-side binding generator
// read the contract from one place.
struct WasmAbiExport {
    std::string_view name;      // exported symbol, e.g. "run"
    std::string_view signature; // WAT type, e.g. "(param i32 i32) (result i32)"
    std::string_view purpose;   // human-readable role
};

// The exported functions of the agent ABI, in contract order.
[[nodiscard]] std::vector<WasmAbiExport> wasm_abi_exports();

[[nodiscard]] std::string generate_wasi_imports(const WasiConfig &config);
[[nodiscard]] bool validate_runtime_config(const WasmRuntimeConfig &config);

} // namespace ahfl::backends
