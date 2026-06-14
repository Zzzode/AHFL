#pragma once

#include "ahfl/compiler/ir/opt/opt_ir.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir::opt {

/// Lower the semantic IR program to the optimization IR.
/// Each FlowDecl's StateHandler becomes an OptFunction.
[[nodiscard]] OptProgram lower_to_opt(const Program &program);

} // namespace ahfl::ir::opt
