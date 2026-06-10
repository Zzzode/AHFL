#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/semantics/typed_hir.hpp"

namespace ahfl {

[[nodiscard]] ir::Program lower_typed_program(const TypedProgram &program);

} // namespace ahfl
