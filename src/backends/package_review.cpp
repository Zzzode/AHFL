#include "ahfl/backends/package_review.hpp"

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl {

namespace {

template <typename... Ts> struct Overloaded : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string executable_kind_name(handoff::ExecutableKind kind) {
    switch (kind) {
    case handoff::ExecutableKind::Agent:
        return "agent";
    case handoff::ExecutableKind::Workflow:
        return "workflow";
    }

    return "invalid";
}

[[nodiscard]] std::string workflow_node_start_condition_name(
    handoff::WorkflowNodeStartConditionKind kind) {
    switch (kind) {
    case handoff::WorkflowNodeStartConditionKind::Immediate:
        return "immediate";
    case handoff::WorkflowNodeStartConditionKind::AfterDependenciesCompleted:
        return "after_dependencies_completed";
    }

    return "invalid";
}

[[nodiscard]] std::string workflow_node_completion_condition_name(
    handoff::WorkflowNodeCompletionConditionKind kind) {
    switch (kind) {
    case handoff::WorkflowNodeCompletionConditionKind::TargetReachedFinalState:
        return "target_reached_final_state";
    }

    return "invalid";
}

[[nodiscard]] std::string policy_obligation_kind_name(handoff::PolicyObligationKind kind) {
    switch (kind) {
    case handoff::PolicyObligationKind::Requires:
        return "requires";
    case handoff::PolicyObligationKind::Ensures:
        return "ensures";
    case handoff::PolicyObligationKind::Invariant:
        return "invariant";
    case handoff::PolicyObligationKind::Forbid:
        return "forbid";
    case handoff::PolicyObligationKind::WorkflowSafety:
        return "workflow_safety";
    case handoff::PolicyObligationKind::WorkflowLiveness:
        return "workflow_liveness";
    }

    return "invalid";
}

[[nodiscard]] std::string workflow_value_read(const ir::WorkflowValueRead &read) {
    std::string result = read.kind == ir::WorkflowValueSourceKind::WorkflowInput
                             ? "workflow_input:"
                             : "workflow_node_output:";
    result += read.root_name;
    for (const auto &member : read.members) {
        result += ".";
        result += member;
    }
    return result;
}

void print_workflow_expr_summary(const ir::WorkflowExprSummary &summary,
                                 std::string_view label,
                                 int indent_level,
                                 std::ostream &out) {
    line(out, indent_level, std::string(label) + " {");
    line(out, indent_level + 1, "reads " + std::to_string(summary.reads.size()));
    for (const auto &read : summary.reads) {
        line(out, indent_level + 1, "- " + workflow_value_read(read));
    }
    line(out, indent_level, "}");
}

void print_package_reader_summary(const handoff::PackageReaderSummary &summary, std::ostream &out) {
    line(out, 0, "package_reader {");
    line(out, 1, "format " + summary.format_version);

    if (summary.identity.has_value()) {
        line(out,
             1,
             "identity " + summary.identity->name + "@" + summary.identity->version);
    } else {
        line(out, 1, "identity none");
    }

    if (summary.entry_target.has_value()) {
        line(out,
             1,
             "entry " + executable_kind_name(summary.entry_target->kind) + " " +
                 summary.entry_target->canonical_name);
    } else {
        line(out, 1, "entry none");
    }

    line(out, 1, "exports {");
    line(out, 2, "count " + std::to_string(summary.export_targets.size()));
    for (const auto &target : summary.export_targets) {
        line(out,
             2,
             "- " + executable_kind_name(target.kind) + " " + target.canonical_name);
    }
    line(out, 1, "}");

    line(out, 1, "capability_bindings {");
    line(out, 2, "count " + std::to_string(summary.capability_binding_slots.size()));
    for (const auto &slot : summary.capability_binding_slots) {
        line(out,
             2,
             "- " + slot.capability_name + " as " + slot.binding_key + " required_by " +
                 std::to_string(slot.required_by_targets.size()));
    }
    line(out, 1, "}");

    line(out, 1, "policy_obligations {");
    line(out, 2, "count " + std::to_string(summary.policy_obligations.size()));
    for (const auto &obligation : summary.policy_obligations) {
        line(out,
             2,
             "- " + executable_kind_name(obligation.owner_target.kind) + " " +
                 obligation.owner_target.canonical_name + " " +
                 policy_obligation_kind_name(obligation.kind) + " clause " +
                 std::to_string(obligation.clause_index) + " observations " +
                 std::to_string(obligation.observation_symbols.size()));
    }
    line(out, 1, "}");

    line(out, 1, "formal_observations {");
    line(out, 2, "count " + std::to_string(summary.formal_observations.total));
    line(out, 2, "called_capability " + std::to_string(summary.formal_observations.called_capability));
    line(out, 2, "embedded_bool_expr " + std::to_string(summary.formal_observations.embedded_bool_expr));
    line(out, 1, "}");
    line(out, 0, "}");
}

void print_workflow_review(const handoff::ExecutionPlannerBootstrap &bootstrap, std::ostream &out) {
    line(out, 1, "workflow " + bootstrap.workflow_canonical_name + " {");
    line(out, 2, "input_type " + bootstrap.input_type);
    line(out, 2, "output_type " + bootstrap.output_type);

    line(out, 2, "entry_nodes {");
    line(out, 3, "count " + std::to_string(bootstrap.entry_nodes.size()));
    for (const auto &entry_node : bootstrap.entry_nodes) {
        line(out, 3, "- " + entry_node);
    }
    line(out, 2, "}");

    line(out, 2, "dependencies {");
    line(out, 3, "count " + std::to_string(bootstrap.dependency_edges.size()));
    for (const auto &edge : bootstrap.dependency_edges) {
        line(out, 3, "- " + edge.from_node + " -> " + edge.to_node);
    }
    line(out, 2, "}");

    line(out, 2, "nodes {");
    line(out, 3, "count " + std::to_string(bootstrap.nodes.size()));
    for (const auto &node : bootstrap.nodes) {
        line(out, 3, "- " + node.name);
        line(out, 4, "target " + node.target);

        std::string after_line = "after";
        for (const auto &dependency : node.after) {
            after_line += after_line == "after" ? " " : ", ";
            after_line += dependency;
        }
        if (node.after.empty()) {
            after_line += " none";
        }
        line(out, 4, after_line);

        line(out,
             4,
             "lifecycle start=" +
                 workflow_node_start_condition_name(node.lifecycle.start_condition) +
                 " completion=" +
                 workflow_node_completion_condition_name(node.lifecycle.completion_condition) +
                 " latched=" + (node.lifecycle.completion_latched ? "true" : "false"));
        line(out, 4, "target_initial_state " + node.lifecycle.target_initial_state);

        std::string final_states = "target_final_states";
        for (const auto &state : node.lifecycle.target_final_states) {
            final_states += final_states == "target_final_states" ? " " : ", ";
            final_states += state;
        }
        if (node.lifecycle.target_final_states.empty()) {
            final_states += " none";
        }
        line(out, 4, final_states);

        print_workflow_expr_summary(node.input_summary, "input_summary", 4, out);
    }
    line(out, 2, "}");

    print_workflow_expr_summary(bootstrap.return_summary, "return_summary", 2, out);
    line(out, 1, "}");
}

void print_execution_planner_summary(const handoff::Package &package, std::ostream &out) {
    line(out, 0, "execution_planner {");

    std::size_t workflow_count = 0;
    for (const auto &target : package.executable_targets) {
        if (std::holds_alternative<handoff::WorkflowExecutable>(target)) {
            workflow_count += 1;
        }
    }
    line(out, 1, "workflows " + std::to_string(workflow_count));

    for (const auto &target : package.executable_targets) {
        if (const auto *workflow = std::get_if<handoff::WorkflowExecutable>(&target);
            workflow != nullptr) {
            const auto bootstrap =
                handoff::build_execution_planner_bootstrap(package, workflow->canonical_name);
            if (bootstrap.bootstrap.has_value()) {
                print_workflow_review(*bootstrap.bootstrap, out);
            }
        }
    }

    line(out, 0, "}");
}

} // namespace

void print_package_review(const handoff::Package &package, std::ostream &out) {
    out << "ahfl.package_review.v1\n";
    print_package_reader_summary(handoff::build_package_reader_summary(package).summary, out);
    print_execution_planner_summary(package, out);
}

void print_program_package_review(const ir::Program &program, std::ostream &out) {
    print_package_review(handoff::lower_package(program), out);
}

void print_program_package_review(const ir::Program &program,
                                  const handoff::PackageMetadata &metadata,
                                  std::ostream &out) {
    print_package_review(handoff::lower_package(program, metadata), out);
}

} // namespace ahfl
