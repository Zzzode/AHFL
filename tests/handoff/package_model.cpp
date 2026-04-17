#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] bool has_dependency_edge(const ahfl::handoff::WorkflowExecutionGraph &graph,
                                       std::string_view from_node,
                                       std::string_view to_node) {
    for (const auto &edge : graph.dependency_edges) {
        if (edge.from_node == from_node && edge.to_node == to_node) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool has_required_by(const ahfl::handoff::CapabilityBindingSlot &slot,
                                   ahfl::handoff::ExecutableKind kind,
                                   std::string_view canonical_name) {
    for (const auto &target : slot.required_by_targets) {
        if (target.kind == kind && target.canonical_name == canonical_name) {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool has_observation_symbol(const ahfl::handoff::PolicyObligation &obligation,
                                          std::string_view symbol) {
    for (const auto &candidate : obligation.observation_symbols) {
        if (candidate == symbol) {
            return true;
        }
    }

    return false;
}

int run_project_workflow_value_flow(const std::filesystem::path &project_descriptor) {
    const ahfl::Frontend frontend;

    const auto descriptor_result = frontend.load_project_descriptor(project_descriptor);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        descriptor_result.diagnostics.render(std::cout);
        return 1;
    }

    const auto project_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = descriptor_result.descriptor->entry_files,
        .search_roots = descriptor_result.descriptor->search_roots,
    });
    if (project_result.has_errors()) {
        project_result.diagnostics.render(std::cout);
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(project_result.graph);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cout);
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(project_result.graph, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cout);
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(project_result.graph, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cout);
        return 1;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = "broken-version",
        .name = "workflow-value-flow",
        .version = "0.1.0",
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
    metadata.capability_binding_keys.emplace("lib::agents::Echo", "echo");

    const auto package = ahfl::handoff::lower_package(
        ahfl::lower_program_ir(project_result.graph, resolve_result, type_check_result),
        std::move(metadata));

    if (!package.metadata.identity.has_value() ||
        package.metadata.identity->format_version != ahfl::handoff::kFormatVersion ||
        package.metadata.identity->name != "workflow-value-flow" ||
        package.metadata.identity->version != "0.1.0") {
        std::cerr << "unexpected package identity\n";
        return 1;
    }

    if (!package.metadata.entry_target.has_value() ||
        package.metadata.entry_target->kind != ahfl::handoff::ExecutableKind::Workflow ||
        package.metadata.entry_target->canonical_name != "app::main::ValueFlowWorkflow" ||
        package.metadata.export_targets.size() != 2) {
        std::cerr << "unexpected entry/export targets\n";
        return 1;
    }

    const auto *agent = ahfl::handoff::find_agent_executable(package, "lib::agents::AliasAgent");
    if (agent == nullptr || agent->capabilities.size() != 1 ||
        agent->capabilities.front() != "lib::agents::Echo") {
        std::cerr << "unexpected agent executable\n";
        return 1;
    }

    const auto *workflow =
        ahfl::handoff::find_workflow_executable(package, "app::main::ValueFlowWorkflow");
    if (workflow == nullptr || workflow->nodes.size() != 2 ||
        workflow->execution_graph.entry_nodes.size() != 1 ||
        workflow->execution_graph.entry_nodes.front() != "first" ||
        !has_dependency_edge(workflow->execution_graph, "first", "second") ||
        workflow->nodes[1].after.size() != 1 || workflow->nodes[1].after.front() != "first" ||
        workflow->nodes[1].input_summary.reads.size() != 1 ||
        workflow->nodes[1].input_summary.reads.front().root_name != "first" ||
        workflow->nodes[0].lifecycle.start_condition !=
            ahfl::handoff::WorkflowNodeStartConditionKind::Immediate ||
        workflow->nodes[1].lifecycle.start_condition !=
            ahfl::handoff::WorkflowNodeStartConditionKind::AfterDependenciesCompleted ||
        workflow->nodes[0].lifecycle.completion_condition !=
            ahfl::handoff::WorkflowNodeCompletionConditionKind::TargetReachedFinalState ||
        workflow->nodes[0].lifecycle.target_initial_state != "Init" ||
        workflow->nodes[0].lifecycle.target_final_states.size() != 1 ||
        workflow->nodes[0].lifecycle.target_final_states.front() != "Done" ||
        !workflow->nodes[0].lifecycle.completion_latched ||
        workflow->return_summary.reads.size() != 1 ||
        workflow->return_summary.reads.front().root_name != "second") {
        std::cerr << "unexpected workflow executable\n";
        return 1;
    }

    const auto *slot = ahfl::handoff::find_capability_binding_slot(package, "lib::agents::Echo");
    if (slot == nullptr || slot->binding_key != "echo" ||
        !has_required_by(*slot, ahfl::handoff::ExecutableKind::Agent, "lib::agents::AliasAgent") ||
        !has_required_by(
            *slot, ahfl::handoff::ExecutableKind::Workflow, "app::main::ValueFlowWorkflow")) {
        std::cerr << "unexpected capability binding slot\n";
        return 1;
    }

    if (!package.policy_obligations.empty()) {
        std::cerr << "unexpected policy obligations\n";
        return 1;
    }

    return 0;
}

int run_file_expr_temporal(const std::filesystem::path &input_file) {
    const ahfl::Frontend frontend;

    auto parse_result = frontend.parse_file(input_file);
    if (parse_result.has_errors() || !parse_result.program) {
        parse_result.diagnostics.render(std::cout, std::cref(parse_result.source));
        return 1;
    }

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(*parse_result.program);
    if (resolve_result.has_errors()) {
        resolve_result.diagnostics.render(std::cout, std::cref(parse_result.source));
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    const auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    if (type_check_result.has_errors()) {
        type_check_result.diagnostics.render(std::cout, std::cref(parse_result.source));
        return 1;
    }

    const ahfl::Validator validator;
    const auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    if (validation_result.has_errors()) {
        validation_result.diagnostics.render(std::cout, std::cref(parse_result.source));
        return 1;
    }

    const auto package = ahfl::handoff::lower_package(
        ahfl::lower_program_ir(*parse_result.program, resolve_result, type_check_result));

    const auto *capability_slot =
        ahfl::handoff::find_capability_binding_slot(package, "ir::expr_temporal::Decide");
    if (capability_slot == nullptr ||
        !has_required_by(
            *capability_slot, ahfl::handoff::ExecutableKind::Agent, "ir::expr_temporal::ExprAgent") ||
        !has_required_by(*capability_slot,
                         ahfl::handoff::ExecutableKind::Workflow,
                         "ir::expr_temporal::ExprWorkflow") ||
        capability_slot->required_by_targets.size() != 2) {
        std::cerr << "unexpected capability dependency surface\n";
        return 1;
    }

    const auto *contract_requires = ahfl::handoff::find_policy_obligation(
        package,
        "ir::expr_temporal::ExprAgent",
        ahfl::handoff::PolicyObligationKind::Requires,
        0);
    const auto *contract_invariant = ahfl::handoff::find_policy_obligation(
        package,
        "ir::expr_temporal::ExprAgent",
        ahfl::handoff::PolicyObligationKind::Invariant,
        1);
    const auto *workflow_safety = ahfl::handoff::find_policy_obligation(
        package,
        "ir::expr_temporal::ExprWorkflow",
        ahfl::handoff::PolicyObligationKind::WorkflowSafety,
        0);
    const auto *workflow_liveness = ahfl::handoff::find_policy_obligation(
        package,
        "ir::expr_temporal::ExprWorkflow",
        ahfl::handoff::PolicyObligationKind::WorkflowLiveness,
        0);

    if (contract_requires == nullptr || contract_requires->observation_symbols.size() != 1 ||
        !has_observation_symbol(*contract_requires, "contract_ir_expr_temporal_ExprAgent_0_atom_0") ||
        contract_invariant == nullptr || contract_invariant->observation_symbols.size() != 1 ||
        !has_observation_symbol(*contract_invariant, "contract_ir_expr_temporal_ExprAgent_1_atom_0") ||
        workflow_safety == nullptr || !workflow_safety->observation_symbols.empty() ||
        workflow_liveness == nullptr || !workflow_liveness->observation_symbols.empty()) {
        std::cerr << "unexpected policy obligation surface\n";
        return 1;
    }

    const auto *workflow =
        ahfl::handoff::find_workflow_executable(package, "ir::expr_temporal::ExprWorkflow");
    if (workflow == nullptr || workflow->nodes.size() != 1 ||
        workflow->nodes.front().input_summary.reads.size() != 1 ||
        workflow->nodes.front().input_summary.reads.front().kind !=
            ahfl::ir::WorkflowValueSourceKind::WorkflowInput ||
        workflow->return_summary.reads.size() != 1 ||
        workflow->return_summary.reads.front().kind !=
            ahfl::ir::WorkflowValueSourceKind::WorkflowNodeOutput ||
        workflow->return_summary.reads.front().root_name != "run") {
        std::cerr << "unexpected workflow value summary surface\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: package_model <case> <project-descriptor>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    const std::filesystem::path project_descriptor = argv[2];

    if (test_case == "project-workflow-value-flow") {
        return run_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "file-expr-temporal") {
        return run_file_expr_temporal(project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
