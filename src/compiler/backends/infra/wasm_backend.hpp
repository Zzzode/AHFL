#pragma once

#include <string>
#include <utility>
#include <vector>

#include "compiler/backends/infra/wasm_runtime.hpp"

namespace ahfl::backends {

// Legacy RFC 0019 textual-WAT contract helper. KR6.5 executable emission uses
// emit_core_wasm(CoreProgram, CoreLayoutTable) and the CLI never calls this
// projection path. Retained temporarily for ABI catalogue/unit compatibility.
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
    // RFC 0019 slice 3: the SymbolId of each capability in `capabilities`
    // (parallel vector). The WASM import field name is derived from this
    // index-based identity, not the source name. When a ref has no resolved id
    // the ordinal position is used so output stays deterministic.
    std::vector<std::size_t> capability_ids;
};

[[nodiscard]] WasmModule generate_wasm(const WasmAgentConfig &config);
[[nodiscard]] std::string emit_wat_header(const std::string &module_name);
[[nodiscard]] std::string emit_wat_state_table(const std::vector<std::string> &states);

// RFC 0019 slice 4/5: the WASM deployment profile (backend-local mirror of
// ahfl::WasmProfile, so the infra backend need not depend on driver.hpp). The
// module ABI is profile-independent; the profile only changes the host import
// set (WASI imports vs JS-proxy capability imports) and the browser capability
// restrictions.
enum class WasmProfileKind {
    Wasi,
    Browser,
};

// Profile-aware generation. The `wasi` profile is the default and matches the
// single-arg overload. Pure.
[[nodiscard]] WasmModule generate_wasm(const WasmAgentConfig &config, WasmProfileKind profile);

// RFC 0019 slice 4: capabilities in `config` that have no browser-profile
// equivalent (filesystem / environment access), by source name. Empty under
// the wasi profile's allowances; used to reject a browser emit at compile time.
[[nodiscard]] std::vector<std::string>
browser_rejected_capabilities(const WasmAgentConfig &config);

// Whether a WASI capability has a browser-profile equivalent (network via
// fetch, clock via an injected JS clock). Filesystem / environment do not.
[[nodiscard]] bool wasi_capability_browser_supported(WasiCapability cap);

} // namespace ahfl::backends
