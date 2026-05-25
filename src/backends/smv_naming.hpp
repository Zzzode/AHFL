#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "ahfl/ir/ir.hpp"
#include "support/string_utils.hpp"

namespace ahfl::smv {

// --- Node state constants ---

constexpr std::string_view kNodeIdle = "AHFL_NODE_IDLE";
constexpr std::string_view kNodeRunning = "AHFL_NODE_RUNNING";
constexpr std::string_view kNodeCompleted = "AHFL_NODE_COMPLETED";
constexpr std::string_view kNodeFailed = "AHFL_NODE_FAILED";
constexpr std::string_view kNodeRecovering = "AHFL_NODE_RECOVERING";
constexpr std::string_view kNodeRecovered = "AHFL_NODE_RECOVERED";
constexpr std::string_view kNodeCompensating = "AHFL_NODE_COMPENSATING";
constexpr std::string_view kNodeCompensated = "AHFL_NODE_COMPENSATED";
constexpr std::string_view kNodeTerminalFailed = "AHFL_NODE_TERMINAL_FAILED";

// --- Operator rendering ---

[[nodiscard]] inline std::string smv_unary_op(ir::TemporalUnaryOp op) {
    switch (op) {
    case ir::TemporalUnaryOp::Always: return "G";
    case ir::TemporalUnaryOp::Eventually: return "F";
    case ir::TemporalUnaryOp::Next: return "X";
    case ir::TemporalUnaryOp::Not: return "!";
    }
    return "!";
}

[[nodiscard]] inline std::string smv_binary_op(ir::TemporalBinaryOp op) {
    switch (op) {
    case ir::TemporalBinaryOp::Implies: return "->";
    case ir::TemporalBinaryOp::Or: return "|";
    case ir::TemporalBinaryOp::And: return "&";
    case ir::TemporalBinaryOp::Until: return "U";
    }
    return "&";
}

// --- Variable naming ---

[[nodiscard]] inline std::string agent_state_var(const ir::AgentDecl &agent) {
    return "agent__" + sanitize_identifier(agent.name) + "__state";
}

[[nodiscard]] inline std::string workflow_node_state_var(const ir::WorkflowDecl &workflow,
                                                         std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__state";
}

[[nodiscard]] inline std::string workflow_node_phase_var(const ir::WorkflowDecl &workflow,
                                                         std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__phase";
}

[[nodiscard]] inline std::string workflow_node_completed_var(const ir::WorkflowDecl &workflow,
                                                             std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__completed";
}

[[nodiscard]] inline std::string workflow_node_started_var(const ir::WorkflowDecl &workflow,
                                                           std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__started";
}

[[nodiscard]] inline std::string workflow_node_running_var(const ir::WorkflowDecl &workflow,
                                                           std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__running";
}

[[nodiscard]] inline std::string workflow_node_failure_requested_var(const ir::WorkflowDecl &workflow,
                                                                     std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__failure_requested";
}

[[nodiscard]] inline std::string workflow_node_failed_var(const ir::WorkflowDecl &workflow,
                                                          std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__failed";
}

[[nodiscard]] inline std::string workflow_node_recovering_var(const ir::WorkflowDecl &workflow,
                                                              std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__recovering";
}

[[nodiscard]] inline std::string workflow_node_recovered_var(const ir::WorkflowDecl &workflow,
                                                             std::string_view node_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__recovered";
}

[[nodiscard]] inline std::string workflow_node_call_event_var(const ir::WorkflowDecl &workflow,
                                                              std::string_view node_name,
                                                              std::string_view capability_name) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__call__" + sanitize_identifier(capability_name);
}

[[nodiscard]] inline std::string workflow_node_effect_event_var(const ir::WorkflowDecl &workflow,
                                                                std::string_view node_name,
                                                                std::string_view effect_kind) {
    return "workflow__" + sanitize_identifier(workflow.name) + "__node__" +
           sanitize_identifier(node_name) + "__effect__" + sanitize_identifier(effect_kind);
}

[[nodiscard]] inline std::string workflow_node_call_committed_event_var(
    const ir::WorkflowDecl &workflow, std::string_view node_name, std::string_view capability_name) {
    return workflow_node_call_event_var(workflow, node_name, capability_name) + "__committed";
}

[[nodiscard]] inline std::string workflow_node_call_failed_event_var(
    const ir::WorkflowDecl &workflow, std::string_view node_name, std::string_view capability_name) {
    return workflow_node_call_event_var(workflow, node_name, capability_name) + "__failed";
}

[[nodiscard]] inline std::string workflow_node_effect_committed_event_var(
    const ir::WorkflowDecl &workflow, std::string_view node_name, std::string_view effect_kind) {
    return workflow_node_effect_event_var(workflow, node_name, effect_kind) + "__committed";
}

[[nodiscard]] inline std::string workflow_node_effect_failed_event_var(
    const ir::WorkflowDecl &workflow, std::string_view node_name, std::string_view effect_kind) {
    return workflow_node_effect_event_var(workflow, node_name, effect_kind) + "__failed";
}

[[nodiscard]] inline std::string clause_atom_name(std::string_view base_name, std::size_t index) {
    return sanitize_identifier(std::string(base_name) + "__atom__" + std::to_string(index));
}

[[nodiscard]] inline std::string observation_scope_kind_key(ir::FormalObservationScopeKind kind) {
    switch (kind) {
    case ir::FormalObservationScopeKind::ContractClause: return "contract";
    case ir::FormalObservationScopeKind::WorkflowSafetyClause: return "workflow_safety";
    case ir::FormalObservationScopeKind::WorkflowLivenessClause: return "workflow_liveness";
    }
    return "invalid";
}

[[nodiscard]] inline std::string capability_effect_kind_name(ir::CapabilityEffectKind kind) {
    switch (kind) {
    case ir::CapabilityEffectKind::Read: return "read";
    case ir::CapabilityEffectKind::ExternalSideEffect: return "external_side_effect";
    case ir::CapabilityEffectKind::DurableWrite: return "durable_write";
    case ir::CapabilityEffectKind::FinancialWrite: return "financial_write";
    case ir::CapabilityEffectKind::Unknown: return "unknown";
    }
    return "unknown";
}

} // namespace ahfl::smv
