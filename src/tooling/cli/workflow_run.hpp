#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "tooling/cli/command_catalog.hpp"

#include <iosfwd>

namespace ahfl::cli {

[[nodiscard]] int run_workflow_with_llm(const ahfl::ir::Program &program,
                                        const CommandLineOptions &options,
                                        std::ostream &out,
                                        std::ostream &err);

} // namespace ahfl::cli
