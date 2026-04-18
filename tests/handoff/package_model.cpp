#include "ahfl/frontend/frontend.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
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

[[nodiscard]] std::optional<ahfl::handoff::Package>
load_project_package(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return std::nullopt;
    }

    return ahfl::handoff::lower_package(*ir_program, make_project_workflow_value_flow_metadata());
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

int run_package_reader_summary_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto summary = ahfl::handoff::build_package_reader_summary(*package);
    if (summary.has_errors()) {
        summary.diagnostics.render(std::cout);
        return 1;
    }

    if (!summary.summary.identity.has_value() ||
        summary.summary.identity->name != "workflow-value-flow" ||
        summary.summary.identity->version != "0.2.0" ||
        !summary.summary.entry_target.has_value() ||
        summary.summary.entry_target->canonical_name != "app::main::ValueFlowWorkflow" ||
        summary.summary.export_targets.size() != 2 ||
        summary.summary.capability_binding_slots.size() != 1 ||
        summary.summary.policy_obligations.size() != 0 ||
        summary.summary.formal_observations.total != 1 ||
        summary.summary.formal_observations.called_capability != 1 ||
        summary.summary.formal_observations.embedded_bool_expr != 0) {
        std::cerr << "unexpected package reader summary\n";
        return 1;
    }

    return 0;
}

int run_package_reader_summary_rejects_missing_export(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    package->metadata.export_targets.push_back(ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Workflow,
        .canonical_name = "missing::Workflow",
    });

    const auto summary = ahfl::handoff::build_package_reader_summary(*package);
    if (!summary.has_errors()) {
        std::cerr << "expected package reader summary failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            summary.diagnostics,
            "package reader summary export target 'missing::Workflow' does not exist")) {
        summary.diagnostics.render(std::cout);
        std::cerr << "missing package reader export diagnostic\n";
        return 1;
    }

    return 0;
}

int run_execution_planner_bootstrap_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto bootstrap = ahfl::handoff::build_entry_execution_planner_bootstrap(*package);
    if (bootstrap.has_errors() || !bootstrap.bootstrap.has_value()) {
        bootstrap.diagnostics.render(std::cout);
        return 1;
    }

    if (bootstrap.bootstrap->workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        bootstrap.bootstrap->entry_nodes.size() != 1 ||
        bootstrap.bootstrap->entry_nodes.front() != "first" ||
        bootstrap.bootstrap->dependency_edges.size() != 1 ||
        bootstrap.bootstrap->dependency_edges.front().from_node != "first" ||
        bootstrap.bootstrap->dependency_edges.front().to_node != "second" ||
        bootstrap.bootstrap->nodes.size() != 2 ||
        bootstrap.bootstrap->nodes.front().target != "lib::agents::AliasAgent" ||
        bootstrap.bootstrap->nodes.front().input_summary.reads.size() != 1 ||
        bootstrap.bootstrap->return_summary.reads.size() != 1) {
        std::cerr << "unexpected execution planner bootstrap\n";
        return 1;
    }

    return 0;
}

int run_execution_planner_bootstrap_rejects_agent_entry(
    const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    auto metadata = make_project_workflow_value_flow_metadata();
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Agent,
        .canonical_name = "lib::agents::AliasAgent",
    };
    const auto package = ahfl::handoff::lower_package(*ir_program, std::move(metadata));

    const auto bootstrap = ahfl::handoff::build_entry_execution_planner_bootstrap(package);
    if (!bootstrap.has_errors()) {
        std::cerr << "expected non-workflow entry bootstrap failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            bootstrap.diagnostics,
            "package entry target 'lib::agents::AliasAgent' is not a workflow target")) {
        bootstrap.diagnostics.render(std::cout);
        std::cerr << "missing non-workflow entry diagnostic\n";
        return 1;
    }

    return 0;
}

