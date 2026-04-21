#include "ahfl/runtime_session/session.hpp"
#include "ahfl/validation/common.hpp"

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

[[nodiscard]] bool workflow_references_mock_selector(const handoff::WorkflowPlan &workflow,
                                                     const dry_run::CapabilityMock &mock) {
    for (const auto &node : workflow.nodes) {
        for (const auto &binding : node.capability_bindings) {
            if (mock.binding_key.has_value() && *mock.binding_key == binding.binding_key) {
                return true;
            }

            if (mock.capability_name.has_value() &&
                *mock.capability_name == binding.capability_name) {
                return true;
            }
        }
    }

    return false;
}

void validate_capability_mock_set(const dry_run::CapabilityMockSet &mock_set,
                                  const handoff::WorkflowPlan &workflow,
                                  DiagnosticBag &diagnostics) {
    if (mock_set.format_version != dry_run::kCapabilityMockSetFormatVersion) {
        diagnostics.error().message("runtime session encountered unsupported capability mock set format_version '" + mock_set.format_version + "'").emit();
    }

    std::unordered_set<std::string> seen_mock_selectors;
    for (const auto &mock : mock_set.mocks) {
        if (mock.capability_name.has_value() == mock.binding_key.has_value()) {
            diagnostics.error().message("runtime session capability mock must specify exactly one of 'capability_name' or 'binding_key'").emit();
            continue;
        }

        if (mock.result_fixture.empty()) {
            diagnostics.error().message("runtime session capability mock result_fixture must not be empty").emit();
        }

        const auto selector = mock_selector_key(mock);
        if (!selector.empty() && !seen_mock_selectors.insert(selector).second) {
            diagnostics.error().message("runtime session capability mock set contains duplicate mock selector '" + selector + "'").emit();
        }
        if (!workflow_references_mock_selector(workflow, mock)) {
            diagnostics.error().message("runtime session capability mock set contains unused mock selector '" + selector + "'").emit();
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

[[nodiscard]] bool node_status_is_executed(NodeSessionStatus status) {
    return status == NodeSessionStatus::Completed || status == NodeSessionStatus::Failed;
}

[[nodiscard]] bool workflow_status_is_terminal(WorkflowSessionStatus status) {
    return status == WorkflowSessionStatus::Completed || status == WorkflowSessionStatus::Failed;
}

[[nodiscard]] bool mock_result_fixture_requests_failure(std::string_view fixture) {
    return fixture.ends_with(".fail") || fixture.ends_with(".failed");
}

[[nodiscard]] bool mock_result_fixture_requests_partial(std::string_view fixture) {
    return fixture.ends_with(".pending") || fixture.ends_with(".partial");
}

void validate_failure_summary(const RuntimeFailureSummary &summary,
                              std::string_view owner_name,
                              DiagnosticBag &diagnostics) {
    validation::validate_failure_summary_owner(
        summary, owner_name, diagnostics, "runtime session validation");
}

} // namespace

RuntimeSessionValidationResult validate_runtime_session(const RuntimeSession &session) {
    RuntimeSessionValidationResult result{
        .diagnostics = {},
    };

    if (session.format_version != kRuntimeSessionFormatVersion) {
        result.diagnostics.error().message("runtime session validation encountered unsupported format_version '" + session.format_version + "'").emit();
    }

    if (session.source_execution_plan_format_version != handoff::kExecutionPlanFormatVersion) {
        result.diagnostics.error().message("runtime session validation encountered unsupported source_execution_plan_format_version '" + session.source_execution_plan_format_version + "'").emit();
    }

    if (session.workflow_canonical_name.empty()) {
        result.diagnostics.error().message("runtime session validation contains empty workflow_canonical_name").emit();
    }

    if (session.session_id.empty()) {
        result.diagnostics.error().message("runtime session validation contains empty session_id").emit();
    }

    if (session.input_fixture.empty()) {
        result.diagnostics.error().message("runtime session validation contains empty input_fixture").emit();
    }

    if (session.failure_summary.has_value()) {
        validate_failure_summary(*session.failure_summary, "workflow", result.diagnostics);
    }

    if (!session.options.sequential_mode) {
        result.diagnostics.error().message("runtime session validation currently requires sequential_mode=true").emit();
    }

    std::unordered_map<std::string, const RuntimeSessionNode *> nodes_by_name;
    std::unordered_set<std::string> execution_order_names;
    std::unordered_set<std::size_t> execution_indices;
    std::size_t failed_node_count = 0;
    std::size_t non_completed_node_count = 0;
    std::size_t skipped_node_count = 0;

    for (const auto &node_name : session.execution_order) {
        if (node_name.empty()) {
            result.diagnostics.error().message("runtime session validation execution_order contains empty node name").emit();
            continue;
        }

        if (!execution_order_names.insert(node_name).second) {
            result.diagnostics.error().message("runtime session validation execution_order contains duplicate node '" +
                node_name + "'").emit();
        }
    }

    for (const auto &node : session.nodes) {
        if (node.node_name.empty()) {
            result.diagnostics.error().message("runtime session validation contains node with empty node_name").emit();
            continue;
        }

        if (!nodes_by_name.emplace(node.node_name, &node).second) {
            result.diagnostics.error().message("runtime session validation contains duplicate node '" + node.node_name + "'").emit();
            continue;
        }

        if (node.target.empty()) {
            result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' has empty target").emit();
        }

        if (node.failure_summary.has_value()) {
            validate_failure_summary(
                *node.failure_summary, "node '" + node.node_name + "'", result.diagnostics);
            if (node.failure_summary->node_name.has_value() &&
                *node.failure_summary->node_name != node.node_name) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' failure summary node_name does not match node").emit();
            }
        }

        std::unordered_set<std::string> satisfied_dependencies;
        for (const auto &dependency : node.satisfied_dependencies) {
            if (dependency.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains empty satisfied dependency").emit();
                continue;
            }

            if (!satisfied_dependencies.insert(dependency).second) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains duplicate satisfied dependency '" + dependency + "'").emit();
            }
        }

        std::unordered_set<std::string> binding_keys;
        for (const auto &binding : node.capability_bindings) {
            if (binding.capability_name.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' has capability binding with empty capability name").emit();
            }

            if (binding.binding_key.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' has capability binding with empty binding key").emit();
            }

            const auto binding_ref = binding.capability_name + "#" + binding.binding_key;
            if (!binding_keys.insert(binding_ref).second) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains duplicate capability binding '" + binding_ref + "'").emit();
            }
        }

        std::unordered_set<std::string> mock_selectors;
        for (const auto &usage : node.used_mocks) {
            if (usage.selector.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains mock usage with empty selector").emit();
            } else if (!mock_selectors.insert(usage.selector).second) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains duplicate mock selector '" + usage.selector + "'").emit();
            }

            if (usage.result_fixture.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains mock usage with empty result_fixture").emit();
            }

            if (usage.capability_name.has_value() == usage.binding_key.has_value()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' mock usage must specify exactly one of 'capability_name' or 'binding_key'").emit();
                continue;
            }

            if (!is_binding_selector(usage) && !is_capability_selector(usage)) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains mock usage selector '" + usage.selector + "' inconsistent with binding reference fields").emit();
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
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' contains mock usage selector '" + usage.selector + "' not referenced by capability_bindings").emit();
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

            if (node.status == NodeSessionStatus::Completed && !matched_mock) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' missing used mock for binding key '" + binding.binding_key + "'").emit();
            }
        }

        switch (node.status) {
        case NodeSessionStatus::Completed:
            if (node.failure_summary.has_value()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' must not carry failure summary while status is Completed").emit();
            }
            if (!execution_indices.insert(node.execution_index).second) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' reuses execution_index " + std::to_string(node.execution_index)).emit();
            }

            if (node.execution_index >= session.execution_order.size()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' execution_index " + std::to_string(node.execution_index) + " is outside execution_order").emit();
                break;
            }

            if (session.execution_order[node.execution_index] != node.node_name) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' execution_index does not match execution_order").emit();
            }
            break;
        case NodeSessionStatus::Failed:
            ++failed_node_count;
            ++non_completed_node_count;
            if (!node.failure_summary.has_value()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' is Failed but has no failure summary").emit();
            } else if (node.failure_summary->kind == RuntimeFailureKind::WorkflowFailed) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' failure summary must not use WorkflowFailed kind").emit();
            }

            if (!execution_indices.insert(node.execution_index).second) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' reuses execution_index " + std::to_string(node.execution_index)).emit();
            }

            if (node.execution_index >= session.execution_order.size()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' execution_index " + std::to_string(node.execution_index) + " is outside execution_order").emit();
                break;
            }

            if (session.execution_order[node.execution_index] != node.node_name) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' execution_index does not match execution_order").emit();
            }
            break;
        case NodeSessionStatus::Blocked:
        case NodeSessionStatus::Ready:
            ++non_completed_node_count;
            if (node.failure_summary.has_value()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' must not carry failure summary unless status is Failed").emit();
            }
            if (!node.used_mocks.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' must not carry used_mocks unless node was executed").emit();
            }
            if (workflow_status_is_terminal(session.workflow_status)) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' is not executable-complete while workflow status is terminal").emit();
            }
            if (execution_order_names.contains(node.node_name)) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' appears in execution_order but is not executed").emit();
            }
            break;
        case NodeSessionStatus::Skipped:
            ++non_completed_node_count;
            ++skipped_node_count;
            if (node.failure_summary.has_value()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' must not carry failure summary unless status is Failed").emit();
            }
            if (!node.used_mocks.empty()) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' must not carry used_mocks unless node was executed").emit();
            }
            if (session.workflow_status != WorkflowSessionStatus::Failed) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' is Skipped but workflow status is not Failed").emit();
            }
            if (execution_order_names.contains(node.node_name)) {
                result.diagnostics.error().message("runtime session validation node '" + node.node_name + "' appears in execution_order but is not executed").emit();
            }
            break;
        }
    }

    for (std::size_t index = 0; index < session.execution_order.size(); ++index) {
        const auto &node_name = session.execution_order[index];
        const auto found = nodes_by_name.find(node_name);
        if (found == nodes_by_name.end()) {
            result.diagnostics.error().message("runtime session validation execution_order references unknown node '" + node_name + "'").emit();
            continue;
        }

        if (!node_status_is_executed(found->second->status)) {
            result.diagnostics.error().message("runtime session validation execution_order node '" + node_name + "' is not executed").emit();
        }
        if (found->second->execution_index != index) {
            result.diagnostics.error().message("runtime session validation execution_order node '" + node_name + "' index does not match node execution_index").emit();
        }
    }

    switch (session.workflow_status) {
    case WorkflowSessionStatus::Completed:
        if (session.failure_summary.has_value()) {
            result.diagnostics.error().message("runtime session validation completed workflow must not carry failure summary").emit();
        }
        if (session.execution_order.size() != session.nodes.size()) {
            result.diagnostics.error().message("runtime session validation completed workflow must include every node in execution_order").emit();
        }
        if (non_completed_node_count != 0) {
            result.diagnostics.error().message("runtime session validation completed workflow must not contain non-completed nodes").emit();
        }
        break;
    case WorkflowSessionStatus::Failed:
        if (!session.failure_summary.has_value()) {
            result.diagnostics.error().message("runtime session validation failed workflow must carry failure summary").emit();
        }
        if (failed_node_count == 0) {
            result.diagnostics.error().message("runtime session validation failed workflow must contain at least one Failed node").emit();
        }
        break;
    case WorkflowSessionStatus::Partial:
        if (failed_node_count != 0) {
            result.diagnostics.error().message("runtime session validation partial workflow must not contain Failed nodes").emit();
        }
        if (skipped_node_count != 0) {
            result.diagnostics.error().message("runtime session validation partial workflow must not contain Skipped nodes").emit();
        }
        if (non_completed_node_count == 0) {
            result.diagnostics.error().message("runtime session validation partial workflow must contain at least one non-completed node").emit();
        }
        break;
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
        result.diagnostics.error().message("runtime session bootstrap currently requires sequential_mode=true").emit();
        return result;
    }

    const auto plan_validation = handoff::validate_execution_plan(plan);
    result.diagnostics.append(plan_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto *workflow = find_workflow_plan(plan, request.workflow_canonical_name);
    if (workflow == nullptr) {
        result.diagnostics.error().message("runtime session request workflow '" + request.workflow_canonical_name + "' does not exist in execution plan").emit();
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
        .failure_summary = std::nullopt,
        .input_fixture = request.input_fixture,
        .options = options,
        .execution_order = {},
        .nodes = {},
        .return_summary = workflow->return_summary,
    };
    session.execution_order.reserve(workflow->nodes.size());
    session.nodes.reserve(workflow->nodes.size());

    std::unordered_set<std::string> executed_nodes;
    bool terminal_failure = false;
    bool partial_stop = false;
    for (std::size_t ready_index = 0; ready_index < ready_nodes.size(); ++ready_index) {
        const auto *node = ready_nodes[ready_index];
        if (node == nullptr || executed_nodes.contains(node->name)) {
            continue;
        }

        std::vector<RuntimeSessionMockUsage> used_mocks;
        used_mocks.reserve(node->capability_bindings.size());
        std::optional<RuntimeFailureSummary> node_failure_summary;
        bool requests_partial = false;
        for (const auto &binding : node->capability_bindings) {
            const auto *mock = find_mock_for_binding(mock_set, binding);
            if (mock == nullptr) {
                node_failure_summary = RuntimeFailureSummary{
                    .kind = RuntimeFailureKind::MockMissing,
                    .node_name = node->name,
                    .message = "runtime session node '" + node->name +
                               "' is missing capability mock for binding key '" +
                               binding.binding_key + "' capability '" +
                               binding.capability_name + "'",
                };
                break;
            }

            if (mock_result_fixture_requests_partial(mock->result_fixture)) {
                requests_partial = true;
                continue;
            }

            used_mocks.push_back(make_mock_usage(*mock));

            if (mock_result_fixture_requests_failure(mock->result_fixture)) {
                node_failure_summary = RuntimeFailureSummary{
                    .kind = RuntimeFailureKind::NodeFailed,
                    .node_name = node->name,
                    .message = "runtime session node '" + node->name +
                               "' failed with mock result_fixture '" +
                               mock->result_fixture + "'",
                };
                break;
            }
        }

        if (node_failure_summary.has_value()) {
            executed_nodes.insert(node->name);
            session.execution_order.push_back(node->name);
            session.nodes.push_back(RuntimeSessionNode{
                .node_name = node->name,
                .target = node->target,
                .status = NodeSessionStatus::Failed,
                .execution_index = session.execution_order.size() - 1U,
                .failure_summary = node_failure_summary,
                .satisfied_dependencies = node->after,
                .lifecycle = node->lifecycle,
                .input_summary = node->input_summary,
                .capability_bindings = node->capability_bindings,
                .used_mocks = std::move(used_mocks),
            });
            session.workflow_status = WorkflowSessionStatus::Failed;
            session.failure_summary = RuntimeFailureSummary{
                .kind = RuntimeFailureKind::WorkflowFailed,
                .node_name = node->name,
                .message = "runtime session workflow '" + workflow->workflow_canonical_name +
                           "' failed at node '" + node->name + "'",
            };
            terminal_failure = true;
            break;
        }

        if (requests_partial) {
            session.workflow_status = WorkflowSessionStatus::Partial;
            partial_stop = true;
            break;
        }

        executed_nodes.insert(node->name);
        session.execution_order.push_back(node->name);
        session.nodes.push_back(RuntimeSessionNode{
            .node_name = node->name,
            .target = node->target,
            .status = NodeSessionStatus::Completed,
            .execution_index = session.execution_order.size() - 1U,
            .failure_summary = std::nullopt,
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

    if (partial_stop || terminal_failure) {
        for (const auto &node : workflow->nodes) {
            if (executed_nodes.contains(node.name)) {
                continue;
            }

            std::vector<std::string> satisfied_dependencies;
            for (const auto &dependency : node.after) {
                if (executed_nodes.contains(dependency)) {
                    satisfied_dependencies.push_back(dependency);
                }
            }

            const auto node_status =
                terminal_failure
                    ? NodeSessionStatus::Skipped
                    : (remaining_dependencies[node.name] == 0U ? NodeSessionStatus::Ready
                                                               : NodeSessionStatus::Blocked);
            session.nodes.push_back(RuntimeSessionNode{
                .node_name = node.name,
                .target = node.target,
                .status = node_status,
                .execution_index = 0,
                .failure_summary = std::nullopt,
                .satisfied_dependencies = std::move(satisfied_dependencies),
                .lifecycle = node.lifecycle,
                .input_summary = node.input_summary,
                .capability_bindings = node.capability_bindings,
                .used_mocks = {},
            });
        }
    } else if (executed_nodes.size() != workflow->nodes.size()) {
        result.diagnostics.error().message("runtime session could not schedule all workflow nodes for '" + workflow->workflow_canonical_name + "'").emit();
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
