#include "runtime/engine/workflow_recovery.hpp"

#include "ahfl/runtime/execution_projection.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/workflow_runtime.hpp"
#include "runtime/evaluator/value_json.hpp"

#include <cstdint>
#include <exception>
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
    // RFC 0022: a snapshot carrying a resume record is schema v2; a plain
    // completed-nodes checkpoint stays v1 so existing readers are unaffected.
    const bool is_v2 = snapshot.suspended.has_value();
    root->set("schema", JsonValue::make_string(std::string(
                            is_v2 ? kWorkflowRecoverySchemaV2 : kWorkflowRecoverySchema)));
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

    if (is_v2) {
        const auto &suspended = *snapshot.suspended;
        auto record = JsonValue::make_object();
        record->set("node_id", id_json(suspended.node.index()));
        record->set("agent_id", id_json(suspended.agent.index()));
        record->set("pending_cap_id", id_json(suspended.pending_cap_id));
        record->set("pending_ordinal", id_json(static_cast<std::size_t>(suspended.pending_ordinal)));
        if (suspended.node_input.has_value()) {
            auto input = ahfl::json::parse_json(evaluator::value_to_json(*suspended.node_input));
            if (!input.has_value() || !*input) {
                return {};
            }
            record->set("node_input", std::move(*input));
        } else {
            record->set("node_input", JsonValue::make_null());
        }
        auto memo = JsonValue::make_array();
        for (const auto &entry : suspended.memo) {
            auto memo_item = JsonValue::make_object();
            memo_item->set("ordinal", id_json(static_cast<std::size_t>(entry.ordinal)));
            memo_item->set("cap_id", id_json(entry.cap_id));
            memo_item->set("arg_hash",
                           JsonValue::make_string(std::to_string(entry.arg_hash)));
            auto value = ahfl::json::parse_json(evaluator::value_to_json(entry.result));
            if (!value.has_value() || !*value) {
                return {};
            }
            memo_item->set("result", std::move(*value));
            memo->push(std::move(memo_item));
        }
        record->set("memo", std::move(memo));
        root->set("suspended", std::move(record));
    }
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
    const auto schema_name = schema != nullptr ? schema->as_string() : std::string{};
    const bool schema_ok =
        schema_name == kWorkflowRecoverySchema || schema_name == kWorkflowRecoverySchemaV2;
    if (schema == nullptr || !schema_ok || !workflow.has_value() || !checkpoint.has_value() ||
        nodes == nullptr || !nodes->is_array()) {
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

    // RFC 0022: parse the v2 resume record when present.
    if (const auto *suspended = (*parsed)->get("suspended");
        suspended != nullptr && suspended->is_object()) {
        const auto node_id = non_negative_id(*suspended, "node_id");
        const auto agent_id = non_negative_id(*suspended, "agent_id");
        const auto pending_cap_id = non_negative_id(*suspended, "pending_cap_id");
        const auto pending_ordinal = non_negative_id(*suspended, "pending_ordinal");
        const auto *node_input = suspended->get("node_input");
        const auto *memo = suspended->get("memo");
        if (!node_id.has_value() || !agent_id.has_value() || !pending_cap_id.has_value() ||
            !pending_ordinal.has_value() || node_input == nullptr || memo == nullptr ||
            !memo->is_array()) {
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        }
        SuspendedNodeState record{
            .node = WorkflowNodeId{*node_id},
            .agent = AgentId{*agent_id},
            .pending_cap_id = *pending_cap_id,
            .pending_ordinal = static_cast<std::uint64_t>(*pending_ordinal),
        };
        if (node_input->kind != ahfl::json::Kind::Null) {
            auto value = evaluator::value_from_json(ahfl::json::serialize_json(*node_input));
            if (!value.has_value()) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            record.node_input = std::move(*value);
        }
        std::set<std::uint64_t> memo_ordinals;
        for (const auto &memo_item : memo->array_items) {
            if (!memo_item || !memo_item->is_object()) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            const auto ordinal = non_negative_id(*memo_item, "ordinal");
            const auto cap_id = non_negative_id(*memo_item, "cap_id");
            const auto *arg_hash = memo_item->get("arg_hash");
            const auto *value = memo_item->get("result");
            if (!ordinal.has_value() || !cap_id.has_value() || arg_hash == nullptr ||
                arg_hash->kind != ahfl::json::Kind::String || value == nullptr) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            std::uint64_t arg_hash_value = 0;
            try {
                arg_hash_value = std::stoull(arg_hash->string_val);
            } catch (const std::exception &) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            auto result_value = evaluator::value_from_json(ahfl::json::serialize_json(*value));
            if (!result_value.has_value()) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            const auto ordinal_value = static_cast<std::uint64_t>(*ordinal);
            if (!memo_ordinals.insert(ordinal_value).second) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            record.memo.push_back(CapabilityMemoEntry{
                .ordinal = ordinal_value,
                .cap_id = *cap_id,
                .arg_hash = arg_hash_value,
                .result = std::move(*result_value),
            });
        }
        snapshot.suspended = std::move(record);
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
