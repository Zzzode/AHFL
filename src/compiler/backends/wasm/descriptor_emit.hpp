#pragma once

#include <string>
#include <string_view>

#include "base/json/json_value.hpp"
#include "compiler/backends/wasm/core_wasm_codegen.hpp"

namespace ahfl::backends {

/// Schema identifier for the embedded-host execution descriptor. Unlike the
/// conformance descriptor (`ahfl.conformance-wasm-descriptor`), this
/// document carries no scenarios: the embedded host (playground, generic JS
/// host) supplies its own input JSON and drives the Core-Wasm ABI directly.
inline constexpr std::string_view kWasmdExecutionDescriptorFormat =
    "ahfl.wasm-execution-descriptor";

/// Populates the descriptor-derived JSON fields shared by every embedded-host
/// execution descriptor onto `root`: the reachable-import catalogue, the
/// node-event buffer layout, the bump-heap base, the scheduled-node count, the
/// P6-frame lane (fixed-region coordinates + dense layout table), the boundary
/// wire-schema table, and the workflow/agent lane. The caller owns `root` and
/// adds its own schema + identity fields (case/scenarios for conformance,
/// kind/entry for the generic descriptor).
void populate_wasm_descriptor_fields(json::JsonValue &root,
                                     const CoreWasmExecutionDescriptor &descriptor);

/// Renders the full execution descriptor document for an embedded host that
/// supplies its own input. `kind` is "workflow" or "agent"; `entry` is the
/// canonical entry target name. The document carries no scenarios.
[[nodiscard]] std::string
render_wasm_execution_descriptor(const CoreWasmExecutionDescriptor &descriptor,
                                 std::string_view kind,
                                 std::string_view entry);

} // namespace ahfl::backends
