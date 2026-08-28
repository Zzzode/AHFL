#pragma once

#include <ostream>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"

namespace ahfl {

// RFC 0026 (KR6.3): the SMV verification backend consumes the AHFL-IR
// (verification / orchestration) layer of the IR tower. `ir::AhflIr` is that
// layer; today it aliases `ir::Program`, so this is a zero-behavior-change
// boundary annotation.
void print_program_smv(const ir::AhflIr &program, std::ostream &out);

void emit_program_smv(const ast::Program &program,
                      const ResolveResult &resolve_result,
                      const TypeCheckResult &type_check_result,
                      std::ostream &out);

} // namespace ahfl
