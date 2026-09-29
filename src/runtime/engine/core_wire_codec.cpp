#include "runtime/engine/core_wire_codec.hpp"

#include "runtime/engine/core_wire_codec_recovery.hpp"
#include "runtime/value/scalar_spelling.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ahfl::runtime::wire_codec {

namespace {

using ahfl::ir::core::CoreWirePayloadKind;
using ahfl::ir::core::CoreWireSchemaBool;
using ahfl::ir::core::CoreWireSchemaDecimal;
using ahfl::ir::core::CoreWireSchemaDuration;
using ahfl::ir::core::CoreWireSchemaEnum;
using ahfl::ir::core::CoreWireSchemaFloat;
using ahfl::ir::core::CoreWireSchemaInt;
using ahfl::ir::core::CoreWireSchemaMap;
using ahfl::ir::core::CoreWireSchemaNodeId;
using ahfl::ir::core::CoreWireSchemaOption;
using ahfl::ir::core::CoreWireSchemaSequence;
using ahfl::ir::core::CoreWireSchemaShape;
using ahfl::ir::core::CoreWireSchemaString;
using ahfl::ir::core::CoreWireSchemaStruct;
using ahfl::ir::core::CoreWireSchemaTable;
using ahfl::ir::core::CoreWireSchemaTimestamp;
using ahfl::ir::core::CoreWireSchemaTuple;
using ahfl::ir::core::CoreWireSchemaUnit;
using ahfl::ir::core::CoreWireSchemaUuid;
using ahfl::ir::core::CoreWireSchemaVariant;
using ahfl::ir::core::CoreWireSequenceKind;

namespace scalar_spelling = runtime::scalar_spelling;

constexpr std::string_view kOptionEnumName = "std::option::Option";

// Decode policy for the shared Decoder traversal (internal detail; never exposed
// on the public codec surface). Exact is the only policy live trust paths use;
// LegacyV2 is reachable ONLY via decode_json_legacy_v2 (durable-resume loader),
// and relaxes exactly one rule: SignedInteger -> Float at a Float schema node.
enum class DecodePolicy {
    Exact,
    LegacyV2,
};

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

// Resolve a node id to its shape, or nullptr if out of range. The binding's table
// already passed the local verifier, which is CYCLE-SAFE over reachable nodes but
// (per C1) does NOT forbid recursive cycles — a self-referential schema is legal.
// This guard only defends a malformed out-of-range id in a child slot.
[[nodiscard]] const CoreWireSchemaShape *shape_at(const CoreWireSchemaTable &table,
                                                  CoreWireSchemaNodeId id) {
    if (id.value >= table.nodes.size()) {
        return nullptr;
    }
    return &table.nodes[id.value].shape;
}

[[nodiscard]] bool int_in_bounds(const CoreWireSchemaInt &schema, std::int64_t value) {
    if (!schema.bounds.has_value()) {
        return true;
    }
    return value >= schema.bounds->first && value <= schema.bounds->second;
}

// String length is measured in UTF-8 bytes, matching the rest of the runtime
// (builtins string length is a byte count; codepoint handling is deferred
// project-wide). length_bounds are [min, max] inclusive.
[[nodiscard]] bool string_len_in_bounds(const CoreWireSchemaString &schema,
                                        const std::string &value) {
    if (!schema.length_bounds.has_value()) {
        return true;
    }
    const auto len = static_cast<std::int64_t>(value.size());
    return len >= schema.length_bounds->first && len <= schema.length_bounds->second;
}

[[nodiscard]] bool capacity_ok(const std::optional<std::uint64_t> &capacity, std::size_t count) {
    if (!capacity.has_value()) {
        return true;
    }
    return count <= *capacity;
}

// A UUID's exact wire spelling: exactly 32 lowercase hex characters. No dashes,
// no braces, no uppercase, no spaces. This is stricter than make_uuid (which
// normalizes) on purpose — the wire form must already be canonical.
[[nodiscard]] bool is_canonical_uuid_hex(std::string_view hex) {
    if (hex.size() != 32) {
        return false;
    }
    for (const char c : hex) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Decimal scale compare WITHOUT narrowing: parts.scale is i32, schema.scale is
// i64, so widen the parsed value rather than truncating the schema's.
[[nodiscard]] bool decimal_scale_matches(std::int32_t parsed_scale, std::int64_t schema_scale) {
    return static_cast<std::int64_t>(parsed_scale) == schema_scale;
}

// =====================================================================
// Policy 1: decode a JSON DOM value into a native Value, schema-guided.
// =====================================================================

class Decoder {
  public:
    explicit Decoder(const CoreWireSchemaTable &table, DecodePolicy policy)
        : table_(table), policy_(policy) {}

    [[nodiscard]] WireDecodeResult decode(const json::JsonValue &json, CoreWireSchemaNodeId id) {
        const auto *shape = shape_at(table_, id);
        if (shape == nullptr) {
            return WireDecodeResult::failure("wire-codec: schema node id out of range");
        }
        return std::visit([&](const auto &s) { return decode_shape(json, s); }, *shape);
    }

  private:
    const CoreWireSchemaTable &table_;
    DecodePolicy policy_;

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaUnit &) {
        if (json.kind != json::Kind::Null) {
            return WireDecodeResult::failure("wire-codec: expected null for Unit");
        }
        return WireDecodeResult::success(runtime::make_unit());
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaBool &) {
        if (json.kind != json::Kind::Bool) {
            return WireDecodeResult::failure("wire-codec: expected bool");
        }
        return WireDecodeResult::success(runtime::make_bool(json.bool_val));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaInt &schema) {
        // as_int() yields a value only for a SignedInteger-provenance Int, so a
        // high-bit unsigned magnitude or an out-of-range integer token (which the
        // base DOM classes as UnsignedInteger / IntegerFallback) fails closed here
        // rather than silently degrading (RFC 0026 C2b P0-10).
        const auto signed_int = json.as_int();
        if (json.kind != json::Kind::Int || !signed_int.has_value()) {
            return WireDecodeResult::failure("wire-codec: expected integer");
        }
        if (!int_in_bounds(schema, *signed_int)) {
            return WireDecodeResult::failure("wire-codec: integer out of schema bounds");
        }
        return WireDecodeResult::success(runtime::make_int(*signed_int));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaFloat &) {
        // LegacyV2 compat (P0-16, recovery-internal only): an old pre-sidecar
        // snapshot serialized an integral Float (1.0) as the bare int `1` through
        // the generic serializer. Under LegacyV2 ONLY, accept a SignedInteger
        // Kind::Int at a Float node as that historical artifact. UnsignedInteger /
        // IntegerFallback / real Float rules below are NOT relaxed, and this never
        // applies under the exact policy.
        if (policy_ == DecodePolicy::LegacyV2 && json.kind == json::Kind::Int) {
            const auto signed_int = json.as_int(); // SignedInteger provenance only
            if (!signed_int.has_value()) {
                return WireDecodeResult::failure("wire-codec: expected float (no int widening)");
            }
            const double widened = static_cast<double>(*signed_int);
            if (!std::isfinite(widened)) {
                return WireDecodeResult::failure("wire-codec: float must be finite");
            }
            return WireDecodeResult::success(runtime::make_float(widened));
        }
        // Must be a JSON float with float syntax and finite — never an Int widened
        // to float, and never an IntegerFallback (an out-of-uint64 integer token the
        // DOM stores as an approximate Float) laundered through the float branch
        // (RFC 0026 C2b P0-10).
        if (json.kind != json::Kind::Float ||
            json.number_provenance != json::NumberProvenance::FloatSyntax) {
            return WireDecodeResult::failure("wire-codec: expected float (no int widening)");
        }
        if (!std::isfinite(json.float_val)) {
            return WireDecodeResult::failure("wire-codec: float must be finite");
        }
        return WireDecodeResult::success(runtime::make_float(json.float_val));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaString &schema) {
        if (json.kind != json::Kind::String) {
            return WireDecodeResult::failure("wire-codec: expected string");
        }
        if (!string_len_in_bounds(schema, json.string_val)) {
            return WireDecodeResult::failure("wire-codec: string length out of schema bounds");
        }
        return WireDecodeResult::success(runtime::make_string(json.string_val));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaDecimal &schema) {
        if (json.kind != json::Kind::String) {
            return WireDecodeResult::failure("wire-codec: expected string for Decimal");
        }
        const auto decoded = scalar_spelling::parse_decimal(json.string_val);
        if (!decoded.has_value()) {
            return WireDecodeResult::failure("wire-codec: malformed Decimal spelling");
        }
        if (!decimal_scale_matches(decoded->parts.scale, schema.scale)) {
            return WireDecodeResult::failure("wire-codec: Decimal scale does not match schema");
        }
        // Preserve the input spelling verbatim; parsing was validation only.
        return WireDecodeResult::success(runtime::make_decimal(json.string_val));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaDuration &) {
        if (json.kind != json::Kind::String) {
            return WireDecodeResult::failure("wire-codec: expected string for Duration");
        }
        if (!scalar_spelling::parse_duration(json.string_val).has_value()) {
            return WireDecodeResult::failure("wire-codec: malformed Duration spelling");
        }
        // Preserve the input spelling verbatim; parsing was validation only.
        return WireDecodeResult::success(runtime::make_duration(json.string_val));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaTimestamp &) {
        // Canonical wire form is the exact object {"_timestamp": <int64>}.
        if (json.kind != json::Kind::Object || json.object_fields.size() != 1) {
            return WireDecodeResult::failure(
                "wire-codec: expected exact {\"_timestamp\":<int>} object");
        }
        const auto *ts = json.get("_timestamp");
        // Same SignedInteger-only gate as plain Int: a high-bit unsigned or an
        // out-of-range integer token must fail closed, not degrade (P0-10).
        if (ts == nullptr || ts->kind != json::Kind::Int) {
            return WireDecodeResult::failure("wire-codec: Timestamp _timestamp must be an integer");
        }
        const auto ts_int = ts->as_int();
        if (!ts_int.has_value()) {
            return WireDecodeResult::failure("wire-codec: Timestamp _timestamp must be an integer");
        }
        return WireDecodeResult::success(runtime::make_timestamp(*ts_int));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaUuid &) {
        // Canonical wire form is the exact object {"_uuid":"<32 lowercase hex>"}.
        if (json.kind != json::Kind::Object || json.object_fields.size() != 1) {
            return WireDecodeResult::failure("wire-codec: expected exact {\"_uuid\":\"...\"} object");
        }
        const auto *hex = json.get("_uuid");
        if (hex == nullptr || hex->kind != json::Kind::String) {
            return WireDecodeResult::failure("wire-codec: Uuid _uuid must be a string");
        }
        if (!is_canonical_uuid_hex(hex->string_val)) {
            return WireDecodeResult::failure(
                "wire-codec: UUID must be exactly 32 lowercase hex chars");
        }
        auto uuid = runtime::make_uuid(hex->string_val);
        if (!uuid.has_value()) {
            return WireDecodeResult::failure("wire-codec: malformed UUID spelling");
        }
        return WireDecodeResult::success(std::move(*uuid));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaOption &schema) {
        // Design seam: `null` under an Option node is None; a non-null value is
        // decoded under the Some child (the compact form, NOT value_json's
        // {"_enum":...} object).
        if (json.kind == json::Kind::Null) {
            return WireDecodeResult::success(runtime::make_option_none());
        }
        auto inner = decode(json, schema.value);
        if (!inner.ok()) {
            return inner;
        }
        return WireDecodeResult::success(runtime::make_option_some(std::move(*inner.value)));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaSequence &schema) {
        if (json.kind != json::Kind::Array) {
            return WireDecodeResult::failure("wire-codec: expected array for sequence");
        }
        if (!capacity_ok(schema.capacity, json.array_items.size())) {
            return WireDecodeResult::failure("wire-codec: sequence exceeds schema capacity");
        }
        std::vector<Value> items;
        items.reserve(json.array_items.size());
        for (const auto &item : json.array_items) {
            if (!item) {
                return WireDecodeResult::failure("wire-codec: null sequence element node");
            }
            auto decoded = decode(*item, schema.element);
            if (!decoded.ok()) {
                return decoded;
            }
            items.push_back(std::move(*decoded.value));
        }
        if (schema.kind == CoreWireSequenceKind::Set) {
            // A Set's JSON array may arrive in any order: we reject only
            // structurally-equal duplicates here, then let make_set construct the
            // canonical (deduped + ordered) Set. Order is NOT an inbound requirement;
            // make_set would silently dedup, so the duplicate check must precede it.
            for (std::size_t i = 0; i < items.size(); ++i) {
                for (std::size_t j = i + 1; j < items.size(); ++j) {
                    if (runtime::structurally_equal(items[i], items[j])) {
                        return WireDecodeResult::failure("wire-codec: duplicate element in Set");
                    }
                }
            }
            return WireDecodeResult::success(runtime::make_set(std::move(items)));
        }
        return WireDecodeResult::success(runtime::make_list(std::move(items)));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaMap &schema) {
        if (json.kind != json::Kind::Object) {
            return WireDecodeResult::failure("wire-codec: expected object for map");
        }
        const auto *key_shape = shape_at(table_, schema.key);
        if (key_shape == nullptr || !std::holds_alternative<CoreWireSchemaString>(*key_shape)) {
            return WireDecodeResult::failure("wire-codec: map key schema must be String");
        }
        if (!capacity_ok(schema.capacity, json.object_fields.size())) {
            return WireDecodeResult::failure("wire-codec: map exceeds schema capacity");
        }
        const auto &key_string_schema = std::get<CoreWireSchemaString>(*key_shape);
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(json.object_fields.size());
        std::vector<std::string_view> seen_keys;
        for (const auto &[key, json_val] : json.object_fields) {
            for (const auto &prior : seen_keys) {
                if (prior == key) {
                    return WireDecodeResult::failure("wire-codec: duplicate map key");
                }
            }
            seen_keys.push_back(key);
            if (!string_len_in_bounds(key_string_schema, key)) {
                return WireDecodeResult::failure("wire-codec: map key length out of schema bounds");
            }
            if (!json_val) {
                return WireDecodeResult::failure("wire-codec: null map value node");
            }
            auto decoded_val = decode(*json_val, schema.value);
            if (!decoded_val.ok()) {
                return decoded_val;
            }
            entries.emplace_back(runtime::make_string(key), std::move(*decoded_val.value));
        }
        return WireDecodeResult::success(runtime::make_map(std::move(entries)));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaStruct &schema) {
        if (json.kind != json::Kind::Object) {
            return WireDecodeResult::failure("wire-codec: expected object for struct");
        }
        // Raw duplicate-field check over the DOM (hand-built DOMs can carry dups).
        // The error is schema-only: it names the expected struct (a compiler
        // artifact), never the observed duplicate key (P0-8: no payload echo).
        for (std::size_t i = 0; i < json.object_fields.size(); ++i) {
            for (std::size_t j = i + 1; j < json.object_fields.size(); ++j) {
                if (json.object_fields[i].first == json.object_fields[j].first) {
                    return WireDecodeResult::failure(
                        "wire-codec: duplicate field in struct '" + schema.wire_name + "'");
                }
            }
        }
        // Exact `_type` discriminator.
        const auto *type_field = json.get("_type");
        if (type_field == nullptr || type_field->kind != json::Kind::String ||
            type_field->string_val != schema.wire_name) {
            return WireDecodeResult::failure("wire-codec: struct _type must equal '" +
                                             schema.wire_name + "'");
        }
        // Field set must be exactly {_type} + schema fields: count matches and
        // every schema field present.
        if (json.object_fields.size() != schema.fields.size() + 1) {
            return WireDecodeResult::failure("wire-codec: struct has missing or extra fields");
        }
        std::unordered_map<std::string, Value> fields;
        for (const auto &field : schema.fields) {
            const auto *field_json = json.get(field.wire_name);
            if (field_json == nullptr) {
                return WireDecodeResult::failure("wire-codec: struct missing field '" +
                                                 field.wire_name + "'");
            }
            auto decoded = decode(*field_json, field.type);
            if (!decoded.ok()) {
                return decoded;
            }
            fields.emplace(field.wire_name, std::move(*decoded.value));
        }
        return WireDecodeResult::success(runtime::make_struct(schema.wire_name, std::move(fields)));
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaEnum &schema) {
        if (json.kind != json::Kind::Object) {
            return WireDecodeResult::failure("wire-codec: expected object for enum");
        }
        for (std::size_t i = 0; i < json.object_fields.size(); ++i) {
            for (std::size_t j = i + 1; j < json.object_fields.size(); ++j) {
                if (json.object_fields[i].first == json.object_fields[j].first) {
                    return WireDecodeResult::failure("wire-codec: duplicate field in enum '" +
                                                     schema.wire_name + "'");
                }
            }
        }
        const auto *enum_field = json.get("_enum");
        if (enum_field == nullptr || enum_field->kind != json::Kind::String ||
            enum_field->string_val != schema.wire_name) {
            return WireDecodeResult::failure("wire-codec: enum _enum must equal '" +
                                             schema.wire_name + "'");
        }
        const auto *variant_field = json.get("_variant");
        if (variant_field == nullptr || variant_field->kind != json::Kind::String) {
            return WireDecodeResult::failure("wire-codec: enum missing string _variant");
        }
        const CoreWireSchemaVariant *variant = nullptr;
        for (const auto &v : schema.variants) {
            if (v.wire_name == variant_field->string_val) {
                variant = &v;
                break;
            }
        }
        if (variant == nullptr) {
            // Schema-only: name the enum, never the observed _variant (P0-8).
            return WireDecodeResult::failure("wire-codec: variant not declared in enum '" +
                                             schema.wire_name + "'");
        }
        switch (variant->payload_kind) {
        case CoreWirePayloadKind::Unit:
            // Exactly {_enum, _variant}.
            if (json.object_fields.size() != 2) {
                return WireDecodeResult::failure("wire-codec: unit variant must carry no payload");
            }
            return WireDecodeResult::success(
                runtime::make_enum(schema.wire_name, variant->wire_name));
        case CoreWirePayloadKind::Tuple: {
            // Exactly {_enum, _variant, _payload}.
            if (json.object_fields.size() != 3) {
                return WireDecodeResult::failure("wire-codec: tuple variant must carry only _payload");
            }
            const auto *payload = json.get("_payload");
            if (payload == nullptr || payload->kind != json::Kind::Array) {
                return WireDecodeResult::failure("wire-codec: tuple variant needs _payload array");
            }
            if (payload->array_items.size() != variant->slots.size()) {
                return WireDecodeResult::failure("wire-codec: tuple variant payload arity mismatch");
            }
            std::vector<Value> payload_values;
            payload_values.reserve(variant->slots.size());
            for (std::size_t i = 0; i < variant->slots.size(); ++i) {
                if (!payload->array_items[i]) {
                    return WireDecodeResult::failure("wire-codec: null tuple payload element");
                }
                auto decoded = decode(*payload->array_items[i], variant->slots[i].type);
                if (!decoded.ok()) {
                    return decoded;
                }
                payload_values.push_back(std::move(*decoded.value));
            }
            return WireDecodeResult::success(runtime::make_enum(
                schema.wire_name, variant->wire_name, std::move(payload_values)));
        }
        case CoreWirePayloadKind::Struct: {
            // Exactly {_enum, _variant, _named_payload}.
            if (json.object_fields.size() != 3) {
                return WireDecodeResult::failure(
                    "wire-codec: struct variant must carry only _named_payload");
            }
            const auto *named = json.get("_named_payload");
            if (named == nullptr || named->kind != json::Kind::Object) {
                return WireDecodeResult::failure(
                    "wire-codec: struct variant needs _named_payload object");
            }
            for (std::size_t i = 0; i < named->object_fields.size(); ++i) {
                for (std::size_t j = i + 1; j < named->object_fields.size(); ++j) {
                    if (named->object_fields[i].first == named->object_fields[j].first) {
                        return WireDecodeResult::failure(
                            "wire-codec: duplicate field in struct variant '" + variant->wire_name +
                            "'");
                    }
                }
            }
            if (named->object_fields.size() != variant->slots.size()) {
                return WireDecodeResult::failure("wire-codec: struct variant field set mismatch");
            }
            std::unordered_map<std::string, Value> named_values;
            for (const auto &slot : variant->slots) {
                const auto *slot_json = named->get(slot.wire_name);
                if (slot_json == nullptr) {
                    return WireDecodeResult::failure("wire-codec: struct variant missing field '" +
                                                     slot.wire_name + "'");
                }
                auto decoded = decode(*slot_json, slot.type);
                if (!decoded.ok()) {
                    return decoded;
                }
                named_values.emplace(slot.wire_name, std::move(*decoded.value));
            }
            return WireDecodeResult::success(runtime::make_enum(
                schema.wire_name, variant->wire_name, std::move(named_values)));
        }
        }
        return WireDecodeResult::failure("wire-codec: unknown enum payload kind");
    }

    [[nodiscard]] WireDecodeResult decode_shape(const json::JsonValue &json,
                                                const CoreWireSchemaTuple &schema) {
        // A tuple is wired as a positional JSON array; the runtime models it as a
        // positional list (no dedicated tuple Value).
        if (json.kind != json::Kind::Array) {
            return WireDecodeResult::failure("wire-codec: expected array for tuple");
        }
        if (json.array_items.size() != schema.elements.size()) {
            return WireDecodeResult::failure("wire-codec: tuple arity mismatch");
        }
        std::vector<Value> items;
        items.reserve(schema.elements.size());
        for (std::size_t i = 0; i < schema.elements.size(); ++i) {
            if (!json.array_items[i]) {
                return WireDecodeResult::failure("wire-codec: null tuple element");
            }
            auto decoded = decode(*json.array_items[i], schema.elements[i]);
            if (!decoded.ok()) {
                return decoded;
            }
            items.push_back(std::move(*decoded.value));
        }
        return WireDecodeResult::success(runtime::make_list(std::move(items)));
    }
};

// =====================================================================
// Policy 2: validate a native Value against the schema, no rebuild.
// =====================================================================

class Validator {
  public:
    explicit Validator(const CoreWireSchemaTable &table) : table_(table) {}

