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

// RFC 0026 C2b stage3 (P0-19): serialize one memo entry's result trust-authority
// into the append-only sidecar fields, applying the save-local presence
// normalization. Returns false (fail-closed) on an ill-formed entry state so the
// whole snapshot save aborts rather than persisting a corrupt/ambiguous record.
//
// The gate is representation-local (no capability binding at save time) and does
// NOT mutate the caller's in-memory entry. It writes:
//   - `result`            : the legacy JSON (old readers keep reading this).
//   - `result_wire_json`  : the exact value_to_json spelling (new-reader authority).
//   - `result_present`    : the presence bit (JSON bool).
// For a LegacyV2 entry that was loaded but never consumed, ONLY a byte-stable raw
// value may be re-emitted WITHOUT a sidecar/presence upgrade: the save layer has no
// binding, so it refuses to re-canonicalize. It re-writes the authoritative bytes
// only when serialize_json(parse(bytes)) == bytes (the permitted branch is then
// byte-identical); a would-canonicalize Legacy entry (1.0, 1e3, -0.0, an overflowing
// integer, ...) is fail-closed here and must first be binding-consume upgraded to a
// fresh NativeOnly -> ExactSidecar (see the branch note for the laundering rationale).
[[nodiscard]] bool write_memo_result(const CapabilityMemoEntry &entry, JsonValue &memo_item) {
    switch (entry.source) {
    case PersistedMemoResultSource::LegacyV2: {
        // Re-emit the legacy `result` and write NO sidecar / NO presence (no false
        // upgrade): the entry stays LegacyV2 until a per-ordinal binding decode
        // resolves it.
        //
        // TRUST BOUNDARY (P0 provenance laundering): the save layer has NO binding,
        // so it MUST NOT re-canonicalize the legacy bytes. parse + generic serialize
        // is provenance-lossy — e.g. a hostile bare integer literal that parses as an
        // out-of-range IntegerFallback is re-serialized in exponent/float form and
        // re-parses as FloatSyntax, which the recovery-internal legacy Float decoder
        // would then ACCEPT. Re-saving such an UNCONSUMED entry would silently
        // convert a value the exact/legacy codecs must both reject into one the
        // legacy Float slot admits (a trust-boundary false accept, not a mere byte
        // fidelity issue). So a LegacyV2 entry may be re-saved ONLY when its bytes are
        // already byte-stable under the deterministic serializer
        // (serialize_json(parse(bytes)) == bytes). Any legacy spelling that would
        // canonicalize (1.0, 1e3, -0.0, an overflowing integer, ...) is rejected here
        // and must be UPGRADED THROUGH CONSUMPTION instead: P0-13's dense prefix
        // binding-decodes a replay-consumed entry and re-writes it as a fresh
        // NativeOnly -> ExactSidecar. Refusing to re-save an unconsumed,
        // would-canonicalize Legacy entry is the honest boundary.
        if (!entry.authoritative_json.has_value() || entry.authoritative_json->empty() ||
            entry.result_present.has_value()) {
            return false; // ill-formed LegacyV2 state
        }
        auto raw = ahfl::json::parse_json(*entry.authoritative_json);
        if (!raw.has_value() || !*raw) {
            return false;
        }
        // Byte-stability gate: reject anything the serializer would rewrite.
        if (ahfl::json::serialize_json(**raw) != *entry.authoritative_json) {
            return false; // would canonicalize -> provenance laundering; fail closed
        }
        memo_item.set("result", std::move(*raw));
        return true;
    }
    case PersistedMemoResultSource::ExactSidecar: {
        if (!entry.authoritative_json.has_value() || entry.authoritative_json->empty() ||
            !entry.result_present.has_value()) {
            return false; // ill-formed ExactSidecar state
        }
        auto wire = ahfl::json::parse_json(*entry.authoritative_json);
        if (!wire.has_value() || !*wire) {
            return false;
        }
        const bool present = *entry.result_present;
        // presence=false MUST spell exactly JSON null (valueless / Unit-null seam).
        if (!present && (*wire)->kind != ahfl::json::Kind::Null) {
            return false;
        }
        // Legacy `result` is the deterministic projection of the sidecar bytes.
        auto legacy = ahfl::json::parse_json(*entry.authoritative_json);
        if (!legacy.has_value() || !*legacy) {
            return false;
        }
        memo_item.set("result", std::move(*legacy));
        memo_item.set("result_wire_json", JsonValue::make_string(*entry.authoritative_json));
        memo_item.set("result_present", JsonValue::make_bool(present));
        return true;
    }
    case PersistedMemoResultSource::NativeOnly: {
        if (entry.authoritative_json.has_value() || !entry.result_present.has_value()) {
            return false; // ill-formed NativeOnly state
        }
        const std::string wire = evaluator::value_to_json(entry.result);
        bool present = *entry.result_present;
        // P0-19 save-local normalization (no caller mutation): a present bare
        // NoneValue is the established valueless-success compat case; persist it as
        // presence=false so a later Unit-binding decode does not fabricate a Unit.
        const bool is_bare_none =
            std::holds_alternative<evaluator::NoneValue>(entry.result.node);
        const bool is_unit = std::holds_alternative<evaluator::UnitValue>(entry.result.node);
        if (present && is_bare_none) {
            present = false;
        }
        if (!present) {
            // Only a valueless success (NoneValue) or an explicit Unit may be
            // absent, and its wire spelling must be exactly null.
            if (!(is_bare_none || is_unit) || wire != "null") {
                return false;
            }
        }
        auto legacy = ahfl::json::parse_json(wire);
        if (!legacy.has_value() || !*legacy) {
            return false;
        }
        memo_item.set("result", std::move(*legacy));
        memo_item.set("result_wire_json", JsonValue::make_string(wire));
        memo_item.set("result_present", JsonValue::make_bool(present));
        return true;
    }
    }
    return false;
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
            // RFC 0026 C2b stage3: write the result trust-authority (legacy field +
            // append-only sidecar/presence) with the P0-19 save-local presence
            // normalization; fail-closed on an ill-formed entry state.
            if (!write_memo_result(entry, *memo_item)) {
                return {};
            }
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
            // Direct DOM decode (RFC 0026 C2b P0-10): decode the subtree in place
            // instead of serialize_json -> value_from_json, so numeric provenance
            // is preserved and an ambiguous number in a corrupt snapshot fails
            // closed rather than silently degrading.
            auto value = evaluator::value_from_json(*output);
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
            auto value = evaluator::value_from_json(*node_input); // direct DOM (P0-10)
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
            const auto ordinal_value = static_cast<std::uint64_t>(*ordinal);
            if (!memo_ordinals.insert(ordinal_value).second) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }

            // RFC 0026 C2b stage3: load the result trust-authority. A new v2 entry
            // carries an ExactSidecar (`result_wire_json` string + `result_present`
            // bool); an old v2 entry has neither and is LegacyV2 (its authority is
            // the raw `result` JSON substring, captured losslessly from the original
            // content via begin/end offsets — no serialize -> reparse laundering).
            // Every entry loaded from disk is Legacy/Sidecar; the native `result`
            // Value is ONLY a compatibility projection here and is NEVER the trust
            // authority, so its schema-free decodability MUST NOT gate load
            // admission (P0-20: e.g. a legal Map<String,String>{"_timestamp":...}
            // is not schema-free-decodable but is exact-decodable under its Map
            // binding at consume). The authoritative JSON is validated first.
            const auto *wire_field = memo_item->get("result_wire_json");
            const auto *present_field = memo_item->get("result_present");
            if (value->end_offset < value->begin_offset ||
                value->end_offset > content->size()) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            CapabilityMemoEntry entry{
                .ordinal = ordinal_value,
                .cap_id = *cap_id,
                .arg_hash = arg_hash_value,
            };

            if (wire_field == nullptr && present_field == nullptr) {
                // LegacyV2: authority = the exact `result` substring; presence
                // stays UNKNOWN (nullopt) until a per-ordinal binding decode.
                entry.source = PersistedMemoResultSource::LegacyV2;
                entry.authoritative_json =
                    content->substr(value->begin_offset, value->end_offset - value->begin_offset);
                entry.result_present = std::nullopt;
                if (entry.authoritative_json->empty()) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
            } else {
                // ExactSidecar: both fields must be present and well-typed.
                if (wire_field == nullptr || wire_field->kind != ahfl::json::Kind::String ||
                    present_field == nullptr || present_field->kind != ahfl::json::Kind::Bool) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
                const std::string &sidecar = wire_field->string_val;
                if (sidecar.empty()) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
                auto sidecar_dom = ahfl::json::parse_json(sidecar);
                if (!sidecar_dom.has_value() || !*sidecar_dom) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
                const bool present = present_field->bool_val;
                // presence=false must spell exactly JSON null.
                if (!present && (*sidecar_dom)->kind != ahfl::json::Kind::Null) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
                // Two-field consistency: the raw legacy `result` substring must equal
                // the deterministic projection serialize_json(parse(sidecar)). This
                // only prevents the two fields from drifting; it permits the full
                // deterministic serializer canonicalization (e.g. integral Float
                // 1.0 -> 1), and does NOT prove payload legality (that is decided by
                // the exact binding decode at consume).
                const std::string legacy_raw =
                    content->substr(value->begin_offset, value->end_offset - value->begin_offset);
                if (legacy_raw != ahfl::json::serialize_json(**sidecar_dom)) {
                    return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
                }
                entry.source = PersistedMemoResultSource::ExactSidecar;
                entry.authoritative_json = sidecar;
                entry.result_present = present;
            }
            if (!memo_result_state_well_formed(entry)) {
                return std::unexpected(WorkflowRecoveryError::InvalidSnapshot);
            }
            // Best-effort compat native projection (P0-20): NOT a trust authority
            // and NOT a load-admission gate. When the legacy `result` is not
            // schema-free-decodable (a legal rich shape such as a Map with a
            // reserved-marker key), fall back to a non-authoritative NoneValue
            // placeholder and keep loading — the authoritative JSON still flows to
            // the per-ordinal exact binding decode at consume. Trust paths MUST NOT
            // read `entry.result` under Legacy/Sidecar.
            if (auto projected = evaluator::value_from_json(*value); projected.has_value()) {
                entry.result = std::move(*projected);
            } else {
                entry.result = evaluator::make_none();
            }
            record.memo.push_back(std::move(entry));
        }
        snapshot.suspended = std::move(record);
    }
    return snapshot;
}

[[nodiscard]] bool memo_result_state_well_formed(const CapabilityMemoEntry &entry) {
    switch (entry.source) {
    case PersistedMemoResultSource::NativeOnly:
        return !entry.authoritative_json.has_value() && entry.result_present.has_value();
    case PersistedMemoResultSource::LegacyV2:
        return entry.authoritative_json.has_value() && !entry.authoritative_json->empty() &&
               !entry.result_present.has_value();
    case PersistedMemoResultSource::ExactSidecar:
        return entry.authoritative_json.has_value() && !entry.authoritative_json->empty() &&
               entry.result_present.has_value();
    }
    return false;
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
