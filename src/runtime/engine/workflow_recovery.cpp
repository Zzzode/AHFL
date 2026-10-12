#include "runtime/engine/workflow_recovery.hpp"

#include "ahfl/runtime/execution_projection.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/workflow_result.hpp"
#include "runtime/value/value_json.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <variant>

namespace ahfl::runtime {
namespace {

using ahfl::json::JsonValue;

[[nodiscard]] std::unique_ptr<JsonValue> id_json(std::uint64_t value) {
    auto json = JsonValue::make_int(0);
    if (value <= static_cast<std::uint64_t>(INT64_MAX))
        json->int_val = static_cast<std::int64_t>(value);
    else {
        json->number_provenance = ahfl::json::NumberProvenance::UnsignedInteger;
        json->uint_val = value;
    }
    return json;
}

[[nodiscard]] std::optional<std::size_t> non_negative_id(const JsonValue &object,
                                                         std::string_view key) {
    const auto *field = object.get(key);
    const auto value = field ? field->as_uint() : std::nullopt;
    if (!value || *value > std::numeric_limits<std::size_t>::max())
        return std::nullopt;
    return static_cast<std::size_t>(*value);
}

[[nodiscard]] bool exact_fields(const JsonValue &object,
                                std::initializer_list<std::string_view> fields) {
    if (!object.is_object() || object.object_fields.size() != fields.size())
        return false;
    for (const auto field : fields)
        if (!object.get(field))
            return false;
    return true;
}

[[nodiscard]] std::optional<StoredMemoResult> stored_result(const MemoResult &result) {
    if (const auto *stored = std::get_if<StoredMemoResult>(&result)) {
        auto parsed = ahfl::json::parse_json(stored->result_wire_json);
        if (!parsed || !*parsed || (!stored->present && (*parsed)->kind != ahfl::json::Kind::Null))
            return std::nullopt;
        return *stored;
    }
    const auto &memory = std::get<InMemoryMemoResult>(result);
    auto wire = runtime::try_value_to_json(memory.value);
    if (!wire)
        return std::nullopt;
    const bool none = std::holds_alternative<runtime::NoneValue>(memory.value.node);
    const bool unit = std::holds_alternative<runtime::UnitValue>(memory.value.node);
    if ((memory.present && none) || (!memory.present && (!(none || unit) || *wire != "null")))
        return std::nullopt;
    return StoredMemoResult{std::move(*wire), memory.present};
}

[[nodiscard]] bool write_memo_result(const CapabilityMemoEntry &entry, JsonValue &memo_item) {
    auto result = stored_result(entry.result);
    if (!result)
        return false;
    memo_item.set("result_wire_json", JsonValue::make_string(result->result_wire_json));
    memo_item.set("result_present", JsonValue::make_bool(result->present));
    return true;
}

[[nodiscard]] std::string serialize_snapshot(const WorkflowRecoverySnapshot &snapshot) {
    auto root = JsonValue::make_object();
    root->set("format", JsonValue::make_string(std::string(kWorkflowRecoveryFormat)));
    root->set("workflow_id", id_json(snapshot.workflow.index()));
    root->set("checkpoint_id", id_json(snapshot.checkpoint.index()));
    auto nodes = JsonValue::make_array();
    for (const auto &node : snapshot.completed_nodes) {
        auto item = JsonValue::make_object();
        item->set("node_id", id_json(node.node.index()));
        item->set("agent_id", id_json(node.agent.index()));
        if (node.output.has_value()) {
            auto output_json = runtime::try_value_to_json(*node.output);
            if (!output_json.has_value()) {
                return {};
            }
            auto output = ahfl::json::parse_json(*output_json);
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

    if (snapshot.suspended) {
        const auto &suspended = *snapshot.suspended;
        auto record = JsonValue::make_object();
        record->set("node_id", id_json(suspended.node.index()));
        record->set("agent_id", id_json(suspended.agent.index()));
        record->set("pending_cap_id", id_json(suspended.pending_cap_id));
        record->set("pending_ordinal",
                    id_json(static_cast<std::size_t>(suspended.pending_ordinal)));
        if (suspended.node_input.has_value()) {
            auto input_json = runtime::try_value_to_json(*suspended.node_input);
            if (!input_json.has_value()) {
                return {};
            }
            auto input = ahfl::json::parse_json(*input_json);
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
            memo_item->set("arg_hash", JsonValue::make_string(std::to_string(entry.arg_hash)));
            memo_item->set("node_id", id_json(entry.node.index()));
            if (!write_memo_result(entry, *memo_item)) {
                return {};
            }
            memo->push(std::move(memo_item));
        }
        record->set("memo", std::move(memo));
        root->set("suspended", std::move(record));
    } else {
        root->set("suspended", JsonValue::make_null());
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

WorkflowRecoveryStore::WorkflowRecoveryStore(std::filesystem::path path) : path_(std::move(path)) {}

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
    if (snapshot.suspended) {
        const auto &pending = *snapshot.suspended;
        if (!pending.node.valid() || !pending.agent.valid())
            return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
        std::set<std::pair<WorkflowNodeId, std::uint64_t>> coordinates;
        for (const auto &entry : pending.memo)
            if (!entry.node.valid() || !coordinates.emplace(entry.node, entry.ordinal).second ||
                (entry.node == pending.node && entry.ordinal >= pending.pending_ordinal))
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
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

std::expected<WorkflowRecoverySnapshot, WorkflowRecoveryError> WorkflowRecoveryStore::load() const {
    std::error_code status_error;
    const bool exists = std::filesystem::exists(path_, status_error);
    if (status_error) {
        return std::unexpected(WorkflowRecoveryError::ReadFailed);
    }
    if (!exists) {
        return std::unexpected(WorkflowRecoveryError::Missing);
    }
    const auto content = read_text(path_);
    if (!content.has_value()) {
        return std::unexpected(WorkflowRecoveryError::ReadFailed);
    }
    auto parsed = ahfl::json::parse_json(*content);
    const auto invalid = [] {
        return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
    };
    if (!parsed || !*parsed ||
        !exact_fields(**parsed,
                      {"format", "workflow_id", "checkpoint_id", "completed_nodes", "suspended"}))
        return invalid();
    const auto *format = (*parsed)->get("format");
    const auto workflow = non_negative_id(**parsed, "workflow_id");
    const auto checkpoint = non_negative_id(**parsed, "checkpoint_id");
    const auto *nodes = (*parsed)->get("completed_nodes");
    if (format->as_string() != kWorkflowRecoveryFormat || !workflow || !checkpoint ||
        !nodes->is_array())
        return invalid();
    WorkflowRecoverySnapshot snapshot{.workflow = WorkflowId{*workflow},
                                      .checkpoint = CheckpointId{*checkpoint}};
    if (!snapshot.workflow.valid() || !snapshot.checkpoint.valid())
        return invalid();
    std::set<WorkflowNodeId> seen;
    for (const auto &item : nodes->array_items) {
        if (!item || !exact_fields(*item, {"node_id", "agent_id", "output"}))
            return invalid();
        const auto node_id = non_negative_id(*item, "node_id");
        const auto agent_id = non_negative_id(*item, "agent_id");
        if (!node_id || !agent_id)
            return invalid();
        RecoveredNodeState state{.node = WorkflowNodeId{*node_id}, .agent = AgentId{*agent_id}};
        if (!state.node.valid() || !state.agent.valid() || !seen.insert(state.node).second)
            return invalid();
        const auto *output = item->get("output");
        if (output->kind != ahfl::json::Kind::Null) {
            auto value = runtime::value_from_json(*output);
            if (!value)
                return invalid();
            state.output = std::move(*value);
        }
        snapshot.completed_nodes.push_back(std::move(state));
    }
    const auto *suspended = (*parsed)->get("suspended");
    if (suspended->kind == ahfl::json::Kind::Null)
        return snapshot;
    if (!exact_fields(
            *suspended,
            {"node_id", "agent_id", "pending_cap_id", "pending_ordinal", "node_input", "memo"}))
        return invalid();
    const auto node_id = non_negative_id(*suspended, "node_id");
    const auto agent_id = non_negative_id(*suspended, "agent_id");
    const auto pending_cap_id = non_negative_id(*suspended, "pending_cap_id");
    const auto pending_ordinal = non_negative_id(*suspended, "pending_ordinal");
    const auto *memo = suspended->get("memo");
    if (!node_id || !agent_id || !pending_cap_id || !pending_ordinal || !memo->is_array())
        return invalid();
    SuspendedNodeState record{.node = WorkflowNodeId{*node_id},
                              .agent = AgentId{*agent_id},
                              .pending_cap_id = *pending_cap_id,
                              .pending_ordinal = *pending_ordinal};
    if (!record.node.valid() || !record.agent.valid())
        return invalid();
    const auto *input = suspended->get("node_input");
    if (input->kind != ahfl::json::Kind::Null) {
        auto value = runtime::value_from_json(*input);
        if (!value)
            return invalid();
        record.node_input = std::move(*value);
    }
    std::set<std::pair<WorkflowNodeId, std::uint64_t>> coordinates;
    for (const auto &item : memo->array_items) {
        if (!item ||
            !exact_fields(
                *item,
                {"ordinal", "cap_id", "arg_hash", "node_id", "result_wire_json", "result_present"}))
            return invalid();
        const auto ordinal = non_negative_id(*item, "ordinal");
        const auto cap_id = non_negative_id(*item, "cap_id");
        const auto memo_node = non_negative_id(*item, "node_id");
        const auto hash = item->get("arg_hash")->as_string();
        const auto wire = item->get("result_wire_json")->as_string();
        const auto present = item->get("result_present")->as_bool();
        if (!ordinal || !cap_id || !memo_node || !hash || !wire || !present)
            return invalid();
        std::uint64_t arg_hash = 0;
        const auto [end, error] =
            std::from_chars(hash->data(), hash->data() + hash->size(), arg_hash);
        if (error != std::errc{} || end != hash->data() + hash->size())
            return invalid();
        const auto node = WorkflowNodeId{*memo_node};
        if (!node.valid() || !coordinates.emplace(node, *ordinal).second ||
            (node == record.node && *ordinal >= record.pending_ordinal))
            return invalid();
        CapabilityMemoEntry entry{.ordinal = *ordinal,
                                  .cap_id = *cap_id,
                                  .arg_hash = arg_hash,
                                  .result = StoredMemoResult{std::string(*wire), *present},
                                  .node = node};
        if (!memo_result_state_well_formed(entry))
            return invalid();
        record.memo.push_back(std::move(entry));
    }
    snapshot.suspended = std::move(record);
    return snapshot;
}

[[nodiscard]] bool memo_result_state_well_formed(const CapabilityMemoEntry &entry) {
    return stored_result(entry.result).has_value();
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
            state.output = runtime::clone_value(*value);
        }
        snapshot.completed_nodes.push_back(std::move(state));
    }
    return snapshot;
}

std::uint64_t compute_idempotency_key(std::size_t workflow_index,
                                      std::size_t node_index,
                                      std::uint64_t ordinal,
                                      std::size_t cap_symbol_id,
                                      std::uint64_t arg_hash) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFULL;
            hash *= kPrime;
        }
    };
    mix(static_cast<std::uint64_t>(workflow_index));
    mix(static_cast<std::uint64_t>(node_index));
    mix(ordinal);
    mix(static_cast<std::uint64_t>(cap_symbol_id));
    mix(arg_hash);
    return hash;
}

} // namespace ahfl::runtime
