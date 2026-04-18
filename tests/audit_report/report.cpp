#include "ahfl/audit_report/report.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
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

namespace {

[[nodiscard]] bool diagnostics_contain(const ahfl::DiagnosticBag &diagnostics,
                                       std::string_view needle) {
    for (const auto &entry : diagnostics.entries()) {
        if (entry.message.find(needle) != std::string::npos) {
            return true;
        }
    }

    return false;
}

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
build_valid_runtime_session(const ahfl::handoff::ExecutionPlan &plan) {
    const auto session = ahfl::runtime_session::build_runtime_session(
        plan,
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

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_valid_execution_journal(const ahfl::runtime_session::RuntimeSession &session) {
    const auto journal = ahfl::execution_journal::build_execution_journal(session);
    if (journal.has_errors() || !journal.journal.has_value()) {
        journal.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *journal.journal;
}

[[nodiscard]] std::optional<ahfl::dry_run::DryRunTrace>
build_valid_dry_run_trace(const ahfl::handoff::ExecutionPlan &plan) {
    const auto trace = ahfl::dry_run::run_local_dry_run(
        plan,
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
    if (trace.has_errors() || !trace.trace.has_value()) {
        trace.diagnostics.render(std::cout);
        return std::nullopt;
    }

    return *trace.trace;
}

[[nodiscard]] ahfl::audit_report::AuditReport make_valid_audit_report() {
    return ahfl::audit_report::AuditReport{
        .format_version = std::string(ahfl::audit_report::kAuditReportFormatVersion),
        .source_package_identity =
            ahfl::handoff::PackageIdentity{
                .format_version = std::string(ahfl::handoff::kFormatVersion),
                .name = "workflow-value-flow",
                .version = "0.2.0",
            },
        .workflow_canonical_name = "app::main::ValueFlowWorkflow",
        .session_id = "run-001",
        .run_id = "run-001",
        .input_fixture = "fixture.request.basic",
        .conclusion = ahfl::audit_report::AuditConclusion::Passed,
        .plan_summary =
            ahfl::audit_report::AuditPlanSummary{
                .source_execution_plan_format_version =
                    std::string(ahfl::handoff::kExecutionPlanFormatVersion),
                .execution_order = {"first", "second"},
                .nodes =
                    {
                        ahfl::audit_report::AuditPlanNodeSummary{
                            .node_name = "first",
                            .target = "lib::agents::AliasAgent",
                            .planned_dependencies = {},
                            .capability_bindings =
                                {
                                    ahfl::handoff::CapabilityBindingReference{
                                        .capability_name = "lib::agents::Echo",
                                        .binding_key = "runtime.echo",
                                    },
                                },
                        },
                        ahfl::audit_report::AuditPlanNodeSummary{
                            .node_name = "second",
                            .target = "lib::agents::AliasAgent",
                            .planned_dependencies = {"first"},
                            .capability_bindings = {},
                        },
                    },
                .return_summary = {},
            },
        .session_summary =
            ahfl::audit_report::AuditSessionSummary{
                .source_runtime_session_format_version =
                    std::string(ahfl::runtime_session::kRuntimeSessionFormatVersion),
                .workflow_status = ahfl::runtime_session::WorkflowSessionStatus::Completed,
                .nodes =
                    {
                        ahfl::audit_report::AuditSessionNodeSummary{
                            .node_name = "first",
                            .final_status =
                                ahfl::runtime_session::NodeSessionStatus::Completed,
                            .execution_index = 0,
                            .satisfied_dependencies = {},
                            .used_mock_selectors = {"binding:runtime.echo"},
                        },
                        ahfl::audit_report::AuditSessionNodeSummary{
                            .node_name = "second",
                            .final_status =
                                ahfl::runtime_session::NodeSessionStatus::Completed,
                            .execution_index = 1,
                            .satisfied_dependencies = {"first"},
                            .used_mock_selectors = {},
                        },
                    },
            },
        .journal_summary =
            ahfl::audit_report::AuditJournalSummary{
                .source_execution_journal_format_version =
                    std::string(ahfl::execution_journal::kExecutionJournalFormatVersion),
                .total_events = 8,
                .node_ready_events = 2,
                .node_started_events = 2,
                .node_completed_events = 2,
                .workflow_completed_events = 1,
                .completed_node_order = {"first", "second"},
            },
        .trace_summary =
            ahfl::audit_report::AuditTraceSummary{
                .source_dry_run_trace_format_version =
                    std::string(ahfl::dry_run::kTraceFormatVersion),
                .status = ahfl::dry_run::DryRunStatus::Completed,
                .execution_order = {"first", "second"},
                .nodes =
                    {
                        ahfl::audit_report::AuditTraceNodeSummary{
                            .node_name = "first",
                            .execution_index = 0,
                            .mock_result_selectors = {"binding:runtime.echo"},
                        },
                        ahfl::audit_report::AuditTraceNodeSummary{
                            .node_name = "second",
                            .execution_index = 1,
                            .mock_result_selectors = {},
                        },
                    },
                .return_summary = {},
            },
        .replay_consistency =
            ahfl::replay_view::ReplayConsistencySummary{
                .plan_matches_session = true,
                .session_matches_journal = true,
                .journal_matches_execution_order = true,
            },
        .audit_consistency =
            ahfl::audit_report::AuditConsistencySummary{
                .plan_matches_session = true,
                .session_matches_journal = true,
                .journal_matches_trace = true,
                .trace_matches_replay = true,
            },
    };
}

int run_validate_audit_report_ok() {
    const auto report = make_valid_audit_report();
    const auto validation = ahfl::audit_report::validate_audit_report(report);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    return 0;
}

int run_validate_audit_report_rejects_unknown_session_node() {
    auto report = make_valid_audit_report();
    report.session_summary.nodes[1].node_name = "third";

    const auto validation = ahfl::audit_report::validate_audit_report(report);
    if (!validation.has_errors()) {
        std::cerr << "expected unknown session node audit validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "audit report validation session_summary node 'third' does not exist in plan_summary")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing unknown session node audit validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_audit_report_rejects_journal_order_mismatch() {
    auto report = make_valid_audit_report();
    report.journal_summary.completed_node_order = {"second", "first"};

    const auto validation = ahfl::audit_report::validate_audit_report(report);
    if (!validation.has_errors()) {
        std::cerr << "expected journal order mismatch audit validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "audit report validation journal_summary completed_node_order does not match plan_summary execution_order")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing journal order mismatch audit validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_audit_report_project_workflow_value_flow(
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

    const auto trace = build_valid_dry_run_trace(*plan);
    if (!trace.has_value()) {
        return 1;
    }

    const auto report =
        ahfl::audit_report::build_audit_report(*plan, *session, *journal, *trace);
    if (report.has_errors() || !report.report.has_value()) {
        report.diagnostics.render(std::cout);
        return 1;
    }

    const auto &value = *report.report;
    if (value.format_version != ahfl::audit_report::kAuditReportFormatVersion ||
        !value.source_package_identity.has_value() ||
        value.source_package_identity->name != "workflow-value-flow" ||
        value.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        value.session_id != "run-001" || !value.run_id.has_value() ||
        *value.run_id != "run-001" || value.input_fixture != "fixture.request.basic" ||
        value.conclusion != ahfl::audit_report::AuditConclusion::Passed ||
        value.plan_summary.execution_order.size() != 2 ||
        value.plan_summary.execution_order[0] != "first" ||
        value.session_summary.nodes.size() != 2 ||
        value.journal_summary.total_events != 8 ||
        value.journal_summary.completed_node_order.size() != 2 ||
        value.trace_summary.execution_order.size() != 2 ||
        !value.replay_consistency.plan_matches_session ||
        !value.audit_consistency.journal_matches_trace ||
        !value.audit_consistency.trace_matches_replay) {
        std::cerr << "unexpected audit report bootstrap result\n";
        return 1;
    }

    return 0;
}

int run_build_audit_report_rejects_trace_workflow_mismatch(
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

    auto trace = build_valid_dry_run_trace(*plan);
    if (!trace.has_value()) {
        return 1;
    }
    trace->workflow_canonical_name = "app::main::WrongWorkflow";

    const auto report =
        ahfl::audit_report::build_audit_report(*plan, *session, *journal, *trace);
    if (!report.has_errors()) {
        std::cerr << "expected trace workflow mismatch audit bootstrap failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            report.diagnostics,
            "audit report bootstrap dry-run trace workflow_canonical_name does not match runtime session")) {
        report.diagnostics.render(std::cout);
        std::cerr << "missing trace workflow mismatch audit bootstrap diagnostic\n";
        return 1;
    }

    return 0;
}

int run_build_audit_report_rejects_trace_execution_order_mismatch(
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

    auto trace = build_valid_dry_run_trace(*plan);
    if (!trace.has_value()) {
        return 1;
    }
    trace->execution_order = {"second", "first"};

    const auto report =
        ahfl::audit_report::build_audit_report(*plan, *session, *journal, *trace);
    if (!report.has_errors()) {
        std::cerr << "expected trace execution_order mismatch audit bootstrap failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            report.diagnostics,
            "audit report bootstrap dry-run trace execution_order does not match replay view execution_order")) {
        report.diagnostics.render(std::cout);
        std::cerr << "missing trace execution_order mismatch audit bootstrap diagnostic\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: audit_report_tests <case> [project-descriptor]\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::optional<std::filesystem::path> project_descriptor =
        argc >= 3 ? std::optional<std::filesystem::path>{argv[2]} : std::nullopt;

    if (test_case == "validate-audit-report-ok") {
        return run_validate_audit_report_ok();
    }

    if (test_case == "validate-audit-report-rejects-unknown-session-node") {
        return run_validate_audit_report_rejects_unknown_session_node();
    }

    if (test_case == "validate-audit-report-rejects-journal-order-mismatch") {
        return run_validate_audit_report_rejects_journal_order_mismatch();
    }

    if (test_case == "build-audit-report-project-workflow-value-flow") {
        if (!project_descriptor.has_value()) {
            std::cerr << "missing project descriptor for audit bootstrap case\n";
            return 2;
        }
        return run_build_audit_report_project_workflow_value_flow(*project_descriptor);
    }

    if (test_case == "build-audit-report-rejects-trace-workflow-mismatch") {
        if (!project_descriptor.has_value()) {
            std::cerr << "missing project descriptor for audit bootstrap case\n";
            return 2;
        }
        return run_build_audit_report_rejects_trace_workflow_mismatch(*project_descriptor);
    }

    if (test_case == "build-audit-report-rejects-trace-execution-order-mismatch") {
        if (!project_descriptor.has_value()) {
            std::cerr << "missing project descriptor for audit bootstrap case\n";
            return 2;
        }
        return run_build_audit_report_rejects_trace_execution_order_mismatch(*project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
