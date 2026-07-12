#include "tooling/cli/pipeline_runner.hpp"

#include "pipeline_context.hpp"
#include "pipeline_core_commands.hpp"

#include <optional>

namespace ahfl::cli {

bool handles_package_command(CommandKind command) {
    return handles_core_pipeline_command(command);
}

std::optional<int> dispatch_package_command(CommandKind command,
                                            const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet *mock_set,
                                            const CommandLineOptions &options) {
    const PackagePipelineContext context{
        program,
        metadata,
        mock_set,
        options,
        {},
    };
    return dispatch_core_pipeline_command(command, context);
}

} // namespace ahfl::cli
