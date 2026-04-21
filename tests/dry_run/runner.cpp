#include "ahfl/dry_run/runner.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include "../common/test_support.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

[[nodiscard]] ahfl::handoff::PackageMetadata
make_project_workflow_value_flow_metadata() {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string(ahfl::handoff::kFormatVersion),
        .name = "workflow-value-flow",
        .version = "0.2.0",
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Workflow,
        .canonical_name = "app::main::ValueFlowWorkflow",
    };
    metadata.export_targets = {
        *metadata.entry_target,
        ahfl::handoff::ExecutableRef{
            .kind = ahfl::handoff::ExecutableKind::Agent,
            .canonical_name = "lib::agents::AliasAgent",
        },
    };
    metadata.capability_binding_keys.emplace("lib::agents::Echo", "runtime.echo");
    return metadata;
}

int run_local_dry_run_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto ir_program = ahfl::test_support::load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto mock_set = ahfl::dry_run::parse_capability_mock_set_json(R"({
  "format_version": "ahfl.capability-mocks.v0.6",
  "mocks": [
    {
      "binding_key": "runtime.echo",
      "result_fixture": "fixture.echo.ok",
      "invocation_label": "echo-1"
    }
  ]
})");
    if (mock_set.has_errors() || !mock_set.mock_set.has_value()) {
        mock_set.diagnostics.render(std::cout);
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(
        *plan.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = "run-001",
        },
        *mock_set.mock_set);
    if (dry_run.has_errors() || !dry_run.trace.has_value()) {
        dry_run.diagnostics.render(std::cout);
        return 1;
    }

    const auto &trace = *dry_run.trace;
    if (trace.format_version != ahfl::dry_run::kTraceFormatVersion ||
        trace.source_execution_plan_format_version != ahfl::handoff::kExecutionPlanFormatVersion ||
        !trace.source_package_identity.has_value() ||
        trace.source_package_identity->name != "workflow-value-flow" ||
        trace.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        trace.input_fixture != "fixture.request.basic" || !trace.run_id.has_value() ||
        *trace.run_id != "run-001" || trace.execution_order.size() != 2 ||
        trace.execution_order[0] != "first" || trace.execution_order[1] != "second" ||
        trace.node_traces.size() != 2 || trace.node_traces[0].execution_index != 0 ||
        trace.node_traces[1].execution_index != 1 ||
        !trace.node_traces[0].satisfied_dependencies.empty() ||
        trace.node_traces[1].satisfied_dependencies.size() != 1 ||
        trace.node_traces[1].satisfied_dependencies.front() != "first" ||
        trace.node_traces[0].capability_bindings.size() != 1 ||
        trace.node_traces[0].capability_bindings.front().binding_key != "runtime.echo" ||
        trace.node_traces[0].mock_results.size() != 1 ||
        trace.node_traces[0].mock_results.front().binding_key != "runtime.echo" ||
        trace.node_traces[0].mock_results.front().result_fixture != "fixture.echo.ok" ||
        trace.return_summary.reads.size() != 1 ||
        trace.return_summary.reads.front().root_name != "second") {
        std::cerr << "unexpected dry-run trace\n";
        return 1;
    }

    return 0;
}

int run_local_dry_run_rejects_missing_workflow(const std::filesystem::path &project_descriptor) {
    const auto ir_program = ahfl::test_support::load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(
        *plan.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::MissingWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = std::nullopt,
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks = {
                ahfl::dry_run::CapabilityMock{
                    .capability_name = std::nullopt,
                    .binding_key = std::string("runtime.echo"),
                    .result_fixture = "fixture.echo.ok",
                    .invocation_label = std::nullopt,
                },
            },
        });
    if (!dry_run.has_errors()) {
        std::cerr << "expected missing workflow dry-run failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            dry_run.diagnostics,
            "local dry-run request workflow 'app::main::MissingWorkflow' does not exist in execution plan")) {
        dry_run.diagnostics.render(std::cout);
        std::cerr << "missing workflow dry-run diagnostic\n";
        return 1;
    }

    return 0;
}

