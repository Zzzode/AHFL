#pragma once

#include "ahfl/compiler/ir/opt/opt_ir.hpp"

#include <iosfwd>

namespace ahfl::ir::opt {

void print_opt_program_json(const OptProgram &program, std::ostream &out);

} // namespace ahfl::ir::opt
