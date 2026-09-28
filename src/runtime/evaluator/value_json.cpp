#include "runtime/evaluator/value_json.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_value.hpp"
#include "base/support/json.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Serialization
// ============================================================================

namespace {

// Locale-independent fixed-decimal int64 spelling. std::to_chars never consults
// the stream's imbued locale, basefield, showbase, or showpos, so the canonical
// wire JSON for an integer is identical across cold starts and unaffected by a
// caller stream's integer formatting flags — unlike `out << (int64_t)`, which
// routes through the locale's num_put facet and can emit grouped/hex/prefixed
// digits. This is the serialization SSOT for both IntValue and TimestampValue.
void write_int64(std::ostream &out, std::int64_t value) {
    char buf[20]; // int64 min "-9223372036854775808" is exactly 20 chars.
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    (void)ec; // 20 bytes always suffice for a base-10 int64.
    out.write(buf, static_cast<std::streamsize>(ptr - buf));
}

void write_json_impl(const Value &v, std::ostream &out) {
    if (const auto *items = list_items(v)) {
        out << '[';
        for (std::size_t i = 0; i < items->size(); ++i) {
            if (i > 0)
                out << ',';
            if ((*items)[i]) {
                write_json_impl(*(*items)[i], out);
            } else {
                out << "null";
            }
        }
        out << ']';
        return;
    }
    if (is_optional(v)) {
        if (const auto *inner = optional_inner(v)) {
            write_json_impl(*inner, out);
        } else {
            out << "null";
        }
        return;
    }

    std::visit(
        [&out](const auto &inner) {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                out << "null";
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                out << (inner.value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, IntValue>) {
                write_int64(out, inner.value);
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                out << format_double(inner.value, /*json_mode=*/true);
            } else if constexpr (std::is_same_v<T, StringValue>) {
                ahfl::write_escaped_json_string(out, inner.value);
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                ahfl::write_escaped_json_string(out, inner.spelling);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                ahfl::write_escaped_json_string(out, inner.spelling);
            } else if constexpr (std::is_same_v<T, StructValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_type");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.type_name);
                for (const auto &[name, val] : inner.fields) {
                    out << ',';
                    ahfl::write_escaped_json_string(out, name);
                    out << ':';
                    if (val) {
                        write_json_impl(*val, out);
                    } else {
                        out << "null";
                    }
                }
                out << '}';
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_enum");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.enum_name);
                out << ',';
                ahfl::write_escaped_json_string(out, "_variant");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.variant);
                if (!inner.payload.empty()) {
                    out << ',';
                    ahfl::write_escaped_json_string(out, "_payload");
                    out << ':';
                    out << '[';
                    for (std::size_t i = 0; i < inner.payload.size(); ++i) {
                        if (i > 0)
                            out << ',';
                        if (inner.payload[i]) {
                            write_json_impl(*inner.payload[i], out);
                        } else {
                            out << "null";
                        }
                    }
                    out << ']';
                }
                if (!inner.named_payload.empty()) {
                    out << ',';
                    ahfl::write_escaped_json_string(out, "_named_payload");
                    out << ':';
                    out << '{';
                    // FieldMap iterates in name-sorted order, giving
                    // deterministic serialization without an explicit sort.
                    bool first = true;
                    for (const auto &[name, value] : inner.named_payload) {
                        if (!first) {
                            out << ',';
                        }
                        first = false;
                        ahfl::write_escaped_json_string(out, name);
                        out << ':';
                        if (value) {
                            write_json_impl(*value, out);
                        } else {
                            out << "null";
                        }
                    }
                    out << '}';
                }
                out << '}';
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_callable");
                out << ':';
                ahfl::write_escaped_json_string(out, "runtime");
                out << '}';
            } else if constexpr (std::is_same_v<T, SetValue>) {
                // Serialize Set as a JSON array; canonical ordering is already
                // baked into the storage, so equal sets serialize identically.
                out << '[';
                for (std::size_t i = 0; i < inner.items.size(); ++i) {
                    if (i > 0)
                        out << ',';
                    if (inner.items[i]) {
                        write_json_impl(*inner.items[i], out);
                    } else {
                        out << "null";
                    }
                }
                out << ']';
            } else if constexpr (std::is_same_v<T, MapValue>) {
                out << '{';
                for (std::size_t i = 0; i < inner.entries.size(); ++i) {
                    if (i > 0)
                        out << ',';
                    // Key serialized as string; non-string keys fall back to
                    // their canonical spelling for round-tripping.
                    if (inner.entries[i].first) {
                        std::ostringstream key_oss;
                        write_json_impl(*inner.entries[i].first, key_oss);
                        out << key_oss.str();
                    } else {
                        out << "null";
                    }
                    out << ':';
                    if (inner.entries[i].second) {
                        write_json_impl(*inner.entries[i].second, out);
                    } else {
                        out << "null";
                    }
                }
                out << '}';
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_uuid");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.hex);
                out << '}';
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_timestamp");
                out << ':';
                write_int64(out, inner.unix_ms);
                out << '}';
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                // B3 (RFC 0013 P3-gaps-B): unit serializes as JSON null,
                // matching the SSA monostate constant and NoneValue. The
                // round-trip asymmetry (null deserializes to NoneValue) is
                // pinned by the value_json unit test.
                out << "null";
            }
        },
        v.node);
}

} // namespace

