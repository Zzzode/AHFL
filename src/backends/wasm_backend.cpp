#include "ahfl/backends/wasm_backend.hpp"

namespace ahfl::backends {

std::string emit_wat_header(const std::string& module_name) {
    std::string wat;
    wat += "(module $" + module_name + "\n";
    wat += "  (type $state_fn (func (param i32) (result i32)))\n";
    wat += "  (memory (export \"memory\") 1)\n";
    return wat;
}

std::string emit_wat_state_table(const std::vector<std::string>& states) {
    std::string wat;
    wat += "  ;; state table: " + std::to_string(states.size()) + " states\n";
    for (std::size_t i = 0; i < states.size(); ++i) {
        wat += "  ;; state " + std::to_string(i) + ": " + states[i] + "\n";
    }
    return wat;
}

WasmModule generate_wasm(const WasmAgentConfig& config) {
    WasmModule mod;
    mod.module_name = config.agent_name;

    std::string wat;
    wat += emit_wat_header(config.agent_name);
    wat += emit_wat_state_table(config.states);
    wat += "  (func $get_state (export \"get_state\") (result i32)\n";
    wat += "    (i32.const 0)\n";
    wat += "  )\n";
    wat += "  (func $transition (export \"transition\") (param $input i32) (result i32)\n";
    wat += "    ;; state machine logic\n";
    wat += "    (local.get $input)\n";
    wat += "  )\n";
    wat += ")\n";

    mod.wat_source = wat;
    mod.exports.emplace_back("memory");
    mod.exports.emplace_back("get_state");
    mod.exports.emplace_back("transition");

    for (const auto& cap : config.capabilities) {
        mod.imports.emplace_back("env." + cap);
    }

    return mod;
}

} // namespace ahfl::backends
