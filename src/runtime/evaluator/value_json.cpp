#include "runtime/evaluator/value_json.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/json/json_value.hpp"
#include "base/support/json.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Serialization
// ============================================================================

namespace {

void write_json_impl(const Value &v, std::ostream &out) {
    std::visit(
        [&out](const auto &inner) {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                out << "null";
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                out << (inner.value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, IntValue>) {
                out << inner.value;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                if (std::isnan(inner.value)) {
                    out << "null";
                } else if (std::isinf(inner.value)) {
                    out << "null";
                } else {
                    // Use max precision for roundtrip fidelity
                    char buf[64];
                    auto [ptr, ec] = std::to_chars(buf,
                                                   buf + sizeof(buf),
                                                   inner.value,
                                                   std::chars_format::general,
                                                   std::numeric_limits<double>::max_digits10);
                    std::string_view sv(buf, static_cast<std::size_t>(ptr - buf));
                    out << sv;
                    // Ensure output looks like a float (has '.' or 'e')
                    if (sv.find('.') == std::string_view::npos &&
                        sv.find('e') == std::string_view::npos &&
                        sv.find('E') == std::string_view::npos) {
                        out << ".0";
                    }
                }
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
            } else if constexpr (std::is_same_v<T, ListValue>) {
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
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                out << '{';
                ahfl::write_escaped_json_string(out, "_enum");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.enum_name);
                out << ',';
                ahfl::write_escaped_json_string(out, "_variant");
                out << ':';
                ahfl::write_escaped_json_string(out, inner.variant);
                out << '}';
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                if (inner.inner) {
                    write_json_impl(*inner.inner, out);
                } else {
                    out << "null";
                }
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
                out << inner.unix_ms;
                out << '}';
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

[[nodiscard]] std::optional<Value>
struct_or_enum_from_json_object(const ahfl::json::JsonValue &object) {
    const auto *enum_name = string_field_value(object, "_enum");
    const auto *variant_name = string_field_value(object, "_variant");
    if (enum_name != nullptr && variant_name != nullptr) {
        return Value{EnumValue{*enum_name, *variant_name}};
    }

    // RFC P7: UUID marker.
    const auto *uuid_hex = string_field_value(object, "_uuid");
    if (uuid_hex != nullptr) {
        return Value{UuidValue{*uuid_hex}};
    }

    // RFC P7: Timestamp marker.
    if (const auto *ts_field = object.get("_timestamp")) {
        if (ts_field->kind == ahfl::json::Kind::Int) {
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
        struct_value.fields.emplace(key, std::make_unique<Value>(std::move(*field_value)));
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
        return Value{IntValue{json_value.int_val}};
    case ahfl::json::Kind::Float:
        return Value{FloatValue{json_value.float_val}};
    case ahfl::json::Kind::String:
        return Value{StringValue{json_value.string_val}};
    case ahfl::json::Kind::Array: {
        ListValue list_value;
        for (const auto &json_item : json_value.array_items) {
            if (!json_item) {
                return std::nullopt;
            }
            auto item_value = value_from_json_value(*json_item);
            if (!item_value.has_value()) {
                return std::nullopt;
            }
            list_value.items.push_back(std::make_unique<Value>(std::move(*item_value)));
        }
        return Value{std::move(list_value)};
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

} // namespace ahfl::evaluator
