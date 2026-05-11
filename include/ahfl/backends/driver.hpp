#pragma once

#include <ostream>

#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

enum class BackendKind {
    Ir,
    IrJson,
    NativeJson,
    ExecutionPlan,
    PackageReview,
    Summary,
    Smv,
    AssuranceJson,
};

void emit_backend(BackendKind kind,
                  const ir::Program &program,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata = nullptr);

} // namespace ahfl
