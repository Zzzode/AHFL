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

[[nodiscard]] EmitResult emit_backend(BackendKind kind,
                                      const ir::Program &program,
                                      std::ostream &out,
                                      const handoff::PackageMetadata *package_metadata = nullptr);

} // namespace ahfl
