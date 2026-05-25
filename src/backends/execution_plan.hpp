#pragma once

#include <ostream>

#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

void print_execution_plan_json(const handoff::ExecutionPlan &plan, std::ostream &out);

void print_program_execution_plan(const ir::Program &program, std::ostream &out);
void print_program_execution_plan(const ir::Program &program,
                                  const handoff::PackageMetadata &metadata,
                                  std::ostream &out);

} // namespace ahfl
