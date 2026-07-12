#include "runtime/engine/workflow_recovery.hpp"

#include "ahfl/runtime/execution_projection.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <fstream>
#include <set>
#include <sstream>
#include <string>

namespace ahfl::runtime {
namespace {

using ahfl::json::JsonValue;

[[nodiscard]] std::unique_ptr<JsonValue> id_json(std::size_t value) {
    return JsonValue::make_int(static_cast<std::int64_t>(value));
}

[[nodiscard]] std::optional<std::size_t>
non_negative_id(const JsonValue &object, std::string_view key) {
    const auto *field = object.get(key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto value = field->as_int();
    if (!value.has_value() || *value < 0) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(*value);
}

[[nodiscard]] std::string serialize_snapshot(const WorkflowRecoverySnapshot &snapshot) {
    auto root = JsonValue::make_object();
    root->set("schema", JsonValue::make_string(std::string(kWorkflowRecoverySchema)));
    root->set("workflow_id", id_json(snapshot.workflow.index()));
    root->set("checkpoint_id", id_json(snapshot.checkpoint.index()));
    auto nodes = JsonValue::make_array();
    for (const auto &node : snapshot.completed_nodes) {
        auto item = JsonValue::make_object();
        item->set("node_id", id_json(node.node.index()));
        item->set("agent_id", id_json(node.agent.index()));
        if (node.output.has_value()) {
            auto output = ahfl::json::parse_json(evaluator::value_to_json(*node.output));
            if (!output.has_value() || !*output) {
                return {};
            }
            item->set("output", std::move(*output));
        } else {
            item->set("output", JsonValue::make_null());
        }
        nodes->push(std::move(item));
    }
    root->set("completed_nodes", std::move(nodes));
    return ahfl::json::serialize_json(*root);
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.good() && !input.eof()) {
        return std::nullopt;
    }
    return content.str();
}

} // namespace

WorkflowRecoveryStore::WorkflowRecoveryStore(std::filesystem::path path)
    : path_(std::move(path)) {}

const std::filesystem::path &WorkflowRecoveryStore::path() const noexcept {
    return path_;
}

std::expected<void, WorkflowRecoveryError>
WorkflowRecoveryStore::save(const WorkflowRecoverySnapshot &snapshot,
                            const support::AtomicReplaceOptions &options) const {
    if (!snapshot.workflow.valid() || !snapshot.checkpoint.valid()) {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    }
    std::set<WorkflowNodeId> nodes;
    for (const auto &node : snapshot.completed_nodes) {
        if (!node.node.valid() || !node.agent.valid() || !nodes.insert(node.node).second) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
    }
    const auto content = serialize_snapshot(snapshot);
    if (content.empty()) {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    }
    if (!support::atomic_replace_text(path_, content, options).has_value()) {
        return std::unexpected(WorkflowRecoveryError::WriteFailed);
    }
    return {};
}

std::expected<WorkflowRecoverySnapshot, WorkflowRecoveryError>
WorkflowRecoveryStore::load() const {
    if (!std::filesystem::exists(path_)) {
        return std::unexpected(WorkflowRecoveryError::Missing);
    }
    const auto content = read_text(path_);
    if (!content.has_value()) {
        return std::unexpected(WorkflowRecoveryError::ReadFailed);
    }
    auto parsed = ahfl::json::parse_json(*content);
    if (!parsed.has_value() || !*parsed || !(*parsed)->is_object()) {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    }
    const auto *schema = (*parsed)->get("schema");
    const auto workflow = non_negative_id(**parsed, "workflow_id");
    const auto checkpoint = non_negative_id(**parsed, "checkpoint_id");
    const auto *nodes = (*parsed)->get("completed_nodes");
    if (schema == nullptr || schema->as_string() != kWorkflowRecoverySchema ||
        !workflow.has_value() || !checkpoint.has_value() || nodes == nullptr ||
        !nodes->is_array()) {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    }

    WorkflowRecoverySnapshot snapshot{
        .workflow = WorkflowId{*workflow},
        .checkpoint = CheckpointId{*checkpoint},
    };
    std::set<WorkflowNodeId> seen;
    for (const auto &item : nodes->array_items) {
        if (!item || !item->is_object()) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
        const auto node_id = non_negative_id(*item, "node_id");
        const auto agent_id = non_negative_id(*item, "agent_id");
        const auto *output = item->get("output");
        if (!node_id.has_value() || !agent_id.has_value() || output == nullptr) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
        RecoveredNodeState state{
            .node = WorkflowNodeId{*node_id},
            .agent = AgentId{*agent_id},
        };
        if (output->kind != ahfl::json::Kind::Null) {
            const auto output_text = ahfl::json::serialize_json(*output);
            auto value = evaluator::value_from_json(output_text);
            if (!value.has_value()) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            state.output = std::move(*value);
        }
        if (!seen.insert(state.node).second) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
        snapshot.completed_nodes.push_back(std::move(state));
    }
    return snapshot;
}

std::expected<WorkflowRecoverySnapshot, WorkflowRecoveryError>
materialize_workflow_recovery_snapshot(const WorkflowResult &result,
                                       const ExecutionCheckpointProjection &checkpoint) {
    if (!checkpoint.workflow.valid() || !checkpoint.checkpoint.valid() ||
        checkpoint.workflow != result.report.workflow) {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    }

    WorkflowRecoverySnapshot snapshot{
        .workflow = checkpoint.workflow,
        .checkpoint = checkpoint.checkpoint,
    };
    std::set<WorkflowNodeId> seen;
    for (const auto &node : checkpoint.completed_nodes) {
        if (!node.node.valid() || !node.agent.valid() || !seen.insert(node.node).second) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
        RecoveredNodeState state{
            .node = node.node,
            .agent = node.agent,
        };
        if (node.output.has_value()) {
            const auto *value = result.value(*node.output);
            if (value == nullptr) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            state.output = evaluator::clone_value(*value);
        }
        snapshot.completed_nodes.push_back(std::move(state));
    }
    return snapshot;
}

} // namespace ahfl::runtime
