#pragma once

#include <ostream>

#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

struct SourceGraph;

enum class BackendKind {
    Ir,
    IrJson,
    NativeJson,
    ExecutionPlan,
    PackageReview,
    Summary,
    Smv,
};

void emit_backend(BackendKind kind,
                  const ir::Program &program,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata = nullptr);

void emit_backend(BackendKind kind,
                  const ir::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata = nullptr);

void emit_backend(BackendKind kind,
                  const ast::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata = nullptr);

void emit_backend(BackendKind kind,
                  const SourceGraph &graph,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out,
                  const handoff::PackageMetadata *package_metadata = nullptr);

} // namespace ahfl
