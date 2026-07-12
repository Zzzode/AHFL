#include "pipeline_core_commands.hpp"
#include "cli_pipeline_artifacts.hpp"

#include "compiler/backends/pipeline/dry_run_trace.hpp"
#include "compiler/backends/pipeline/execution_plan.hpp"

#include <array>
#include <optional>

namespace ahfl::cli {
namespace {

[[nodiscard]] int emit_execution_plan_with_diagnostics(const PackagePipelineContext &context) {
    const auto plan =
        build_execution_plan_for_cli(context.program, context.metadata, context.io.err);
    if (!plan.has_value())
        return 1;
    ahfl::print_execution_plan_json(*plan, context.io.out);
    return 0;
}

[[nodiscard]] int emit_dry_run_trace_with_diagnostics(const PackagePipelineContext &context) {
    if (context.mock_set == nullptr) {
        context.io.err << "error: internal command dispatch failed: missing capability mocks\n";
        return 1;
    }

    CliPipelineArtifacts artifacts({
        context.program,
        context.metadata,
        *context.mock_set,
        context.options,
        command_name(CommandKind::EmitDryRunTrace),
        context.io.err,
    });
    const auto *trace = artifacts.dry_run_trace();
    if (trace == nullptr)
        return 1;
    ahfl::print_dry_run_trace_json(*trace, context.io.out);
    return 0;
}

constexpr auto kCorePipelineCommandHandlers = [] {
    std::array<PackageCommandHandler, static_cast<std::size_t>(CommandKind::_Count)> table{};

    table[static_cast<std::size_t>(CommandKind::EmitExecutionPlan)] =
        emit_execution_plan_with_diagnostics;
    table[static_cast<std::size_t>(CommandKind::EmitDryRunTrace)] =
        emit_dry_run_trace_with_diagnostics;

    return table;
}();

} // namespace

bool handles_core_pipeline_command(CommandKind command) {
    const auto idx = static_cast<std::size_t>(command);
    return idx < kCorePipelineCommandHandlers.size() &&
           kCorePipelineCommandHandlers[idx] != nullptr;
}

std::optional<int> dispatch_core_pipeline_command(CommandKind command,
                                                  const PackagePipelineContext &context) {
    const auto idx = static_cast<std::size_t>(command);
    if (idx >= kCorePipelineCommandHandlers.size()) {
        return std::nullopt;
    }
    const auto handler = kCorePipelineCommandHandlers[idx];
    if (handler == nullptr) {
        return std::nullopt;
    }
    return handler(context);
}

} // namespace ahfl::cli
