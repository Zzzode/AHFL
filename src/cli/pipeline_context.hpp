#pragma once

#include "ahfl/cli/command_catalog.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace ahfl::cli {

struct PackagePipelineContext {
    const ahfl::ir::Program &program;
    const ahfl::handoff::PackageMetadata &metadata;
    const ahfl::dry_run::CapabilityMockSet *mock_set;
    const CommandLineOptions &options;
};

using PackageCommandHandler = int (*)(const PackagePipelineContext &context);

template <int (*CommandFn)(const ahfl::ir::Program &,
                           const ahfl::handoff::PackageMetadata &,
                           const ahfl::dry_run::CapabilityMockSet &,
                           const CommandLineOptions &)>
[[nodiscard]] int invoke_package_command(const PackagePipelineContext &context) {
    if (context.mock_set == nullptr) {
        std::cerr << "error: internal command dispatch failed: missing capability mocks\n";
        return 1;
    }

    return CommandFn(context.program, context.metadata, *context.mock_set, context.options);
}

} // namespace ahfl::cli