void write_value_json(const Value &v, std::ostream &out) {
    write_json_impl(v, out);
}

std::string value_to_json(const Value &v) {
    std::ostringstream oss;
    write_json_impl(v, oss);
    return oss.str();
}

std::uint64_t hash_values(const std::vector<Value> &values) {
    // FNV-1a (64-bit). Deterministic across runs: no pointer identity, no
    // allocator order — we hash the canonical JSON bytes of each argument.
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    const auto mix_byte = [&hash](unsigned char byte) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= kPrime;
    };
    const auto mix_length = [&mix_byte](std::size_t length) {
        for (int shift = 0; shift < 64; shift += 8) {
            mix_byte(static_cast<unsigned char>((length >> shift) & 0xFFU));
        }
    };
    mix_length(values.size());
    for (const auto &value : values) {
        const std::string json = value_to_json(value);
        // Length-delimit each argument so ["a","b"] and ["ab"] cannot collide.
        mix_length(json.size());
        for (const char ch : json) {
            mix_byte(static_cast<unsigned char>(ch));
        }
    }
    return hash;
}

// ============================================================================
// Deserialization
// ============================================================================

namespace {

[[nodiscard]] std::optional<Value> value_from_json_value(const ahfl::json::JsonValue &json_value);

[[nodiscard]] const std::string *string_field_value(const ahfl::json::JsonValue &object,
                                                    std::string_view field_name) {
    if (const auto *field = object.get(field_name)) {
        if (field->kind == ahfl::json::Kind::String) {
            return &field->string_val;
        }
    }
    return nullptr;
}

// ----------------------------------------------------------------------------
// Deserialization helpers
// ----------------------------------------------------------------------------

[[nodiscard]] std::optional<Value>
struct_or_enum_from_json_object(const ahfl::json::JsonValue &object) {
    const auto *enum_name = string_field_value(object, "_enum");
    const auto *variant_name = string_field_value(object, "_variant");
    if (enum_name != nullptr && variant_name != nullptr) {
        EnumValue enum_value;
        enum_value.enum_name = *enum_name;
        enum_value.variant = *variant_name;
        if (const auto *payload = object.get("_payload")) {
            if (payload->kind != ahfl::json::Kind::Array) {
                return std::nullopt;
            }
            enum_value.payload.reserve(payload->array_items.size());
            for (const auto &json_item : payload->array_items) {
                if (!json_item) {
                    return std::nullopt;
                }
                auto item_value = value_from_json_value(*json_item);
                if (!item_value.has_value()) {
                    return std::nullopt;
                }
                enum_value.payload.push_back(std::make_unique<Value>(std::move(*item_value)));
            }
        }
        if (const auto *named_payload = object.get("_named_payload")) {
            if (named_payload->kind != ahfl::json::Kind::Object) {
                return std::nullopt;
            }
            for (const auto &[field_name, json_item] : named_payload->object_fields) {
                if (!json_item) {
                    return std::nullopt;
                }
                auto item_value = value_from_json_value(*json_item);
                if (!item_value.has_value()) {
                    return std::nullopt;
                }
                enum_value.named_payload.set(
                    field_name, std::make_unique<Value>(std::move(*item_value)));
            }
        }
        return Value{std::move(enum_value)};
    }

    // RFC P7: UUID marker.
    const auto *uuid_hex = string_field_value(object, "_uuid");
    if (uuid_hex != nullptr) {
        return Value{UuidValue{*uuid_hex}};
    }

    // RFC P7: Timestamp marker. RFC 0026 C2b P0-10: only a genuine signed integer
    // is a valid unix-ms timestamp; an UnsignedInteger / IntegerFallback fails
    // closed rather than reading an unset / approximate field.
    if (const auto *ts_field = object.get("_timestamp")) {
        if (ts_field->kind == ahfl::json::Kind::Int &&
            ts_field->number_provenance == ahfl::json::NumberProvenance::SignedInteger) {
            return Value{TimestampValue{ts_field->int_val}};
        }
        return std::nullopt;
    }

    StructValue struct_value;
    if (const auto *type_name = string_field_value(object, "_type")) {
        struct_value.type_name = *type_name;
    }

    for (const auto &[key, json_field] : object.object_fields) {
        if (key == "_type") {
            continue;
        }
        if (!json_field) {
            return std::nullopt;
        }
        auto field_value = value_from_json_value(*json_field);
        if (!field_value.has_value()) {
            return std::nullopt;
        }
        struct_value.fields.set(key, std::make_unique<Value>(std::move(*field_value)));
    }

    return Value{std::move(struct_value)};
}

[[nodiscard]] std::optional<Value> value_from_json_value(const ahfl::json::JsonValue &json_value) {
    switch (json_value.kind) {
    case ahfl::json::Kind::Null:
        return Value{NoneValue{}};
    case ahfl::json::Kind::Bool:
        return Value{BoolValue{json_value.bool_val}};
    case ahfl::json::Kind::Int:
        // RFC 0026 C2b P0-10: a schema-free Int becomes an IntValue ONLY when it
        // is a genuine signed integer. An UnsignedInteger (high-bit magnitude) or
        // an IntegerFallback is not representable and fails closed here rather than
        // silently degrading (the trust-boundary consumers rely on this).
        if (json_value.number_provenance != ahfl::json::NumberProvenance::SignedInteger) {
            return std::nullopt;
        }
        return Value{IntValue{json_value.int_val}};
    case ahfl::json::Kind::Float:
        // Only a genuine FloatSyntax float is accepted; an IntegerFallback (an
        // integer token too large for any 64-bit integer) fails closed.
        if (json_value.number_provenance != ahfl::json::NumberProvenance::FloatSyntax) {
            return std::nullopt;
        }
        return Value{FloatValue{json_value.float_val}};
    case ahfl::json::Kind::String:
        return Value{StringValue{json_value.string_val}};
    case ahfl::json::Kind::Array: {
        // A JSON array decodes to an AHFL list (its natural JSON encoding).
        Value list = make_list(std::vector<Value>{});
        auto *lv = get_list_if(list);
        for (const auto &json_item : json_value.array_items) {
            if (!json_item) {
                return std::nullopt;
            }
            auto item_value = value_from_json_value(*json_item);
            if (!item_value.has_value()) {
                return std::nullopt;
            }
            lv->items.push_back(std::make_unique<Value>(std::move(*item_value)));
        }
        return list;
    }
    case ahfl::json::Kind::Object:
        return struct_or_enum_from_json_object(json_value);
    }
    return std::nullopt;
}

} // namespace

std::optional<Value> value_from_json(std::string_view json) {
    auto parsed = ahfl::json::parse_json(json);
    if (!parsed.has_value() || !*parsed) {
        return std::nullopt;
    }
    return value_from_json_value(**parsed);
}

std::optional<Value> value_from_json(const ahfl::json::JsonValue &json_value) {
    // Direct DOM entry (RFC 0026 C2b P0-10): decode the already-parsed subtree
    // without a serialize -> reparse round-trip, so numeric provenance survives and
    // ambiguous numbers fail closed rather than silently degrade.
    return value_from_json_value(json_value);
}

} // namespace ahfl::evaluator
