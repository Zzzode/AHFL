#include "ahfl/runtime_session/session.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl::runtime_session {

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

[[nodiscard]] std::string mock_selector_key(const dry_run::CapabilityMock &mock) {
    if (mock.binding_key.has_value()) {
        return "binding:" + *mock.binding_key;
    }

    if (mock.capability_name.has_value()) {
        return "capability:" + *mock.capability_name;
    }

    return {};
}

[[nodiscard]] const dry_run::CapabilityMock *
find_mock_for_binding(const dry_run::CapabilityMockSet &mock_set,
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

void validate_capability_mock_set(const dry_run::CapabilityMockSet &mock_set,
                                  const handoff::WorkflowPlan &workflow,
                                  DiagnosticBag &diagnostics) {
    if (mock_set.format_version != dry_run::kCapabilityMockSetFormatVersion) {
        diagnostics.error("runtime session encountered unsupported capability mock set format_version '" +
                          mock_set.format_version + "'");
    }

    std::unordered_set<std::string> seen_mock_selectors;
    for (const auto &mock : mock_set.mocks) {
        if (mock.capability_name.has_value() == mock.binding_key.has_value()) {
            diagnostics.error(
                "runtime session capability mock must specify exactly one of 'capability_name' or 'binding_key'");
            continue;
        }

        if (mock.result_fixture.empty()) {
            diagnostics.error("runtime session capability mock result_fixture must not be empty");
        }

        const auto selector = mock_selector_key(mock);
        if (!selector.empty() && !seen_mock_selectors.insert(selector).second) {
            diagnostics.error("runtime session capability mock set contains duplicate mock selector '" +
                              selector + "'");
        }
    }

    std::unordered_set<std::string> used_mock_selectors;
    for (const auto &node : workflow.nodes) {
        for (const auto &binding : node.capability_bindings) {
            const auto *mock = find_mock_for_binding(mock_set, binding);
            if (mock == nullptr) {
                diagnostics.error("runtime session missing capability mock for binding key '" +
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
            diagnostics.error("runtime session capability mock set contains unused mock selector '" +
                              selector + "'");
        }
    }
}

[[nodiscard]] RuntimeSessionMockUsage
make_mock_usage(const dry_run::CapabilityMock &mock) {
    return RuntimeSessionMockUsage{
        .selector = mock_selector_key(mock),
        .capability_name = mock.capability_name,
        .binding_key = mock.binding_key,
        .result_fixture = mock.result_fixture,
        .invocation_label = mock.invocation_label,
    };
}

[[nodiscard]] std::string default_session_id(const dry_run::DryRunRequest &request) {
    if (request.run_id.has_value()) {
        return *request.run_id;
    }

    return "runtime-session:" + request.workflow_canonical_name;
}

[[nodiscard]] bool is_binding_selector(const RuntimeSessionMockUsage &usage) {
    return usage.binding_key.has_value() && usage.selector == "binding:" + *usage.binding_key;
}

[[nodiscard]] bool is_capability_selector(const RuntimeSessionMockUsage &usage) {
    return usage.capability_name.has_value() &&
           usage.selector == "capability:" + *usage.capability_name;
}

[[nodiscard]] bool mock_usage_matches_binding(const RuntimeSessionMockUsage &usage,
                                              const handoff::CapabilityBindingReference &binding) {
    if (is_binding_selector(usage)) {
        return *usage.binding_key == binding.binding_key;
    }

    if (is_capability_selector(usage)) {
        return *usage.capability_name == binding.capability_name;
    }

    return false;
}

} // namespace

RuntimeSessionValidationResult validate_runtime_session(const RuntimeSession &session) {
    RuntimeSessionValidationResult result{
        .diagnostics = {},
    };

    if (session.format_version != kRuntimeSessionFormatVersion) {
        result.diagnostics.error(
            "runtime session validation encountered unsupported format_version '" +
            session.format_version + "'");
    }

    if (session.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error(
            "runtime session validation encountered unsupported source_execution_plan_format_version '" +
            session.source_execution_plan_format_version + "'");
    }

    if (session.workflow_canonical_name.empty()) {
        result.diagnostics.error(
            "runtime session validation contains empty workflow_canonical_name");
    }

    if (session.session_id.empty()) {
        result.diagnostics.error("runtime session validation contains empty session_id");
    }

    if (session.input_fixture.empty()) {
        result.diagnostics.error("runtime session validation contains empty input_fixture");
    }

    if (!session.options.sequential_mode) {
        result.diagnostics.error(
            "runtime session validation currently requires sequential_mode=true");
    }

    std::unordered_map<std::string, const RuntimeSessionNode *> nodes_by_name;
    std::unordered_set<std::string> execution_order_names;
    std::unordered_set<std::size_t> execution_indices;

    for (const auto &node_name : session.execution_order) {
        if (node_name.empty()) {
            result.diagnostics.error(
                "runtime session validation execution_order contains empty node name");
            continue;
        }

        if (!execution_order_names.insert(node_name).second) {
            result.diagnostics.error(
                "runtime session validation execution_order contains duplicate node '" +
                node_name + "'");
        }
    }

    for (const auto &node : session.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error(
                "runtime session validation contains node with empty node_name");
            continue;
        }

        if (!nodes_by_name.emplace(node.node_name, &node).second) {
            result.diagnostics.error("runtime session validation contains duplicate node '" +
                                     node.node_name + "'");
            continue;
        }

        if (node.target.empty()) {
            result.diagnostics.error("runtime session validation node '" + node.node_name +
                                     "' has empty target");
        }

        std::unordered_set<std::string> satisfied_dependencies;
        for (const auto &dependency : node.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains empty satisfied dependency");
                continue;
            }

            if (!satisfied_dependencies.insert(dependency).second) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains duplicate satisfied dependency '" +
                                         dependency + "'");
            }
        }

        std::unordered_set<std::string> binding_keys;
        for (const auto &binding : node.capability_bindings) {
            if (binding.capability_name.empty()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' has capability binding with empty capability name");
            }

            if (binding.binding_key.empty()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' has capability binding with empty binding key");
            }

            const auto binding_ref = binding.capability_name + "#" + binding.binding_key;
            if (!binding_keys.insert(binding_ref).second) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains duplicate capability binding '" +
                                         binding_ref + "'");
            }
        }

        std::unordered_set<std::string> mock_selectors;
        for (const auto &usage : node.used_mocks) {
            if (usage.selector.empty()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains mock usage with empty selector");
            } else if (!mock_selectors.insert(usage.selector).second) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains duplicate mock selector '" +
                                         usage.selector + "'");
            }

            if (usage.result_fixture.empty()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains mock usage with empty result_fixture");
            }

            if (usage.capability_name.has_value() == usage.binding_key.has_value()) {
                result.diagnostics.error(
                    "runtime session validation node '" + node.node_name +
                    "' mock usage must specify exactly one of 'capability_name' or 'binding_key'");
                continue;
            }

            if (!is_binding_selector(usage) && !is_capability_selector(usage)) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains mock usage selector '" + usage.selector +
                                         "' inconsistent with binding reference fields");
                continue;
            }

            bool matched_binding = false;
            for (const auto &binding : node.capability_bindings) {
                if (mock_usage_matches_binding(usage, binding)) {
                    matched_binding = true;
                    break;
                }
            }

            if (!matched_binding) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' contains mock usage selector '" + usage.selector +
                                         "' not referenced by capability_bindings");
            }
        }

        for (const auto &binding : node.capability_bindings) {
            bool matched_mock = false;
            for (const auto &usage : node.used_mocks) {
                if (mock_usage_matches_binding(usage, binding)) {
                    matched_mock = true;
                    break;
                }
            }

            if (!matched_mock) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' missing used mock for binding key '" +
                                         binding.binding_key + "'");
            }
        }

        switch (node.status) {
        case NodeSessionStatus::Completed:
            if (!execution_indices.insert(node.execution_index).second) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' reuses execution_index " +
                                         std::to_string(node.execution_index));
            }

            if (node.execution_index >= session.execution_order.size()) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' execution_index " +
                                         std::to_string(node.execution_index) +
                                         " is outside execution_order");
                break;
            }

            if (session.execution_order[node.execution_index] != node.node_name) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' execution_index does not match execution_order");
            }
            break;
        case NodeSessionStatus::Blocked:
        case NodeSessionStatus::Ready:
            if (session.workflow_status == WorkflowSessionStatus::Completed) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' is not completed while workflow status is Completed");
            }
            if (execution_order_names.contains(node.node_name)) {
                result.diagnostics.error("runtime session validation node '" + node.node_name +
                                         "' appears in execution_order but is not Completed");
            }
            break;
        }
    }

    for (std::size_t index = 0; index < session.execution_order.size(); ++index) {
        const auto &node_name = session.execution_order[index];
        const auto found = nodes_by_name.find(node_name);
        if (found == nodes_by_name.end()) {
            result.diagnostics.error("runtime session validation execution_order references unknown node '" +
                                     node_name + "'");
            continue;
        }

        if (found->second->status != NodeSessionStatus::Completed) {
            result.diagnostics.error("runtime session validation execution_order node '" +
                                     node_name + "' is not Completed");
        }
        if (found->second->execution_index != index) {
            result.diagnostics.error("runtime session validation execution_order node '" +
                                     node_name + "' index does not match node execution_index");
        }
    }

    if (session.workflow_status == WorkflowSessionStatus::Completed &&
        session.execution_order.size() != session.nodes.size()) {
        result.diagnostics.error(
            "runtime session validation completed workflow must include every node in execution_order");
    }

    return result;
}

