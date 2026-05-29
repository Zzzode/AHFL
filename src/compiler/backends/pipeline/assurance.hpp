#pragma once

#include <ostream>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

void print_program_assurance_json(const ir::Program &program, std::ostream &out);

} // namespace ahfl
