#pragma once

#include <ostream>

#include "ahfl/compiler/backends/driver.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

void print_execution_plan_json(const handoff::ExecutionPlan &plan, std::ostream &out);

[[nodiscard]] EmitResult print_program_execution_plan(const ir::Program &program,
                                                      std::ostream &out);
[[nodiscard]] EmitResult print_program_execution_plan(const ir::Program &program,
                                                      const handoff::PackageMetadata &metadata,
                                                      std::ostream &out);

} // namespace ahfl
