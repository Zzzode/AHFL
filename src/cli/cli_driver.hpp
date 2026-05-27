#pragma once

#include "cli/command_catalog.hpp"
#include "cli/diagnostic_consumer.hpp"
#include "cli/exit_code.hpp"
#include "dry_run/runner.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"

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
