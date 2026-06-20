#pragma once

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/output_context.hpp"

#include <iostream>
#include <optional>
#include <ostream>
#include <string_view>

namespace ahfl::cli {

template <typename Artifact>
using CliArtifactBuilder = std::optional<Artifact> (*)(const ahfl::ir::Program &,
                                                       const ahfl::handoff::PackageMetadata &,
                                                       const ahfl::dry_run::CapabilityMockSet &,
                                                       const CommandLineOptions &,
                                                       std::string_view);

template <typename Artifact> using ArtifactPrinter = void (*)(const Artifact &, std::ostream &);

template <typename Artifact>
[[nodiscard]] int emit_cli_artifact(CliArtifactBuilder<Artifact> build,
                                    ArtifactPrinter<Artifact> print,
                                    const ahfl::ir::Program &program,
                                    const ahfl::handoff::PackageMetadata &metadata,
                                    const ahfl::dry_run::CapabilityMockSet &mock_set,
                                    const CommandLineOptions &options,
                                    std::string_view command_name,
                                    const OutputContext &io = {}) {
    const auto artifact = build(program, metadata, mock_set, options, command_name);
    if (!artifact.has_value()) {
        return 1;
    }

    print(*artifact, io.out);
    return 0;
}

template <typename Result, typename Artifact>
[[nodiscard]] std::optional<Artifact>
unwrap_cli_result(Result &&result,
                  std::optional<Artifact> Result::*artifact_member,
                  std::ostream &err = std::cerr) {
    result.diagnostics.render(err);
    const auto &artifact = result.*artifact_member;
    if (result.has_errors() || !artifact.has_value()) {
        return std::nullopt;
    }
    return *artifact;
}

} // namespace ahfl::cli
