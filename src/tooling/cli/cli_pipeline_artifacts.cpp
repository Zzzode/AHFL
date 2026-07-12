#include "cli_pipeline_artifacts.hpp"

#include "compiler/backends/pipeline/execution_plan.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ahfl::cli {
namespace {

[[nodiscard]] std::optional<std::string> to_owned_string(std::optional<std::string_view> value) {
    return value.transform([](std::string_view text) { return std::string(text); });
}

[[nodiscard]] std::optional<ahfl::dry_run::DryRunRequest>
build_dry_run_request_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const CommandLineOptions &options,
                              std::string_view command_name,
                              std::ostream &err) {
    auto workflow_name = to_owned_string(options.workflow_name);
    if (!workflow_name.has_value()) {
        workflow_name = plan.entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        err << "error: " << command_name << " requires --workflow or package workflow entry\n";
        return std::nullopt;
    }

    return ahfl::dry_run::DryRunRequest{
        .workflow_canonical_name = std::move(*workflow_name),
        .input_fixture = std::string(*options.input_fixture),
        .run_id = to_owned_string(options.run_id),
    };
}

} // namespace

std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata,
                             std::ostream &err) {
    auto plan_result =
        ahfl::handoff::build_execution_plan(ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(err);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return std::nullopt;
    }

    return std::move(*plan_result.plan);
}

CliPipelineArtifacts::CliPipelineArtifacts(CliPipelineInputs inputs) : inputs_(inputs) {}

const ahfl::handoff::ExecutionPlan *CliPipelineArtifacts::execution_plan() {
    return execution_plan_.get([&] {
        return build_execution_plan_for_cli(inputs_.program, inputs_.metadata, inputs_.err);
    });
}

const ahfl::dry_run::DryRunRequest *CliPipelineArtifacts::dry_run_request() {
    return dry_run_request_.get([&]() -> std::optional<ahfl::dry_run::DryRunRequest> {
        const auto *plan = execution_plan();
        if (plan == nullptr)
            return std::nullopt;
        return build_dry_run_request_for_cli(
            *plan, inputs_.options, inputs_.command_name, inputs_.err);
    });
}

const ahfl::dry_run::DryRunTrace *CliPipelineArtifacts::dry_run_trace() {
    return dry_run_trace_.get([&]() -> std::optional<ahfl::dry_run::DryRunTrace> {
        const auto *plan = execution_plan();
        const auto *request = dry_run_request();
        if (plan == nullptr || request == nullptr)
            return std::nullopt;
        auto dry_run = ahfl::dry_run::run_local_dry_run(*plan, *request, inputs_.mock_set);
        dry_run.diagnostics.render(inputs_.err);
        if (dry_run.has_errors() || !dry_run.trace.has_value())
            return std::nullopt;
        return std::move(*dry_run.trace);
    });
}

} // namespace ahfl::cli
