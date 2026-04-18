#pragma once

#include <ostream>

#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl {

void print_package_native_json(const handoff::Package &package, std::ostream &out);

void print_program_native_json(const ir::Program &program, std::ostream &out);
void print_program_native_json(const ir::Program &program,
                               const handoff::PackageMetadata &metadata,
                               std::ostream &out);

} // namespace ahfl
