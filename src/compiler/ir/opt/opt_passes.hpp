#pragma once

#include "ahfl/compiler/ir/opt/opt_ir.hpp"

namespace ahfl::ir::opt {

/// Constant propagation: replace uses of locals that are assigned constants
/// with the constant value directly.
/// Returns true if any modification was made.
[[nodiscard]] bool constant_propagation(OptFunction &function);

/// Dead store elimination: remove assignments to locals that are never read.
/// Returns true if any modification was made.
[[nodiscard]] bool dead_store_elimination(OptFunction &function);

/// Copy propagation: if x = y (Use rvalue), replace all uses of x with y.
/// Returns true if any modification was made.
[[nodiscard]] bool copy_propagation(OptFunction &function);

/// Run all optimization passes in a loop until fixpoint.
/// Returns true if any modification was made across all iterations.
[[nodiscard]] bool optimize(OptFunction &function);

} // namespace ahfl::ir::opt
