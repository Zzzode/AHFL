#pragma once

#include <expected>
#include <ostream>
#include <string>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

/// Result of a backend emission. Void on success, error message on failure.
using EmitResult = std::expected<void, std::string>;

enum class BackendKind {
    Ir,
    IrJson,
    NativeJson,
    ExecutionPlan,
    PackageReview,
    Summary,
    Smv,
    AssuranceJson,
    InfraK8sCrd,
    InfraOpenApi,
    InfraTerraform,
    InfraWasm,
};

/// RFC 0019: deployment profile for the WASM backend. `wasi` targets a
/// command-line wasm runtime with WASI imports; `browser` targets a JS host
/// with capability proxies and no WASI (filesystem capabilities are rejected
/// at emit time). Ignored by non-WASM backends.
enum class WasmProfile {
    Wasi,
    Browser,
};

[[nodiscard]] EmitResult emit_backend(BackendKind kind,
                                      ir::Program &program,
                                      std::ostream &out,
                                      const handoff::PackageMetadata *package_metadata = nullptr,
                                      WasmProfile wasm_profile = WasmProfile::Wasi);

} // namespace ahfl
