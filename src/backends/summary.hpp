#pragma once

#include <ostream>

#include "ahfl/ir/ir.hpp"

namespace ahfl {

void print_program_summary(const ir::Program &program, std::ostream &out);

void emit_program_summary(const ast::Program &program,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

void emit_program_summary(const SourceGraph &graph,
                          const ResolveResult &resolve_result,
                          const TypeCheckResult &type_check_result,
                          std::ostream &out);

} // namespace ahfl
