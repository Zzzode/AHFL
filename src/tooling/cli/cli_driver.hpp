#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/diagnostic_consumer.hpp"
#include "tooling/cli/exit_code.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace ahfl::cli {

class CliDriver final {
  public:
    [[nodiscard]] ExitCode run(std::span<const std::string_view> arguments);

  private:
    using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

    [[nodiscard]] std::optional<ExitCode>
    parse_command_line(std::span<const std::string_view> arguments);

    [[nodiscard]] std::optional<ExitCode> validate_options();

    [[nodiscard]] std::optional<ExitCode> load_package_and_mocks();

    [[nodiscard]] ExitCode execute();

    template <typename InputT>
    [[nodiscard]] ExitCode run_analysis(const InputT &input, MaybeSourceFile source_file);

    CommandLineOptions options_;
    std::optional<CommandKind> effective_command_;
    ahfl::Frontend frontend_;
    std::optional<ahfl::handoff::PackageMetadata> package_metadata_;
    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set_;
    std::unique_ptr<DiagnosticConsumer> diag_consumer_;
};

} // namespace ahfl::cli