int run_parse_capability_mock_set_ok(const std::filesystem::path &) {
    const auto result = ahfl::dry_run::parse_capability_mock_set_json(R"({
  "format_version": "ahfl.capability-mocks.v0.6",
  "mocks": [
    {
      "binding_key": "runtime.echo",
      "result_fixture": "fixture.echo.ok",
      "invocation_label": "echo-1"
    },
    {
      "capability_name": "lib::agents::Audit",
      "result_fixture": "fixture.audit.ok"
    }
  ]
})");
    if (result.has_errors() || !result.mock_set.has_value()) {
        result.diagnostics.render(std::cout);
        return 1;
    }

    const auto &mock_set = *result.mock_set;
    if (mock_set.format_version != ahfl::dry_run::kCapabilityMockSetFormatVersion ||
        mock_set.mocks.size() != 2 || !mock_set.mocks[0].binding_key.has_value() ||
        *mock_set.mocks[0].binding_key != "runtime.echo" ||
        mock_set.mocks[0].result_fixture != "fixture.echo.ok" ||
        !mock_set.mocks[0].invocation_label.has_value() ||
        *mock_set.mocks[0].invocation_label != "echo-1" ||
        !mock_set.mocks[1].capability_name.has_value() ||
        *mock_set.mocks[1].capability_name != "lib::agents::Audit") {
        std::cerr << "unexpected capability mock set\n";
        return 1;
    }

    return 0;
}

int run_parse_capability_mock_set_rejects_duplicate_selector(const std::filesystem::path &) {
    const auto result = ahfl::dry_run::parse_capability_mock_set_json(R"({
  "format_version": "ahfl.capability-mocks.v0.6",
  "mocks": [
    {
      "binding_key": "runtime.echo",
      "result_fixture": "fixture.echo.first"
    },
    {
      "binding_key": "runtime.echo",
      "result_fixture": "fixture.echo.second"
    }
  ]
})");
    if (!result.has_errors()) {
        std::cerr << "expected duplicate selector parse failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            result.diagnostics,
            "capability mock set contains duplicate mock selector 'binding:runtime.echo'")) {
        result.diagnostics.render(std::cout);
        std::cerr << "missing duplicate selector diagnostic\n";
        return 1;
    }

    return 0;
}

int run_local_dry_run_rejects_missing_mock(const std::filesystem::path &project_descriptor) {
    const auto ir_program = ahfl::test_support::load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(
        *plan.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = std::nullopt,
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks = {},
        });
    if (!dry_run.has_errors()) {
        std::cerr << "expected missing mock dry-run failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            dry_run.diagnostics,
            "local dry-run missing capability mock for binding key 'runtime.echo' capability 'lib::agents::Echo'")) {
        dry_run.diagnostics.render(std::cout);
        std::cerr << "missing missing-mock diagnostic\n";
        return 1;
    }

    return 0;
}

int run_local_dry_run_rejects_unused_mock(const std::filesystem::path &project_descriptor) {
    const auto ir_program = ahfl::test_support::load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(
        *plan.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = std::nullopt,
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = "fixture.echo.ok",
                        .invocation_label = std::nullopt,
                    },
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.unused"),
                        .result_fixture = "fixture.unused",
                        .invocation_label = std::nullopt,
                    },
                },
        });
    if (!dry_run.has_errors()) {
        std::cerr << "expected unused mock dry-run failure\n";
        return 1;
    }

    if (!ahfl::test_support::diagnostics_contain(
            dry_run.diagnostics,
            "capability mock set contains unused mock selector 'binding:runtime.unused'")) {
        dry_run.diagnostics.render(std::cout);
        std::cerr << "missing unused-mock diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: dry_run_runner <case> <project-descriptor>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path project_descriptor = argv[2];

    if (test_case == "local-dry-run-project-workflow-value-flow") {
        return run_local_dry_run_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "local-dry-run-rejects-missing-workflow") {
        return run_local_dry_run_rejects_missing_workflow(project_descriptor);
    }

    if (test_case == "parse-capability-mock-set-ok") {
        return run_parse_capability_mock_set_ok(project_descriptor);
    }

    if (test_case == "parse-capability-mock-set-rejects-duplicate-selector") {
        return run_parse_capability_mock_set_rejects_duplicate_selector(project_descriptor);
    }

    if (test_case == "local-dry-run-rejects-missing-mock") {
        return run_local_dry_run_rejects_missing_mock(project_descriptor);
    }

    if (test_case == "local-dry-run-rejects-unused-mock") {
        return run_local_dry_run_rejects_unused_mock(project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
