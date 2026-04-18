#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::optional<ahfl::ir::Program>
load_project_ir(const std::filesystem::path &project_descriptor) {
    const ahfl::Frontend frontend;

    const auto descriptor_result = frontend.load_project_descriptor(project_descriptor);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        descriptor_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const auto project_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = descriptor_result.descriptor->entry_files,
        .search_roots = descriptor_result.descriptor->search_roots,
    });
    if (project_result.has_errors()) {
        project_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(project_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(project_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(project_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return ahfl::lower_program_ir(project_result.graph, resolve_result, type_check_result);
}

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

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] ahfl::runtime_session::RuntimeFailureSummary
make_workflow_failure_summary(std::string message,
                              std::optional<std::string> node_name = std::nullopt) {
    return ahfl::runtime_session::RuntimeFailureSummary{
        .kind = ahfl::runtime_session::RuntimeFailureKind::WorkflowFailed,
        .node_name = std::move(node_name),
        .message = std::move(message),
    };
}

[[nodiscard]] ahfl::runtime_session::RuntimeFailureSummary
make_node_failure_summary(std::string message,
                          std::string node_name,
                          ahfl::runtime_session::RuntimeFailureKind kind =
                              ahfl::runtime_session::RuntimeFailureKind::NodeFailed) {
    return ahfl::runtime_session::RuntimeFailureSummary{
        .kind = kind,
        .node_name = std::move(node_name),
        .message = std::move(message),
    };
}

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
load_project_plan(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return std::nullopt;
    }

    const auto package =
        ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_valid_runtime_session(const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = "run-001",
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = "fixture.echo.ok",
                        .invocation_label = std::string("echo-1"),
                    },
                },
        });
    if (session.has_errors() || !session.session.has_value()) {
        session.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *session.session;
}

int run_build_runtime_session_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    const auto &value = *session;
    if (value.format_version != ahfl::runtime_session::kRuntimeSessionFormatVersion ||
        value.source_execution_plan_format_version != ahfl::handoff::kExecutionPlanFormatVersion ||
        !value.source_package_identity.has_value() ||
        value.source_package_identity->name != "workflow-value-flow" ||
        value.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        value.session_id != "run-001" || !value.run_id.has_value() ||
        *value.run_id != "run-001" || value.input_fixture != "fixture.request.basic" ||
        !value.options.sequential_mode ||
        value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.nodes.size() != 2 ||
        value.nodes[0].status != ahfl::runtime_session::NodeSessionStatus::Completed ||
        value.nodes[1].status != ahfl::runtime_session::NodeSessionStatus::Completed ||
        value.nodes[0].execution_index != 0 || value.nodes[1].execution_index != 1 ||
        !value.nodes[0].satisfied_dependencies.empty() ||
        value.nodes[1].satisfied_dependencies.size() != 1 ||
        value.nodes[1].satisfied_dependencies.front() != "first" ||
        value.nodes[0].used_mocks.size() != 1 ||
        value.nodes[0].used_mocks.front().selector != "binding:runtime.echo" ||
        value.nodes[0].used_mocks.front().binding_key != "runtime.echo" ||
        value.nodes[0].used_mocks.front().result_fixture != "fixture.echo.ok" ||
        !value.nodes[0].used_mocks.front().invocation_label.has_value() ||
        *value.nodes[0].used_mocks.front().invocation_label != "echo-1" ||
        value.return_summary.reads.size() != 1 ||
        value.return_summary.reads.front().root_name != "second") {
        std::cerr << "unexpected runtime session\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_build_runtime_session_rejects_missing_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::MissingWorkflow",
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
                },
        });
    if (!session.has_errors()) {
        std::cerr << "expected missing workflow runtime session failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            session.diagnostics,
            "runtime session request workflow 'app::main::MissingWorkflow' does not exist in execution plan")) {
        session.diagnostics.render(std::cout);
        std::cerr << "missing workflow runtime session diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_runtime_session_rejects_missing_mock(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.basic",
            .run_id = std::nullopt,
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks = {},
        });
    if (session.has_errors() || !session.session.has_value()) {
        session.diagnostics.render(std::cout);
        std::cerr << "expected failed runtime session for missing mock\n";
        return 1;
    }

    const auto &value = *session.session;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        !value.failure_summary.has_value() || value.execution_order.size() != 1 ||
        value.execution_order.front() != "first" || value.nodes.size() != 2 ||
        value.nodes[0].status != ahfl::runtime_session::NodeSessionStatus::Failed ||
        !value.nodes[0].failure_summary.has_value() ||
        value.nodes[0].failure_summary->kind !=
            ahfl::runtime_session::RuntimeFailureKind::MockMissing ||
        value.nodes[1].status != ahfl::runtime_session::NodeSessionStatus::Skipped) {
        std::cerr << "unexpected missing-mock failed runtime session\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_rejects_incomplete_completed_workflow(
    const std::filesystem::path &project_descriptor) {
    auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    session->nodes[1].status = ahfl::runtime_session::NodeSessionStatus::Blocked;

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (!validation.has_errors()) {
        std::cerr << "expected incomplete completed workflow validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "runtime session validation node 'second' is not executable-complete while workflow status is terminal")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing incomplete-workflow validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_rejects_missing_used_mock(
    const std::filesystem::path &project_descriptor) {
    auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    session->nodes[0].used_mocks.clear();

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (!validation.has_errors()) {
        std::cerr << "expected missing used-mock validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "runtime session validation node 'first' missing used mock for binding key 'runtime.echo'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing used-mock validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_accepts_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    session->workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Partial;
    session->execution_order.pop_back();
    session->nodes[1].status = ahfl::runtime_session::NodeSessionStatus::Ready;
    session->nodes[1].used_mocks.clear();

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        std::cerr << "expected valid partial workflow runtime session\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_accepts_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    session->workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    session->failure_summary =
        make_workflow_failure_summary("workflow failed after node terminal failure", "second");
    session->nodes[1].status = ahfl::runtime_session::NodeSessionStatus::Failed;
    session->nodes[1].failure_summary =
        make_node_failure_summary("node reached terminal failure state", "second");

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        std::cerr << "expected valid failed workflow runtime session\n";
        return 1;
    }

    return 0;
}

int run_validate_runtime_session_rejects_failed_without_failure_summary(
    const std::filesystem::path &project_descriptor) {
    auto session = build_valid_runtime_session(project_descriptor);
    if (!session.has_value()) {
        return 1;
    }

    session->workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Failed;
    session->nodes[1].status = ahfl::runtime_session::NodeSessionStatus::Failed;

    const auto validation = ahfl::runtime_session::validate_runtime_session(*session);
    if (!validation.has_errors()) {
        std::cerr << "expected failed workflow summary validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "runtime session validation failed workflow must carry failure summary") ||
        !diagnostics_contain(
            validation.diagnostics,
            "runtime session validation node 'second' is Failed but has no failure summary")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing failed workflow summary diagnostics\n";
        return 1;
    }

    return 0;
}

int run_build_runtime_session_partial_on_pending_mock(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.partial",
            .run_id = "run-partial-001",
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = "fixture.echo.pending",
                        .invocation_label = std::string("echo-pending"),
                    },
                },
        });
    if (session.has_errors() || !session.session.has_value()) {
        session.diagnostics.render(std::cout);
        std::cerr << "expected partial runtime session\n";
        return 1;
    }

    const auto &value = *session.session;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        value.failure_summary.has_value() || !value.execution_order.empty() ||
        value.nodes.size() != 2 ||
        value.nodes[0].status != ahfl::runtime_session::NodeSessionStatus::Ready ||
        value.nodes[0].execution_index != 0 || !value.nodes[0].used_mocks.empty() ||
        value.nodes[1].status != ahfl::runtime_session::NodeSessionStatus::Blocked ||
        value.nodes[1].execution_index != 0 || !value.nodes[1].used_mocks.empty()) {
        std::cerr << "unexpected partial runtime session\n";
        return 1;
    }

    return 0;
}

