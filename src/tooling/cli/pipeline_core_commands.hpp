#pragma once

#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/pipeline_context.hpp"

#include <optional>

namespace ahfl::cli {

[[nodiscard]] bool handles_core_pipeline_command(CommandKind command);

[[nodiscard]] std::optional<int>
dispatch_core_pipeline_command(CommandKind command, const PackagePipelineContext &context);

} // namespace ahfl::cli
