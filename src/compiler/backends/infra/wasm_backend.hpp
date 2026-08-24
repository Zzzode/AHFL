#pragma once

#include <string>
#include <utility>
#include <vector>

#include "compiler/backends/infra/wasm_runtime.hpp"

namespace ahfl::backends {

struct WasmModule {
    std::string module_name;
    std::string wat_source;
    std::vector<std::string> exports;
    std::vector<std::string> imports;
};

struct WasmAgentConfig {
    std::string agent_name;
    std::vector<std::string> states;
    std::vector<std::pair<std::string, std::string>> transitions;
    std::vector<std::string> capabilities;
    // RFC 0019 slice 2: the effect kind of each capability in `capabilities`
    // (parallel vector). Drives the least-privilege effect -> WASI projection.
    // An empty vector (e.g. no capabilities) projects to zero WASI capability.
    std::vector<WasmCapabilityEffect> capability_effects;
};

[[nodiscard]] WasmModule generate_wasm(const WasmAgentConfig &config);
[[nodiscard]] std::string emit_wat_header(const std::string &module_name);
[[nodiscard]] std::string emit_wat_state_table(const std::vector<std::string> &states);

} // namespace ahfl::backends