int run_execution_planner_bootstrap_rejects_missing_dependency(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    for (auto &target : package->executable_targets) {
        if (auto *workflow = std::get_if<ahfl::handoff::WorkflowExecutable>(&target);
            workflow != nullptr && workflow->canonical_name == "app::main::ValueFlowWorkflow") {
            workflow->execution_graph.dependency_edges.push_back(
                ahfl::handoff::WorkflowDependencyEdge{
                    .from_node = "missing",
                    .to_node = "second",
                });
            break;
        }
    }

    const auto bootstrap =
        ahfl::handoff::build_execution_planner_bootstrap(*package, "app::main::ValueFlowWorkflow");
    if (!bootstrap.has_errors()) {
        std::cerr << "expected missing-dependency bootstrap failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            bootstrap.diagnostics,
            "execution planner bootstrap dependency 'missing -> second' refers to unknown workflow node")) {
        bootstrap.diagnostics.render(std::cout);
        std::cerr << "missing dependency diagnostic\n";
        return 1;
    }

    return 0;
}

int run_execution_plan_project_workflow_value_flow(const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto plan = ahfl::handoff::build_execution_plan(*package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    if (plan.plan->format_version != ahfl::handoff::kExecutionPlanFormatVersion ||
        !plan.plan->source_package_identity.has_value() ||
        plan.plan->source_package_identity->name != "workflow-value-flow" ||
        plan.plan->source_package_identity->version != "0.2.0" ||
        !plan.plan->entry_workflow_canonical_name.has_value() ||
        *plan.plan->entry_workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        plan.plan->workflows.size() != 1) {
        std::cerr << "unexpected execution plan header\n";
        return 1;
    }

    const auto &workflow = plan.plan->workflows.front();
    if (workflow.workflow_canonical_name != "app::main::ValueFlowWorkflow" ||
        workflow.entry_nodes.size() != 1 || workflow.entry_nodes.front() != "first" ||
        workflow.dependency_edges.size() != 1 ||
        workflow.dependency_edges.front().from_node != "first" ||
        workflow.dependency_edges.front().to_node != "second" || workflow.nodes.size() != 2 ||
        workflow.return_summary.reads.size() != 1) {
        std::cerr << "unexpected execution workflow plan\n";
        return 1;
    }

    if (workflow.nodes.front().target != "lib::agents::AliasAgent" ||
        workflow.nodes.front().capability_bindings.size() != 1 ||
        workflow.nodes.front().capability_bindings.front().capability_name !=
            "lib::agents::Echo" ||
        workflow.nodes.front().capability_bindings.front().binding_key != "runtime.echo" ||
        workflow.nodes[1].after.size() != 1 || workflow.nodes[1].after.front() != "first" ||
        workflow.nodes[1].capability_bindings.size() != 1) {
        std::cerr << "unexpected execution node plan\n";
        return 1;
    }

    return 0;
}

int run_execution_plan_rejects_agent_entry(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    auto metadata = make_project_workflow_value_flow_metadata();
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Agent,
        .canonical_name = "lib::agents::AliasAgent",
    };

    const auto package = ahfl::handoff::lower_package(*ir_program, std::move(metadata));
    const auto plan = ahfl::handoff::build_execution_plan(package);
    if (!plan.has_errors()) {
        std::cerr << "expected execution plan failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            plan.diagnostics,
            "package entry target 'lib::agents::AliasAgent' is not a workflow target")) {
        plan.diagnostics.render(std::cout);
        std::cerr << "missing execution plan entry diagnostic\n";
        return 1;
    }

    if (plan.plan.has_value()) {
        std::cerr << "unexpected execution plan on failure\n";
        return 1;
    }

    return 0;
}

int run_validate_execution_plan_project_workflow_value_flow(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto plan = ahfl::handoff::build_execution_plan(*package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    const auto validation = ahfl::handoff::validate_execution_plan(*plan.plan);
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        std::cerr << "expected valid execution plan\n";
        return 1;
    }

    return 0;
}

