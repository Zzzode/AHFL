#pragma once

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/handoff/package.hpp"
#include "pipeline/execution/dry_run/runner.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/diagnostic_consumer.hpp"
#include "tooling/cli/exit_code.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace ahfl::package_graph {
struct PackageGraph;
}

namespace ahfl::cli {

struct MemoryReportSnapshot {
    std::size_t source_count{};
    std::size_t source_bytes{};
    std::size_t typed_declarations{};
    std::size_t typed_expressions{};
    std::size_t typed_blocks{};
    std::size_t typed_statements{};
    std::size_t typed_temporal_expressions{};
    std::size_t typed_symbols{};
    std::size_t typed_references{};
    std::size_t typed_imports{};
    std::size_t ir_declarations{};
    std::size_t ir_expressions{};
    std::size_t proxy_current_bytes{};
    std::size_t proxy_peak_bytes{};
    std::size_t proxy_allocation_count{};
};

class CliDriver final {
  public:
    [[nodiscard]] ExitCode run(std::span<const std::string_view> arguments);

  private:
    using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

    [[nodiscard]] std::optional<ExitCode>
    parse_command_line(std::span<const std::string_view> arguments);

    [[nodiscard]] std::optional<ExitCode> validate_options();

    [[nodiscard]] std::optional<ExitCode> load_package_and_mocks();

    [[nodiscard]] ExitCode run_observed();

    [[nodiscard]] ExitCode execute();

    [[nodiscard]] ExitCode dump_package_graph();
    [[nodiscard]] ExitCode dump_lockfile();

    [[nodiscard]] ExitCode run_manifest_package();
    [[nodiscard]] ExitCode run_workspace_package();
    [[nodiscard]] ExitCode
    run_package_graph_package(const ahfl::package_graph::PackageGraph &graph);

    [[nodiscard]] ExitCode format_source_file();

    template <typename InputT>
    [[nodiscard]] ExitCode run_analysis(const InputT &input, MaybeSourceFile source_file);

    CommandLineOptions options_;
    std::optional<CommandKind> effective_command_;
    ahfl::Frontend frontend_;
    std::optional<ahfl::handoff::PackageMetadata> package_metadata_;
    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set_;
    std::optional<MemoryReportSnapshot> memory_report_;
    std::unique_ptr<DiagnosticConsumer> diag_consumer_;
};

} // namespace ahfl::cli
