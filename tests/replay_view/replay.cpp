#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
build_runtime_session_with_mock(const ahfl::handoff::ExecutionPlan &plan,
                                std::string input_fixture,
                                std::string result_fixture,
                                std::string run_id) {
    const auto session = ahfl::runtime_session::build_runtime_session(
        plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = "app::main::ValueFlowWorkflow",
            .input_fixture = std::move(input_fixture),
            .run_id = std::move(run_id),
        },
        ahfl::dry_run::CapabilityMockSet{
            .format_version = std::string(ahfl::dry_run::kCapabilityMockSetFormatVersion),
            .mocks =
                {
                    ahfl::dry_run::CapabilityMock{
                        .capability_name = std::nullopt,
                        .binding_key = std::string("runtime.echo"),
                        .result_fixture = std::move(result_fixture),
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

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_valid_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.basic", "fixture.echo.ok", "run-001");
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_failed_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.failed", "fixture.echo.fail", "run-failed-001");
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_partial_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    return build_runtime_session_with_mock(
        plan, "fixture.request.partial", "fixture.echo.pending", "run-partial-001");
}

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_valid_execution_journal(const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = ahfl::execution_journal::build_execution_journal(session);
    if (journal.has_errors() || !journal.journal.has_value()) {
        journal.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *journal.journal;
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView>
build_replay_view_from_session(const ahfl::handoff::ExecutionPlan &plan,
                               const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = build_valid_execution_journal(session);
    if (!journal.has_value()) {
        return std::nullopt;
    }

    const auto replay = ahfl::replay_view::build_replay_view(plan, session, *journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *replay.replay;
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView>
build_valid_replay_view(const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return std::nullopt;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return std::nullopt;
    }

    const auto replay = ahfl::replay_view::build_replay_view(*plan, *session, *journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *replay.replay;
}

int run_build_replay_view_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = ahfl::replay_view::build_replay_view(*plan, *session, *journal);
    if (replay.has_errors() || !replay.replay.has_value()) {
        replay.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *replay.replay;
    if (value.format_version != ahfl::replay_view::kReplayViewFormatVersion ||
        value.source_execution_plan_format_version != ahfl::handoff::kExecutionPlanFormatVersion ||
        value.source_runtime_session_format_version !=
            ahfl::runtime_session::kRuntimeSessionFormatVersion ||
        value.source_execution_journal_format_version !=
            ahfl::execution_journal::kExecutionJournalFormatVersion ||
        !value.source_package_identity.has_value() ||
        value.source_package_identity->name != "workflow-value-flow" ||
        value.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        value.session_id != "run-001" || !value.run_id.has_value() ||
        *value.run_id != "run-001" || value.input_fixture != "fixture.request.basic" ||
        value.workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Completed ||
        value.replay_status != ahfl::replay_view::ReplayStatus::Consistent ||
        !value.consistency.plan_matches_session ||
        !value.consistency.session_matches_journal ||
        !value.consistency.journal_matches_execution_order ||
        value.execution_order.size() != 2 || value.execution_order[0] != "first" ||
        value.execution_order[1] != "second" || value.nodes.size() != 2 ||
        value.nodes[0].node_name != "first" || value.nodes[0].execution_index != 0 ||
        !value.nodes[0].planned_dependencies.empty() ||
        !value.nodes[0].satisfied_dependencies.empty() ||
        !value.nodes[0].saw_node_became_ready || !value.nodes[0].saw_node_started ||
        !value.nodes[0].saw_node_completed || value.nodes[0].used_mock_selectors.size() != 1 ||
        value.nodes[0].used_mock_selectors.front() != "binding:runtime.echo" ||
        value.nodes[1].node_name != "second" || value.nodes[1].execution_index != 1 ||
        value.nodes[1].planned_dependencies.size() != 1 ||
        value.nodes[1].planned_dependencies.front() != "first" ||
        value.nodes[1].satisfied_dependencies.size() != 1 ||
        value.nodes[1].satisfied_dependencies.front() != "first" ||
        !value.nodes[1].saw_node_became_ready || !value.nodes[1].saw_node_started ||
        !value.nodes[1].saw_node_completed) {
        std::cerr << "unexpected replay view\n";
        return 1;
    }

    return 0;
}

int run_validate_replay_view_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    const auto replay = build_valid_replay_view(project_descriptor);
    if (!replay.has_value()) {
        return 1;
    }

    const auto validation = ahfl::replay_view::validate_replay_view(*replay);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_build_replay_view_failed_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_failed_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    if (replay->workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Failed ||
        replay->replay_status != ahfl::replay_view::ReplayStatus::RuntimeFailed ||
        !replay->workflow_failure_summary.has_value() || replay->execution_order.size() != 1 ||
        replay->nodes.size() != 1 || replay->nodes[0].final_status !=
                                          ahfl::runtime_session::NodeSessionStatus::Failed ||
        !replay->nodes[0].saw_node_became_ready || !replay->nodes[0].saw_node_started ||
        replay->nodes[0].saw_node_completed || !replay->nodes[0].saw_node_failed ||
        !replay->nodes[0].failure_summary.has_value() ||
        replay->nodes[0].used_mock_selectors.size() != 1 ||
        replay->nodes[0].used_mock_selectors.front() != "binding:runtime.echo") {
        std::cerr << "unexpected failed replay view\n";
        return 1;
    }

    return 0;
}

int run_build_replay_view_partial_workflow(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_partial_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_from_session(*plan, *session);
    if (!replay.has_value()) {
        return 1;
    }

    if (replay->workflow_status != ahfl::runtime_session::WorkflowSessionStatus::Partial ||
        replay->replay_status != ahfl::replay_view::ReplayStatus::Partial ||
        replay->workflow_failure_summary.has_value() || !replay->execution_order.empty() ||
        !replay->nodes.empty()) {
        std::cerr << "unexpected partial replay view\n";
        return 1;
    }

    return 0;
}

int run_validate_replay_view_rejects_missing_completed_progression(
    const std::filesystem::path &project_descriptor) {
    auto replay = build_valid_replay_view(project_descriptor);
    if (!replay.has_value()) {
        return 1;
    }

    replay->nodes[1].saw_node_completed = false;

    const auto validation = ahfl::replay_view::validate_replay_view(*replay);
    if (!validation.has_errors()) {
        std::cerr << "expected missing completed progression replay validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "replay view validation node 'second' does not contain a complete ready/start/completed progression")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing completed progression replay validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_replay_view_rejects_execution_order_index_mismatch(
    const std::filesystem::path &project_descriptor) {
    auto replay = build_valid_replay_view(project_descriptor);
    if (!replay.has_value()) {
        return 1;
    }

    replay->nodes[1].execution_index = 0;

    const auto validation = ahfl::replay_view::validate_replay_view(*replay);
    if (!validation.has_errors()) {
        std::cerr << "expected execution_order index mismatch replay validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "replay view validation execution_order node 'second' index does not match node execution_index")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing execution_order index mismatch replay validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_replay_view_rejects_invalid_execution_journal(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    journal->events[1].kind = ahfl::execution_journal::ExecutionJournalEventKind::NodeStarted;

    const auto replay = ahfl::replay_view::build_replay_view(*plan, *session, *journal);
    if (!replay.has_errors()) {
        std::cerr << "expected invalid execution journal replay failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            replay.diagnostics,
            "execution journal validation node 'first' starts before 'node_became_ready'")) {
        replay.diagnostics.render(std::cout);
        std::cerr << "missing invalid execution journal replay diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_replay_view_rejects_session_journal_mismatch(
    const std::filesystem::path &project_descriptor) {
    const auto plan = load_project_plan(project_descriptor);
    if (!plan.has_value()) {
        return 1;
    }

    const auto session = build_valid_runtime_session(*plan);
    if (!session.has_value()) {
        return 1;
    }

    auto journal = build_valid_execution_journal(*session);
    if (!journal.has_value()) {
        return 1;
    }

    journal->session_id = "run-002";

    const auto replay = ahfl::replay_view::build_replay_view(*plan, *session, *journal);
    if (!replay.has_errors()) {
        std::cerr << "expected session/journal mismatch replay failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            replay.diagnostics,
            "replay view session_id does not match between runtime session and execution journal")) {
        replay.diagnostics.render(std::cout);
        std::cerr << "missing session/journal mismatch replay diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: replay_view_tests <case> <project-descriptor>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path project_descriptor = argv[2];

    if (test_case == "build-replay-view-project-workflow-value-flow") {
        return run_build_replay_view_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "build-replay-view-failed-workflow") {
        return run_build_replay_view_failed_workflow(project_descriptor);
    }

    if (test_case == "build-replay-view-partial-workflow") {
        return run_build_replay_view_partial_workflow(project_descriptor);
    }

    if (test_case == "validate-replay-view-project-workflow-value-flow") {
        return run_validate_replay_view_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "validate-replay-view-rejects-missing-completed-progression") {
        return run_validate_replay_view_rejects_missing_completed_progression(project_descriptor);
    }

    if (test_case == "validate-replay-view-rejects-execution-order-index-mismatch") {
        return run_validate_replay_view_rejects_execution_order_index_mismatch(project_descriptor);
    }

    if (test_case == "build-replay-view-rejects-invalid-execution-journal") {
        return run_build_replay_view_rejects_invalid_execution_journal(project_descriptor);
    }

    if (test_case == "build-replay-view-rejects-session-journal-mismatch") {
        return run_build_replay_view_rejects_session_journal_mismatch(project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