int run_validate_execution_plan_rejects_missing_entry_workflow(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto plan = ahfl::handoff::build_execution_plan(*package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    auto invalid_plan = *plan.plan;
    invalid_plan.entry_workflow_canonical_name = "app::main::MissingWorkflow";

    const auto validation = ahfl::handoff::validate_execution_plan(invalid_plan);
    if (!validation.has_errors()) {
        std::cerr << "expected missing-entry-workflow validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(validation.diagnostics,
                             "execution plan validation entry workflow 'app::main::MissingWorkflow' does not exist in workflows")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing entry workflow validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_execution_plan_rejects_unknown_value_read(
    const std::filesystem::path &project_descriptor) {
    auto package = load_project_package(project_descriptor);
    if (!package.has_value()) {
        return 1;
    }

    const auto plan = ahfl::handoff::build_execution_plan(*package);
    if (plan.has_errors() || !plan.plan.has_value()) {
        plan.diagnostics.render(std::cout);
        return 1;
    }

    auto invalid_plan = *plan.plan;
    invalid_plan.workflows.front().nodes.back().input_summary.reads.front().root_name = "missing";

    const auto validation = ahfl::handoff::validate_execution_plan(invalid_plan);
    if (!validation.has_errors()) {
        std::cerr << "expected unknown-value-read validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "execution plan validation workflow 'app::main::ValueFlowWorkflow' node 'second' input_summary reads unknown workflow node output 'missing'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing unknown-value-read validation diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_package_normalizes_display_names(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Workflow,
        .canonical_name = "ValueFlowWorkflow",
    };
    metadata.export_targets = {
        *metadata.entry_target,
        ahfl::handoff::ExecutableRef{
            .kind = ahfl::handoff::ExecutableKind::Agent,
            .canonical_name = "AliasAgent",
        },
    };
    metadata.capability_binding_keys.emplace("Echo", "runtime.echo");

    const auto validation =
        ahfl::handoff::validate_package_metadata(*ir_program, std::move(metadata));
    if (validation.has_errors()) {
        validation.diagnostics.render(std::cout);
        return 1;
    }

    if (!validation.metadata.entry_target.has_value() ||
        validation.metadata.entry_target->canonical_name != "app::main::ValueFlowWorkflow") {
        std::cerr << "expected normalized workflow entry target\n";
        return 1;
    }

    if (validation.metadata.export_targets.size() != 2 ||
        validation.metadata.export_targets[0].canonical_name != "app::main::ValueFlowWorkflow" ||
        validation.metadata.export_targets[1].canonical_name != "lib::agents::AliasAgent") {
        std::cerr << "expected normalized export targets\n";
        return 1;
    }

    const auto capability_binding =
        validation.metadata.capability_binding_keys.find("lib::agents::Echo");
    if (capability_binding == validation.metadata.capability_binding_keys.end() ||
        capability_binding->second != "runtime.echo" ||
        validation.metadata.capability_binding_keys.size() != 1) {
        std::cerr << "expected normalized capability binding key\n";
        return 1;
    }

    return 0;
}

int run_validate_package_rejects_wrong_kind(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = ahfl::handoff::ExecutableKind::Agent,
        .canonical_name = "ValueFlowWorkflow",
    };

    const auto validation =
        ahfl::handoff::validate_package_metadata(*ir_program, std::move(metadata));
    if (!validation.has_errors()) {
        std::cerr << "expected wrong-kind package validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "field 'entry_target' refers to 'ValueFlowWorkflow' with wrong executable kind")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing wrong-kind diagnostic\n";
        return 1;
    }

    return 0;
}

int run_validate_package_rejects_duplicate_normalized_targets(
    const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.export_targets = {
        ahfl::handoff::ExecutableRef{
            .kind = ahfl::handoff::ExecutableKind::Workflow,
            .canonical_name = "app::main::ValueFlowWorkflow",
        },
        ahfl::handoff::ExecutableRef{
            .kind = ahfl::handoff::ExecutableKind::Workflow,
            .canonical_name = "ValueFlowWorkflow",
        },
    };
    metadata.capability_binding_keys.emplace("Echo", "runtime.echo.display");
    metadata.capability_binding_keys.emplace("lib::agents::Echo", "runtime.echo.canonical");

    const auto validation =
        ahfl::handoff::validate_package_metadata(*ir_program, std::move(metadata));
    if (!validation.has_errors()) {
        std::cerr << "expected duplicate-normalized package validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(
            validation.diagnostics,
            "export target 'app::main::ValueFlowWorkflow' is duplicated after semantic normalization") ||
        !diagnostics_contain(
            validation.diagnostics,
            "capability binding for 'lib::agents::Echo' is duplicated after semantic normalization")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing duplicate-normalized diagnostics\n";
        return 1;
    }

    return 0;
}

int run_validate_package_rejects_unknown_capability(const std::filesystem::path &project_descriptor) {
    const auto ir_program = load_project_ir(project_descriptor);
    if (!ir_program.has_value()) {
        return 1;
    }

    ahfl::handoff::PackageMetadata metadata;
    metadata.capability_binding_keys.emplace("MissingCapability", "runtime.missing");

    const auto validation =
        ahfl::handoff::validate_package_metadata(*ir_program, std::move(metadata));
    if (!validation.has_errors()) {
        std::cerr << "expected unknown-capability package validation failure\n";
        return 1;
    }

    if (!diagnostics_contain(validation.diagnostics,
                             "unknown package authoring capability 'MissingCapability'")) {
        validation.diagnostics.render(std::cout);
        std::cerr << "missing unknown-capability diagnostic\n";
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

    if (test_case == "validate-package-normalizes-display-names") {
        return run_validate_package_normalizes_display_names(project_descriptor);
    }

    if (test_case == "package-reader-summary-project-workflow-value-flow") {
        return run_package_reader_summary_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "package-reader-summary-rejects-missing-export") {
        return run_package_reader_summary_rejects_missing_export(project_descriptor);
    }

    if (test_case == "execution-planner-bootstrap-project-workflow-value-flow") {
        return run_execution_planner_bootstrap_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "execution-planner-bootstrap-rejects-agent-entry") {
        return run_execution_planner_bootstrap_rejects_agent_entry(project_descriptor);
    }

    if (test_case == "execution-planner-bootstrap-rejects-missing-dependency") {
        return run_execution_planner_bootstrap_rejects_missing_dependency(project_descriptor);
    }

    if (test_case == "execution-plan-project-workflow-value-flow") {
        return run_execution_plan_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "execution-plan-rejects-agent-entry") {
        return run_execution_plan_rejects_agent_entry(project_descriptor);
    }

    if (test_case == "validate-execution-plan-project-workflow-value-flow") {
        return run_validate_execution_plan_project_workflow_value_flow(project_descriptor);
    }

    if (test_case == "validate-execution-plan-rejects-missing-entry-workflow") {
        return run_validate_execution_plan_rejects_missing_entry_workflow(project_descriptor);
    }

    if (test_case == "validate-execution-plan-rejects-unknown-value-read") {
        return run_validate_execution_plan_rejects_unknown_value_read(project_descriptor);
    }

    if (test_case == "validate-package-rejects-wrong-kind") {
        return run_validate_package_rejects_wrong_kind(project_descriptor);
    }

    if (test_case == "validate-package-rejects-duplicate-normalized-targets") {
        return run_validate_package_rejects_duplicate_normalized_targets(project_descriptor);
    }

    if (test_case == "validate-package-rejects-unknown-capability") {
        return run_validate_package_rejects_unknown_capability(project_descriptor);
    }

    if (test_case == "file-expr-temporal") {
        return run_file_expr_temporal(project_descriptor);
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
