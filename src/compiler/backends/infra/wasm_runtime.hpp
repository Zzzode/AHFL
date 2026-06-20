#pragma once

#include <cstddef>
#include <string>
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

[[nodiscard]] std::string generate_wasi_imports(const WasiConfig &config);
[[nodiscard]] bool validate_runtime_config(const WasmRuntimeConfig &config);

} // namespace ahfl::backends