int run_build_runtime_session_failed_on_node_failure(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = "fixture.request.failed",
            .run_id = "run-failed-001",
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = "fixture.echo.fail",
                        .invocation_label = std::string("echo-fail"),
                    },
                },
        });
    if (session.has_errors() || !session.session.has_value()) {
        session.diagnostics.render(std::cout);
        std::cerr << "expected failed runtime session from node failure\n";
        return 1;
    }

    const auto &value = *session.session;
    if (value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        !value.failure_summary.has_value() || value.execution_order.size() != 1 ||
        value.nodes.size() != 2 ||
        value.nodes[0].status != ahfl::runtime_session::NodeSessionStatus::Failed ||
        !value.nodes[0].failure_summary.has_value() ||
        value.nodes[0].failure_summary->kind !=
            ahfl::runtime_session::RuntimeFailureKind::NodeFailed ||
        value.nodes[0].used_mocks.size() != 1 ||
        value.nodes[1].status != ahfl::runtime_session::NodeSessionStatus::Skipped) {
        std::cerr << "unexpected node-failed runtime session\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: runtime_session_tests <case> <project-descriptor>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path project_descriptor = argv[2];

    if (test_case == "build-runtime-session-project-workflow-value-flow") {
        return run_build_runtime_session_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "build-runtime-session-rejects-missing-workflow") {
        return run_build_runtime_session_rejects_missing_workflow(project_descriptor);
    }

    if (test_case == "build-runtime-session-rejects-missing-mock") {
        return run_build_runtime_session_rejects_missing_mock(project_descriptor);
    }

    if (test_case == "build-runtime-session-partial-on-pending-mock") {
        return run_build_runtime_session_partial_on_pending_mock(project_descriptor);
    }

    if (test_case == "build-runtime-session-failed-on-node-failure") {
        return run_build_runtime_session_failed_on_node_failure(project_descriptor);
    }

    if (test_case == "validate-runtime-session-project-workflow-value-flow") {
        return run_validate_runtime_session_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "validate-runtime-session-rejects-incomplete-completed-workflow") {
        return run_validate_runtime_session_rejects_incomplete_completed_workflow(
            project_descriptor);
    }

    if (test_case == "validate-runtime-session-rejects-missing-used-mock") {
        return run_validate_runtime_session_rejects_missing_used_mock(project_descriptor);
    }

    if (test_case == "validate-runtime-session-accepts-partial-workflow") {
        return run_validate_runtime_session_accepts_partial_workflow(project_descriptor);
    }

    if (test_case == "validate-runtime-session-accepts-failed-workflow") {
        return run_validate_runtime_session_accepts_failed_workflow(project_descriptor);
    }

    if (test_case == "validate-runtime-session-rejects-failed-without-failure-summary") {
        return run_validate_runtime_session_rejects_failed_without_failure_summary(
            project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