    [[nodiscard]] SchemaValidationResult check(const Value &value, CoreWireSchemaNodeId id) {
        const auto *shape = shape_at(table_, id);
        if (shape == nullptr) {
            return SchemaValidationResult::fail("wire-codec: schema node id out of range");
        }
        return std::visit([&](const auto &s) { return check_shape(value, s); }, *shape);
    }

  private:
    const CoreWireSchemaTable &table_;

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaUnit &) {
        if (!std::holds_alternative<runtime::UnitValue>(value.node)) {
            return SchemaValidationResult::fail("wire-codec: expected Unit value");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaBool &) {
        if (!std::holds_alternative<runtime::BoolValue>(value.node)) {
            return SchemaValidationResult::fail("wire-codec: expected Bool value");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaInt &schema) {
        const auto *iv = std::get_if<runtime::IntValue>(&value.node);
        if (iv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Int value");
        }
        if (!int_in_bounds(schema, iv->value)) {
            return SchemaValidationResult::fail("wire-codec: integer out of schema bounds");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaFloat &) {
        const auto *fv = std::get_if<runtime::FloatValue>(&value.node);
        if (fv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Float value (no int widening)");
        }
        if (!std::isfinite(fv->value)) {
            return SchemaValidationResult::fail("wire-codec: float must be finite");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaString &schema) {
        const auto *sv = std::get_if<runtime::StringValue>(&value.node);
        if (sv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected String value");
        }
        if (!string_len_in_bounds(schema, sv->value)) {
            return SchemaValidationResult::fail("wire-codec: string length out of schema bounds");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaDecimal &schema) {
        const auto *dv = std::get_if<runtime::DecimalValue>(&value.node);
        if (dv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Decimal value");
        }
        const auto decoded = scalar_spelling::parse_decimal(dv->spelling);
        if (!decoded.has_value()) {
            return SchemaValidationResult::fail("wire-codec: malformed Decimal spelling");
        }
        if (!decimal_scale_matches(decoded->parts.scale, schema.scale)) {
            return SchemaValidationResult::fail("wire-codec: Decimal scale does not match schema");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaDuration &) {
        const auto *dv = std::get_if<runtime::DurationValue>(&value.node);
        if (dv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Duration value");
        }
        if (!scalar_spelling::parse_duration(dv->spelling).has_value()) {
            return SchemaValidationResult::fail("wire-codec: malformed Duration spelling");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaTimestamp &) {
        if (!std::holds_alternative<runtime::TimestampValue>(value.node)) {
            return SchemaValidationResult::fail("wire-codec: expected Timestamp value");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaUuid &) {
        const auto *uv = std::get_if<runtime::UuidValue>(&value.node);
        if (uv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Uuid value");
        }
        if (!is_canonical_uuid_hex(uv->hex)) {
            return SchemaValidationResult::fail(
                "wire-codec: UUID must be exactly 32 lowercase hex chars");
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaOption &schema) {
        // Exact Option EnumValue only — a bare NoneValue is NOT an Option.
        const auto *ev = std::get_if<runtime::EnumValue>(&value.node);
        if (ev == nullptr || ev->enum_name != kOptionEnumName) {
            return SchemaValidationResult::fail("wire-codec: expected Option enum value");
        }
        if (ev->variant == "None") {
            if (!ev->payload.empty() || !ev->named_payload.empty()) {
                return SchemaValidationResult::fail("wire-codec: Option::None carries a payload");
            }
            return SchemaValidationResult::ok();
        }
        if (ev->variant == "Some") {
            if (ev->payload.size() != 1 || ev->payload.front() == nullptr ||
                !ev->named_payload.empty()) {
                return SchemaValidationResult::fail("wire-codec: Option::Some payload malformed");
            }
            return check(*ev->payload.front(), schema.value);
        }
        return SchemaValidationResult::fail("wire-codec: Option variant must be Some or None");
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaSequence &schema) {
        if (schema.kind == CoreWireSequenceKind::Set) {
            const auto *sv = std::get_if<runtime::SetValue>(&value.node);
            if (sv == nullptr) {
                return SchemaValidationResult::fail("wire-codec: expected Set value");
            }
            if (!capacity_ok(schema.capacity, sv->items.size())) {
                return SchemaValidationResult::fail("wire-codec: set exceeds schema capacity");
            }
            // Validate every child on the ORIGINAL Value FIRST (P0-7): this proves
            // no null and correct types/bounds throughout the subtree, so the
            // subsequent clone-based canonicality check can never dereference a
            // hostile null.
            for (const auto &item : sv->items) {
                if (!item) {
                    return SchemaValidationResult::fail("wire-codec: null Set element");
                }
                if (auto r = check(*item, schema.element); !r.valid) {
                    return r;
                }
            }
            if (auto canon = check_set_canonical(*sv); !canon.valid) {
                return canon;
            }
            return SchemaValidationResult::ok();
        }
        const auto *lv = std::get_if<runtime::ListValue>(&value.node);
        if (lv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected List value");
        }
        if (!capacity_ok(schema.capacity, lv->items.size())) {
            return SchemaValidationResult::fail("wire-codec: list exceeds schema capacity");
        }
        for (const auto &item : lv->items) {
            if (!item) {
                return SchemaValidationResult::fail("wire-codec: null List element");
            }
            if (auto r = check(*item, schema.element); !r.valid) {
                return r;
            }
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaMap &schema) {
        const auto *mv = std::get_if<runtime::MapValue>(&value.node);
        if (mv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Map value");
        }
        const auto *key_shape = shape_at(table_, schema.key);
        if (key_shape == nullptr || !std::holds_alternative<CoreWireSchemaString>(*key_shape)) {
            return SchemaValidationResult::fail("wire-codec: map key schema must be String");
        }
        if (!capacity_ok(schema.capacity, mv->entries.size())) {
            return SchemaValidationResult::fail("wire-codec: map exceeds schema capacity");
        }
        // Validate every key/value on the ORIGINAL Value FIRST (P0-7): reject null
        // key/value, require exact-String keys within bounds, and recurse into
        // each value. Only after the whole subtree is proven null-free do we clone
        // for the canonicality comparison, so clone_value can never turn a hostile
        // null into a None that a later structural compare would mis-handle.
        const auto &key_string_schema = std::get<CoreWireSchemaString>(*key_shape);
        for (const auto &[key, val] : mv->entries) {
            if (!key || !val) {
                return SchemaValidationResult::fail("wire-codec: null Map key or value");
            }
            const auto *key_str = std::get_if<runtime::StringValue>(&key->node);
            if (key_str == nullptr) {
                return SchemaValidationResult::fail("wire-codec: map key must be a String value");
            }
            if (!string_len_in_bounds(key_string_schema, key_str->value)) {
                return SchemaValidationResult::fail(
                    "wire-codec: map key length out of schema bounds");
            }
            if (auto r = check(*val, schema.value); !r.valid) {
                return r;
            }
        }
        if (auto canon = check_map_canonical(*mv); !canon.valid) {
            return canon;
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaStruct &schema) {
        const auto *sv = std::get_if<runtime::StructValue>(&value.node);
        if (sv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Struct value");
        }
        if (sv->type_name != schema.wire_name) {
            return SchemaValidationResult::fail("wire-codec: struct type_name mismatch");
        }
        if (sv->fields.size() != schema.fields.size()) {
            return SchemaValidationResult::fail("wire-codec: struct field count mismatch");
        }
        for (const auto &field : schema.fields) {
            const auto *fv = sv->fields.get(field.wire_name);
            if (fv == nullptr) {
                return SchemaValidationResult::fail("wire-codec: struct missing field '" +
                                                    field.wire_name + "'");
            }
            if (auto r = check(*fv, field.type); !r.valid) {
                return r;
            }
        }
        return SchemaValidationResult::ok();
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaEnum &schema) {
        const auto *ev = std::get_if<runtime::EnumValue>(&value.node);
        if (ev == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected Enum value");
        }
        if (ev->enum_name != schema.wire_name) {
            return SchemaValidationResult::fail("wire-codec: enum name mismatch");
        }
        const CoreWireSchemaVariant *variant = nullptr;
        for (const auto &v : schema.variants) {
            if (v.wire_name == ev->variant) {
                variant = &v;
                break;
            }
        }
        if (variant == nullptr) {
            // Schema-only: name the enum, never the observed variant (P0-8).
            return SchemaValidationResult::fail("wire-codec: variant not declared in enum '" +
                                                schema.wire_name + "'");
        }
        switch (variant->payload_kind) {
        case CoreWirePayloadKind::Unit:
            if (!ev->payload.empty() || !ev->named_payload.empty()) {
                return SchemaValidationResult::fail("wire-codec: unit variant carries a payload");
            }
            return SchemaValidationResult::ok();
        case CoreWirePayloadKind::Tuple: {
            if (!ev->named_payload.empty() || ev->payload.size() != variant->slots.size()) {
                return SchemaValidationResult::fail("wire-codec: tuple variant payload arity mismatch");
            }
            for (std::size_t i = 0; i < variant->slots.size(); ++i) {
                if (!ev->payload[i]) {
                    return SchemaValidationResult::fail("wire-codec: null tuple payload element");
                }
                if (auto r = check(*ev->payload[i], variant->slots[i].type); !r.valid) {
                    return r;
                }
            }
            return SchemaValidationResult::ok();
        }
        case CoreWirePayloadKind::Struct: {
            if (!ev->payload.empty() || ev->named_payload.size() != variant->slots.size()) {
                return SchemaValidationResult::fail("wire-codec: struct variant payload mismatch");
            }
            for (const auto &slot : variant->slots) {
                const auto *fv = ev->named_payload.get(slot.wire_name);
                if (fv == nullptr) {
                    return SchemaValidationResult::fail("wire-codec: struct variant missing field '" +
                                                        slot.wire_name + "'");
                }
                if (auto r = check(*fv, slot.type); !r.valid) {
                    return r;
                }
            }
            return SchemaValidationResult::ok();
        }
        }
        return SchemaValidationResult::fail("wire-codec: unknown enum payload kind");
    }

    [[nodiscard]] SchemaValidationResult check_shape(const Value &value,
                                                     const CoreWireSchemaTuple &schema) {
        const auto *lv = std::get_if<runtime::ListValue>(&value.node);
        if (lv == nullptr) {
            return SchemaValidationResult::fail("wire-codec: expected tuple (list) value");
        }
        if (lv->items.size() != schema.elements.size()) {
            return SchemaValidationResult::fail("wire-codec: tuple arity mismatch");
        }
        for (std::size_t i = 0; i < schema.elements.size(); ++i) {
            if (!lv->items[i]) {
                return SchemaValidationResult::fail("wire-codec: null tuple element");
            }
            if (auto r = check(*lv->items[i], schema.elements[i]); !r.valid) {
                return r;
            }
        }
        return SchemaValidationResult::ok();
    }

    // Set canonicality WITHOUT mutating the input (Codex C2b lock #1): clone the
    // items, canonicalize via the existing make_set (which dedups + orders), and
    // require the clone to be structurally equal to the original. No second
    // comparator, no public comparator API. Child/type/null checks read the
    // ORIGINAL Value directly (in the caller), not the canonical copy.
    [[nodiscard]] SchemaValidationResult check_set_canonical(const runtime::SetValue &sv) {
        Value original{runtime::SetValue{}};
        std::vector<Value> clones;
        clones.reserve(sv.items.size());
        auto &orig_items = std::get<runtime::SetValue>(original.node).items;
        orig_items.reserve(sv.items.size());
        for (const auto &item : sv.items) {
            if (!item) {
                return SchemaValidationResult::fail("wire-codec: null Set element");
            }
            clones.push_back(runtime::clone_value(*item));
            orig_items.push_back(std::make_unique<Value>(runtime::clone_value(*item)));
        }
        const Value canonical = runtime::make_set(std::move(clones));
        if (!runtime::structurally_equal(original, canonical)) {
            return SchemaValidationResult::fail(
                "wire-codec: Set is not canonical (unordered or has duplicates)");
        }
        return SchemaValidationResult::ok();
    }

    // Map canonicality WITHOUT mutating the input (Codex C2b lock #1). Requires
    // every key to be a non-null exact StringValue before cloning, since
    // make_map's last-write-wins would otherwise silently collapse duplicates.
    [[nodiscard]] SchemaValidationResult check_map_canonical(const runtime::MapValue &mv) {
        Value original{runtime::MapValue{}};
        std::vector<std::pair<Value, Value>> clones;
        clones.reserve(mv.entries.size());
        auto &orig_entries = std::get<runtime::MapValue>(original.node).entries;
        orig_entries.reserve(mv.entries.size());
        for (const auto &[key, val] : mv.entries) {
            if (!key || !val) {
                return SchemaValidationResult::fail("wire-codec: null Map key or value");
            }
            if (!std::holds_alternative<runtime::StringValue>(key->node)) {
                return SchemaValidationResult::fail("wire-codec: map key must be a String value");
            }
            clones.emplace_back(runtime::clone_value(*key), runtime::clone_value(*val));
            orig_entries.emplace_back(std::make_unique<Value>(runtime::clone_value(*key)),
                                      std::make_unique<Value>(runtime::clone_value(*val)));
        }
        const Value canonical = runtime::make_map(std::move(clones));
        if (!runtime::structurally_equal(original, canonical)) {
            return SchemaValidationResult::fail(
                "wire-codec: Map is not canonical (unordered or has duplicate keys)");
        }
        return SchemaValidationResult::ok();
    }
};

} // namespace

WireDecodeResult decode_json(const json::JsonValue &json,
                             const ir::core::VerifiedWireSchemaBinding &binding) {
    Decoder decoder(binding.table(), DecodePolicy::Exact);
    return decoder.decode(json, binding.root());
}

WireDecodeResult decode_json_legacy_v2(const json::JsonValue &json,
                                       const ir::core::VerifiedWireSchemaBinding &binding) {
    // Same traversal, LegacyV2 policy: the ONLY relaxation is SignedInteger -> Float
    // at a Float node (see the Float decode_shape). Recovery/runtime-internal only.
    Decoder decoder(binding.table(), DecodePolicy::LegacyV2);
    return decoder.decode(json, binding.root());
}

SchemaValidationResult validate_value(const runtime::Value &value,
                                      const ir::core::VerifiedWireSchemaBinding &binding) {
    Validator validator(binding.table());
    return validator.check(value, binding.root());
}

} // namespace ahfl::runtime::wire_codec
