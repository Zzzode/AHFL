#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"

namespace ahfl::backends {

namespace core_wasm_diag {
inline constexpr std::string_view kInvalidCore = "wasm.INVALID_CORE";
inline constexpr std::string_view kInvalidLayout = "wasm.INVALID_LAYOUT";
inline constexpr std::string_view kUnsupportedTarget = "wasm.UNSUPPORTED_TARGET";
inline constexpr std::string_view kEntryAmbiguous = "wasm.ENTRY_AMBIGUOUS";
inline constexpr std::string_view kUnsupportedOrchestration =
    "wasm.UNSUPPORTED_ORCHESTRATION";
inline constexpr std::string_view kNonterminatingE1Run = "wasm.NONTERMINATING_E1_RUN";
inline constexpr std::string_view kInvalidCapabilityAbi =
    "wasm.INVALID_CAPABILITY_ABI";
inline constexpr std::string_view kUnsupportedCapabilityFrame =
    "wasm.UNSUPPORTED_CAPABILITY_FRAME";
inline constexpr std::string_view kBinaryOverflow = "wasm.BINARY_OVERFLOW";
inline constexpr std::string_view kInternalInvalid = "wasm.INTERNAL_INVALID";
} // namespace core_wasm_diag

struct CoreWasmTarget {
    ir::core::CoreAgentId agent{};
    WasmProfileKind profile{WasmProfileKind::Wasi};
};

struct CoreWasmArtifact {
    std::vector<std::uint8_t> bytes;
    ir::core::CoreAgentId agent{};
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

/// Emit the KR6.5 E1/E2 orchestration subset as a deterministic wasm32 binary.
/// Pure: neither the verified Core program nor its P4-D layout side artifact is
/// mutated. Unsupported Core nodes fail closed with no partial artifact.
[[nodiscard]] CoreWasmCodegenResult
emit_core_wasm(const ir::core::CoreProgram &program,
               const ir::core::CoreLayoutTable &layouts,
               CoreWasmTarget target);

} // namespace ahfl::backends
