#pragma once

#include <ostream>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl {

void print_package_review(const handoff::Package &package, std::ostream &out);

void print_program_package_review(const ir::Program &program, std::ostream &out);
void print_program_package_review(const ir::Program &program,
                                  const handoff::PackageMetadata &metadata,
                                  std::ostream &out);

} // namespace ahfl
