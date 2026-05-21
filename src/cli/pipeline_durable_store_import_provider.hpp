#pragma once

#include "ahfl/cli/command_catalog.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

namespace ahfl::cli {

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND(kind,                                       \
                                                       token,                                      \
                                                       usage_order,                                \
                                                       action_order,                               \
                                                       inference_order,                            \
                                                       package_order,                              \
                                                       capability_order,                           \
                                                       handler)                                    \
    [[nodiscard]] int handler(const ahfl::ir::Program &program,                                    \
                              const ahfl::handoff::PackageMetadata &metadata,                      \
                              const ahfl::dry_run::CapabilityMockSet &mock_set,                    \
                              const CommandLineOptions &options);
#include "ahfl/cli/durable_store_import_provider_commands.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND

} // namespace ahfl::cli
