#pragma once

#include <string>
#include <utility>
#include <vector>

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
};

[[nodiscard]] WasmModule generate_wasm(const WasmAgentConfig &config);
[[nodiscard]] std::string emit_wat_header(const std::string &module_name);
[[nodiscard]] std::string emit_wat_state_table(const std::vector<std::string> &states);

} // namespace ahfl::backends
