#include "ahfl/dry_run/runner.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::dry_run {

namespace {

[[nodiscard]] const handoff::WorkflowPlan *
find_workflow_plan(const handoff::ExecutionPlan &plan, std::string_view workflow_canonical_name) {
    for (const auto &workflow : plan.workflows) {
        if (workflow.workflow_canonical_name == workflow_canonical_name) {
            return &workflow;
        }
    }

    return nullptr;
}

[[nodiscard]] std::string mock_selector_key(const CapabilityMock &mock) {
    if (mock.binding_key.has_value()) {
        return "binding:" + *mock.binding_key;
    }

    if (mock.capability_name.has_value()) {
        return "capability:" + *mock.capability_name;
    }

    return {};
}

[[nodiscard]] const CapabilityMock *
find_mock_for_binding(const CapabilityMockSet &mock_set,
                      const handoff::CapabilityBindingReference &binding) {
    for (const auto &mock : mock_set.mocks) {
        if (mock.binding_key.has_value() && *mock.binding_key == binding.binding_key) {
            return &mock;
        }
    }

    for (const auto &mock : mock_set.mocks) {
        if (mock.capability_name.has_value() && *mock.capability_name == binding.capability_name) {
            return &mock;
        }
    }

    return nullptr;
}

void validate_capability_mock_set(const CapabilityMockSet &mock_set,
                                  const handoff::WorkflowPlan &workflow,
                                  DiagnosticBag &diagnostics) {
    if (mock_set.format_version != kCapabilityMockSetFormatVersion) {
        diagnostics.error("capability mock set encountered unsupported format_version '" +
                          mock_set.format_version + "'");
    }

    std::unordered_set<std::string> seen_mock_selectors;
    for (const auto &mock : mock_set.mocks) {
        if (mock.capability_name.has_value() == mock.binding_key.has_value()) {
            diagnostics.error(
                "capability mock must specify exactly one of 'capability_name' or 'binding_key'");
            continue;
        }

        if (mock.result_fixture.empty()) {
            diagnostics.error("capability mock result_fixture must not be empty");
        }

        const auto selector = mock_selector_key(mock);
        if (!selector.empty() && !seen_mock_selectors.insert(selector).second) {
            diagnostics.error("capability mock set contains duplicate mock selector '" +
                              selector + "'");
        }
    }

    std::unordered_set<std::string> used_mock_selectors;
    for (const auto &node : workflow.nodes) {
        for (const auto &binding : node.capability_bindings) {
            const auto *mock = find_mock_for_binding(mock_set, binding);
            if (mock == nullptr) {
                diagnostics.error("local dry-run missing capability mock for binding key '" +
                                  binding.binding_key + "' capability '" +
                                  binding.capability_name + "'");
                continue;
            }

            used_mock_selectors.insert(mock_selector_key(*mock));
        }
    }

    for (const auto &mock : mock_set.mocks) {
        const auto selector = mock_selector_key(mock);
        if (!selector.empty() && !used_mock_selectors.contains(selector)) {
            diagnostics.error("capability mock set contains unused mock selector '" +
                              selector + "'");
        }
    }
}

} // namespace

DryRunResult run_local_dry_run(const handoff::ExecutionPlan &plan,
                               const DryRunRequest &request,
                               const CapabilityMockSet &mock_set) {
    DryRunResult result{
        .trace = std::nullopt,
        .diagnostics = {},
    };

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto *workflow = find_workflow_plan(plan, request.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error("local dry-run request workflow '" +
                                 request.workflow_canonical_name +
                                 "' does not exist in execution plan");
        return result;
    }

    validate_capability_mock_set(mock_set, *workflow, result.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    std::unordered_map<std::string, std::size_t> remaining_dependencies;
    for (const auto &node : workflow->nodes) {
        remaining_dependencies.emplace(node.name, node.after.size());
    }

    std::vector<const handoff::WorkflowNodePlan *> ready_nodes;
    ready_nodes.reserve(workflow->nodes.size());
    for (const auto &node : workflow->nodes) {
        if (node.after.empty()) {
            ready_nodes.push_back(&node);
        }
    }

    DryRunTrace trace{
        .format_version = std::string(kTraceFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = workflow->workflow_canonical_name,
        .status = DryRunStatus::Completed,
        .input_fixture = request.input_fixture,
        .run_id = request.run_id,
        .execution_order = {},
        .node_traces = {},
        .return_summary = workflow->return_summary,
    };
    trace.execution_order.reserve(workflow->nodes.size());
    trace.node_traces.reserve(workflow->nodes.size());

    std::unordered_set<std::string> executed_nodes;
    for (std::size_t ready_index = 0; ready_index < ready_nodes.size(); ++ready_index) {
        const auto *node = ready_nodes[ready_index];
        if (node == nullptr || executed_nodes.contains(node->name)) {
            continue;
        }

        executed_nodes.insert(node->name);
        trace.execution_order.push_back(node->name);
        std::vector<CapabilityMock> mock_results;
        mock_results.reserve(node->capability_bindings.size());
        for (const auto &binding : node->capability_bindings) {
            const auto *mock = find_mock_for_binding(mock_set, binding);
            if (mock != nullptr) {
                mock_results.push_back(*mock);
            }
        }

        trace.node_traces.push_back(DryRunNodeTrace{
            .node_name = node->name,
            .target = node->target,
            .execution_index = trace.node_traces.size(),
            .satisfied_dependencies = node->after,
            .lifecycle = node->lifecycle,
            .input_summary = node->input_summary,
            .capability_bindings = node->capability_bindings,
            .mock_results = std::move(mock_results),
        });

        for (const auto &candidate : workflow->nodes) {
            if (executed_nodes.contains(candidate.name)) {
                continue;
            }

            bool newly_satisfied = false;
            for (const auto &dependency : candidate.after) {
                if (dependency != node->name) {
                    continue;
                }

                auto &remaining = remaining_dependencies[candidate.name];
                if (remaining > 0U) {
                    remaining -= 1U;
                }
                newly_satisfied = true;
            }

            if (newly_satisfied && remaining_dependencies[candidate.name] == 0U) {
                ready_nodes.push_back(&candidate);
            }
        }
    }

    if (executed_nodes.size() != workflow->nodes.size()) {
        result.diagnostics.error("local dry-run could not schedule all workflow nodes for '" +
                                 workflow->workflow_canonical_name + "'");
        return result;
    }

    result.trace = std::move(trace);
    return result;
}

} // namespace ahfl::dry_run
