#pragma once

#include <ostream>

#include "ahfl/ir/ir.hpp"

namespace ahfl {

enum class BackendKind {
    Ir,
    IrJson,
    Smv,
};

void emit_backend(BackendKind kind, const ir::Program &program, std::ostream &out);

void emit_backend(BackendKind kind,
                  const ast::Program &program,
                  const ResolveResult &resolve_result,
                  const TypeCheckResult &type_check_result,
                  std::ostream &out);

} // namespace ahfl
