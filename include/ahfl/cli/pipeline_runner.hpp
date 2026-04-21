#pragma once

#include "ahfl/cli/command_catalog.hpp"

#include <optional>

namespace ahfl {
namespace dry_run {
struct CapabilityMockSet;
} // namespace dry_run

namespace handoff {
struct PackageMetadata;
} // namespace handoff

namespace ir {
struct Program;
} // namespace ir
} // namespace ahfl

namespace ahfl::cli {

[[nodiscard]] std::optional<int> dispatch_package_command(
    CommandKind command,
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet *mock_set,
    const CommandLineOptions &options);

} // namespace ahfl::cli
