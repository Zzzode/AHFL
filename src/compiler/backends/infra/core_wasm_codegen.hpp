#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"

namespace ahfl::backends {

namespace core_wasm_diag {
inline constexpr std::string_view kInvalidCore = "wasm.INVALID_CORE";
inline constexpr std::string_view kInvalidLayout = "wasm.INVALID_LAYOUT";
inline constexpr std::string_view kUnsupportedTarget = "wasm.UNSUPPORTED_TARGET";
inline constexpr std::string_view kEntryAmbiguous = "wasm.ENTRY_AMBIGUOUS";
inline constexpr std::string_view kEntryNotFound = "wasm.ENTRY_NOT_FOUND";
inline constexpr std::string_view kUnsupportedOrchestration =
    "wasm.UNSUPPORTED_ORCHESTRATION";
inline constexpr std::string_view kNonterminatingE1Run = "wasm.NONTERMINATING_E1_RUN";
inline constexpr std::string_view kInvalidCapabilityAbi =
    "wasm.INVALID_CAPABILITY_ABI";
inline constexpr std::string_view kUnsupportedCapabilityFrame =
    "wasm.UNSUPPORTED_CAPABILITY_FRAME";
inline constexpr std::string_view kUnsupportedWorkflowFrame =
    "wasm.UNSUPPORTED_WORKFLOW_FRAME";
inline constexpr std::string_view kBinaryOverflow = "wasm.BINARY_OVERFLOW";
inline constexpr std::string_view kResourceExhausted = "wasm.RESOURCE_EXHAUSTED";
inline constexpr std::string_view kInternalInvalid = "wasm.INTERNAL_INVALID";
} // namespace core_wasm_diag

using CoreWasmEntry =
    std::variant<ir::core::CoreAgentId, ir::core::CoreWorkflowId>;

struct CoreWasmTarget {
    CoreWasmEntry entry{ir::core::CoreAgentId{}};
    WasmProfileKind profile{WasmProfileKind::Wasi};
};

struct CoreWasmArtifact {
    std::vector<std::uint8_t> bytes;
    CoreWasmEntry entry{ir::core::CoreAgentId{}};
    std::vector<ir::core::CoreInstanceId> packaged_agent_instances;
    std::vector<std::string> exports;
    std::vector<std::string> imports;
};

struct CoreWasmDiagnostic {
    std::string code;
    std::string message;
    ir::SourceRangeOpt source_range;
};

struct CoreWasmCodegenResult {
    std::optional<CoreWasmArtifact> artifact;
    std::vector<CoreWasmDiagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept {
        return artifact.has_value() && diagnostics.empty();
    }
};

/// Resolve the package/legacy CLI boundary into the same typed entry consumed
/// by emit_core_wasm. An explicit package entry is exact-canonical and never
/// falls back to a display name or declaration position.
[[nodiscard]] std::expected<CoreWasmEntry, CoreWasmDiagnostic>
resolve_core_wasm_entry(const ir::core::CoreProgram &program,
                        const handoff::PackageMetadata *package_metadata);

/// Emit a deterministic wasm32 binary selected by one typed entry identity:
/// the KR6.5 E1/E2 agent subset, the E3 no-capability identity-workflow subset,
/// or (RFC 0026 E4-B2-C FOUNDATION) a capability-bearing workflow module. The
/// public C ABI and exported symbol names are unchanged across all of these; the
/// capability-workflow artifact reuses the E2 `ahfl_cap` import ABI and
/// additionally carries an exec-manifest and a node-event buffer.
/// Pure: neither the verified Core program nor its P4-D layout side artifact is
/// mutated. Unsupported Core nodes fail closed with no partial artifact.
[[nodiscard]] CoreWasmCodegenResult
emit_core_wasm(const ir::core::CoreProgram &program,
               const ir::core::CoreLayoutTable &layouts,
               CoreWasmTarget target);

} // namespace ahfl::backends
