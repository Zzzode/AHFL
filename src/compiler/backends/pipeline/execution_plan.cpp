#include "compiler/backends/pipeline/execution_plan.hpp"

#include <algorithm>
#include <cstddef>
#include <ostream>
#include <string_view>

#include "compiler/backends/pipeline/json_helpers.hpp"

namespace ahfl {

namespace {

class ExecutionPlanJsonPrinter final : private PipelineJsonHelpers {
  public:
    explicit ExecutionPlanJsonPrinter(std::ostream &out) : PipelineJsonHelpers(out) {}

    void print(const handoff::ExecutionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_package_identity", [&]() {
                if (plan.source_package_identity.has_value()) {
                    print_package_identity(*plan.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("entry_workflow_canonical_name", [&]() {
                if (plan.entry_workflow_canonical_name.has_value()) {
                    write_string(*plan.entry_workflow_canonical_name);
                    return;
                }
                out_ << "null";
            });
            field("workflows", [&]() {
                print_array(1, [&](const auto &item) {
                    for (const auto &workflow : plan.workflows) {
                        item([&]() { print_workflow_plan(workflow, 2); });
                    }
                });
            });
        });
        out_ << '\n';
    }

  private:
    void print_dependency_edge(const handoff::WorkflowDependencyEdge &edge, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("from_node", [&]() { write_string(edge.from_node); });
            field("to_node", [&]() { write_string(edge.to_node); });
        });
    }

    void print_workflow_node_plan(const handoff::WorkflowNodePlan &node, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("name", [&]() { write_string(node.name); });
            field("target", [&]() { write_string(node.target); });
            field("after", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &dependency : node.after) {
                        item([&]() { write_string(dependency); });
                    }
                });
            });
            field("lifecycle", [&]() { print_lifecycle(node.lifecycle, indent_level + 1); });
            field("input_summary",
                  [&]() { print_workflow_value_summary(node.input_summary, indent_level + 1); });
            field("capability_bindings", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &binding : node.capability_bindings) {
                        item([&]() { print_capability_binding_ref(binding, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_workflow_plan(const handoff::WorkflowPlan &workflow, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("workflow_canonical_name",
                  [&]() { write_string(workflow.workflow_canonical_name); });
            field("input_type", [&]() { write_string(workflow.input_type); });
            field("output_type", [&]() { write_string(workflow.output_type); });
            field("entry_nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &entry_node : workflow.entry_nodes) {
                        item([&]() { write_string(entry_node); });
                    }
                });
            });
            field("dependency_edges", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &edge : workflow.dependency_edges) {
                        item([&]() { print_dependency_edge(edge, indent_level + 2); });
                    }
                });
            });
            field("nodes", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &node : workflow.nodes) {
                        item([&]() { print_workflow_node_plan(node, indent_level + 2); });
                    }
                });
            });
            field("return_summary", [&]() {
                print_workflow_value_summary(workflow.return_summary, indent_level + 1);
            });
        });
    }
};

} // namespace

void print_execution_plan_json(const handoff::ExecutionPlan &plan, std::ostream &out) {
    ExecutionPlanJsonPrinter{out}.print(plan);
}

EmitResult print_program_execution_plan(const ir::Program &program, std::ostream &out) {
    const auto result = handoff::build_execution_plan(handoff::lower_package(program));
    if (!result.plan.has_value()) {
        return std::unexpected<std::string>("execution plan build failed");
    }

    print_execution_plan_json(*result.plan, out);
    return {};
}

EmitResult print_program_execution_plan(const ir::Program &program,
                                        const handoff::PackageMetadata &metadata,
                                        std::ostream &out) {
    const auto result = handoff::build_execution_plan(handoff::lower_package(program, metadata));
    if (!result.plan.has_value()) {
        return std::unexpected<std::string>("execution plan build failed");
    }

    print_execution_plan_json(*result.plan, out);
    return {};
}

} // namespace ahfl