RuntimeSessionResult build_runtime_session(const handoff::ExecutionPlan &plan,
                                           const dry_run::DryRunRequest &request,
                                           const dry_run::CapabilityMockSet &mock_set,
                                           const RuntimeSessionOptions &options) {
    RuntimeSessionResult result{
        .session = std::nullopt,
        .diagnostics = {},
    };

    if (!options.sequential_mode) {
        result.diagnostics.error(
            "runtime session bootstrap currently requires sequential_mode=true");
        return result;
    }

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto *workflow = find_workflow_plan(plan, request.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error("runtime session request workflow '" +
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

    RuntimeSession session{
        .format_version = std::string(kRuntimeSessionFormatVersion),
        .source_execution_plan_format_version = plan.format_version,
        .source_package_identity = plan.source_package_identity,
        .workflow_canonical_name = workflow->workflow_canonical_name,
        .session_id = default_session_id(request),
        .run_id = request.run_id,
        .workflow_status = WorkflowSessionStatus::Completed,
        .input_fixture = request.input_fixture,
        .options = options,
        .execution_order = {},
        .nodes = {},
        .return_summary = workflow->return_summary,
    };
    session.execution_order.reserve(workflow->nodes.size());
    session.nodes.reserve(workflow->nodes.size());

    std::unordered_set<std::string> executed_nodes;
    for (std::size_t ready_index = 0; ready_index < ready_nodes.size(); ++ready_index) {
        const auto *node = ready_nodes[ready_index];
        if (node == nullptr || executed_nodes.contains(node->name)) {
            continue;
        }

        executed_nodes.insert(node->name);
        session.execution_order.push_back(node->name);

        std::vector<RuntimeSessionMockUsage> used_mocks;
        used_mocks.reserve(node->capability_bindings.size());
        for (const auto &binding : node->capability_bindings) {
            const auto *mock = find_mock_for_binding(mock_set, binding);
            if (mock != nullptr) {
                used_mocks.push_back(make_mock_usage(*mock));
            }
        }

        session.nodes.push_back(RuntimeSessionNode{
            .node_name = node->name,
            .target = node->target,
            .status = NodeSessionStatus::Completed,
            .execution_index = session.nodes.size(),
            .satisfied_dependencies = node->after,
            .lifecycle = node->lifecycle,
            .input_summary = node->input_summary,
            .capability_bindings = node->capability_bindings,
            .used_mocks = std::move(used_mocks),
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
        result.diagnostics.error("runtime session could not schedule all workflow nodes for '" +
                                 workflow->workflow_canonical_name + "'");
        return result;
    }

    const auto validation = validate_runtime_session(session);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.session = std::move(session);
    return result;
}

} // namespace ahfl::runtime_session
