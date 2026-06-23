#include "runtime/evaluator/value.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ahfl::evaluator {

// ============================================================================
// Structural ordering / equality primitives (RFC P7)
// ----------------------------------------------------------------------------
// Used by make_set (de-dup + sort) and make_map (sort by key) so that
// structurally-equal container values always compare equal and print
// deterministically regardless of insertion order. Ordering is intentionally
// total only within same-Kind groups; cross-Kind order follows the ValueNode
// variant index, which keeps it a strict weak ordering.
// ============================================================================

namespace {

/// Returns <0 / 0 / >0 like strcmp. Only supports the primitive / scalar kinds
/// that can legitimately appear as Set elements or Map keys. For non-orderable
/// kinds (Struct / List / Enum / Optional / Set / Map / Uuid / Timestamp) it
/// falls back to a per-kind canonical comparison so the result is still a
/// strict weak ordering; callers use it purely for canonicalization, never for
/// semantic `<` on those kinds.
int compare_values(const Value &lhs, const Value &rhs) {
    const auto li = lhs.node.index();
    const auto ri = rhs.node.index();
    if (li != ri) {
        return li < ri ? -1 : 1;
    }
    return std::visit(
        [&](const auto &inner) -> int {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                return 0;
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                const auto *r = std::get_if<BoolValue>(&rhs.node);
                if (inner.value == r->value)
                    return 0;
                return inner.value ? 1 : -1;
            } else if constexpr (std::is_same_v<T, IntValue>) {
                const auto *r = std::get_if<IntValue>(&rhs.node);
                if (inner.value < r->value)
                    return -1;
                return inner.value > r->value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                const auto *r = std::get_if<FloatValue>(&rhs.node);
                if (inner.value < r->value)
                    return -1;
                return inner.value > r->value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, StringValue>) {
                const auto *r = std::get_if<StringValue>(&rhs.node);
                return inner.value.compare(r->value);
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                const auto *r = std::get_if<DecimalValue>(&rhs.node);
                return inner.spelling.compare(r->spelling);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                const auto *r = std::get_if<DurationValue>(&rhs.node);
                return inner.spelling.compare(r->spelling);
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                const auto *r = std::get_if<UuidValue>(&rhs.node);
                return inner.hex.compare(r->hex);
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                const auto *r = std::get_if<TimestampValue>(&rhs.node);
                if (inner.unix_ms < r->unix_ms)
                    return -1;
                return inner.unix_ms > r->unix_ms ? 1 : 0;
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                const auto *r = std::get_if<EnumValue>(&rhs.node);
                if (int c = inner.enum_name.compare(r->enum_name); c != 0)
                    return c;
                if (int c = inner.variant.compare(r->variant); c != 0)
                    return c;
                if (inner.payload.size() != r->payload.size()) {
                    return inner.payload.size() < r->payload.size() ? -1 : 1;
                }
                for (std::size_t i = 0; i < inner.payload.size(); ++i) {
                    if (!inner.payload[i] && !r->payload[i]) {
                        continue;
                    }
                    if (!inner.payload[i]) {
                        return -1;
                    }
                    if (!r->payload[i]) {
                        return 1;
                    }
                    if (int c = compare_values(*inner.payload[i], *r->payload[i]); c != 0) {
                        return c;
                    }
                }
                return 0;
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                const auto *r = std::get_if<CallableValue>(&rhs.node);
                if (inner.body != r->body) {
                    return inner.body < r->body ? -1 : 1;
                }
                if (inner.captured_context.get() != r->captured_context.get()) {
                    return inner.captured_context.get() < r->captured_context.get() ? -1 : 1;
                }
                if (inner.params.size() != r->params.size()) {
                    return inner.params.size() < r->params.size() ? -1 : 1;
                }
                for (std::size_t i = 0; i < inner.params.size(); ++i) {
                    if (int c = inner.params[i].compare(r->params[i]); c != 0) {
                        return c;
                    }
                }
                return 0;
            } else {
                // Non-orderable composite kinds: compare by canonical spelling
                // to keep a total order (used only for canonicalization).
                std::ostringstream a;
                std::ostringstream b;
                print_value(lhs, a);
                print_value(rhs, b);
                return a.str().compare(b.str());
            }
        },
        lhs.node);
}

} // namespace

bool structurally_equal(const Value &lhs, const Value &rhs) {
    // Implemented in terms of the internal compare_values helper so the
    // public equality function stays consistent with canonical ordering.
    const auto li = lhs.node.index();
    const auto ri = rhs.node.index();
    if (li != ri)
        return false;
    return std::visit(
        [&](const auto &inner) -> bool {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                return true;
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                const auto *r = std::get_if<BoolValue>(&rhs.node);
                return inner.value == r->value;
            } else if constexpr (std::is_same_v<T, IntValue>) {
                const auto *r = std::get_if<IntValue>(&rhs.node);
                return inner.value == r->value;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                const auto *r = std::get_if<FloatValue>(&rhs.node);
                return inner.value == r->value;
            } else if constexpr (std::is_same_v<T, StringValue>) {
                const auto *r = std::get_if<StringValue>(&rhs.node);
                return inner.value == r->value;
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                const auto *r = std::get_if<DecimalValue>(&rhs.node);
                return inner.spelling == r->spelling;
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                const auto *r = std::get_if<DurationValue>(&rhs.node);
                return inner.spelling == r->spelling;
            } else if constexpr (std::is_same_v<T, StructValue>) {
                const auto *r = std::get_if<StructValue>(&rhs.node);
                if (inner.type_name != r->type_name)
                    return false;
                if (inner.fields.size() != r->fields.size())
                    return false;
                for (const auto &[name, val] : inner.fields) {
                    auto it = r->fields.find(name);
                    if (it == r->fields.end())
                        return false;
                    if (!structurally_equal(*val, *it->second))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, ListValue>) {
                const auto *r = std::get_if<ListValue>(&rhs.node);
                if (inner.items.size() != r->items.size())
                    return false;
                for (size_t i = 0; i < inner.items.size(); ++i) {
                    if (!structurally_equal(*inner.items[i], *r->items[i]))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                const auto *r = std::get_if<EnumValue>(&rhs.node);
                if (inner.enum_name != r->enum_name || inner.variant != r->variant ||
                    inner.payload.size() != r->payload.size()) {
                    return false;
                }
                for (size_t i = 0; i < inner.payload.size(); ++i) {
                    if (!inner.payload[i] && !r->payload[i]) {
                        continue;
                    }
                    if (!inner.payload[i] || !r->payload[i]) {
                        return false;
                    }
                    if (!structurally_equal(*inner.payload[i], *r->payload[i])) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                const auto *r = std::get_if<OptionalValue>(&rhs.node);
                if (!inner.inner && !r->inner)
                    return true;
                if (!inner.inner || !r->inner)
                    return false;
                return structurally_equal(*inner.inner, *r->inner);
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                const auto *r = std::get_if<CallableValue>(&rhs.node);
                return inner.body == r->body &&
                       inner.captured_context.get() == r->captured_context.get() &&
                       inner.params == r->params;
            } else if constexpr (std::is_same_v<T, SetValue>) {
                const auto *r = std::get_if<SetValue>(&rhs.node);
                if (inner.items.size() != r->items.size())
                    return false;
                for (size_t i = 0; i < inner.items.size(); ++i) {
                    if (!structurally_equal(*inner.items[i], *r->items[i]))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, MapValue>) {
                const auto *r = std::get_if<MapValue>(&rhs.node);
                if (inner.entries.size() != r->entries.size())
                    return false;
                for (size_t i = 0; i < inner.entries.size(); ++i) {
                    if (!structurally_equal(*inner.entries[i].first, *r->entries[i].first))
                        return false;
                    if (!structurally_equal(*inner.entries[i].second, *r->entries[i].second))
                        return false;
                }
                return true;
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                const auto *r = std::get_if<UuidValue>(&rhs.node);
                return inner.hex == r->hex;
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                const auto *r = std::get_if<TimestampValue>(&rhs.node);
                return inner.unix_ms == r->unix_ms;
            }
            return false;
        },
        lhs.node);
}

// ============================================================================
// value_kind implementation
// ============================================================================

ValueKind value_kind(const Value &v) {
    return std::visit(
        [](const auto &inner) -> ValueKind {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                return ValueKind::None;
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                return ValueKind::Bool;
            } else if constexpr (std::is_same_v<T, IntValue>) {
                return ValueKind::Int;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                return ValueKind::Float;
            } else if constexpr (std::is_same_v<T, StringValue>) {
                return ValueKind::String;
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                return ValueKind::Decimal;
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return ValueKind::Duration;
            } else if constexpr (std::is_same_v<T, StructValue>) {
                return ValueKind::Struct;
            } else if constexpr (std::is_same_v<T, ListValue>) {
                return ValueKind::List;
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                return ValueKind::Enum;
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                return ValueKind::Optional;
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                return ValueKind::Callable;
            } else if constexpr (std::is_same_v<T, SetValue>) {
                return ValueKind::Set;
            } else if constexpr (std::is_same_v<T, MapValue>) {
                return ValueKind::Map;
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                return ValueKind::Uuid;
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                return ValueKind::Timestamp;
            }
        },
        v.node);
}

// ============================================================================
// is_none implementation
// ============================================================================

bool is_none(const Value &v) {
    return std::holds_alternative<NoneValue>(v.node);
}

// ============================================================================
// print_value implementation
// ============================================================================

void print_value(const Value &v, std::ostream &out) {
    std::visit(
        [&out](const auto &inner) {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                out << "none";
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                out << (inner.value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, IntValue>) {
                out << inner.value;
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                out << inner.value;
            } else if constexpr (std::is_same_v<T, StringValue>) {
                out << "\"" << inner.value << "\"";
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                out << inner.spelling;
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                out << inner.spelling;
            } else if constexpr (std::is_same_v<T, StructValue>) {
                out << inner.type_name << " { ";
                bool first = true;
                for (const auto &[name, val] : inner.fields) {
                    if (!first)
                        out << ", ";
                    first = false;
                    out << name << ": ";
                    if (val) {
                        print_value(*val, out);
                    } else {
                        out << "<null>";
                    }
                }
                out << " }";
            } else if constexpr (std::is_same_v<T, ListValue>) {
                out << "[";
                for (size_t i = 0; i < inner.items.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    if (inner.items[i]) {
                        print_value(*inner.items[i], out);
                    } else {
                        out << "<null>";
                    }
                }
                out << "]";
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                out << inner.enum_name << "::" << inner.variant;
                if (!inner.payload.empty()) {
                    out << "(";
                    for (size_t i = 0; i < inner.payload.size(); ++i) {
                        if (i > 0)
                            out << ", ";
                        if (inner.payload[i]) {
                            print_value(*inner.payload[i], out);
                        } else {
                            out << "<null>";
                        }
                    }
                    out << ")";
                }
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                if (inner.inner) {
                    out << "some(";
                    print_value(*inner.inner, out);
                    out << ")";
                } else {
                    out << "none";
                }
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                out << "<lambda/" << inner.params.size() << ">";
            } else if constexpr (std::is_same_v<T, SetValue>) {
                out << "set{";
                for (size_t i = 0; i < inner.items.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    if (inner.items[i]) {
                        print_value(*inner.items[i], out);
                    } else {
                        out << "<null>";
                    }
                }
                out << "}";
            } else if constexpr (std::is_same_v<T, MapValue>) {
                out << "map{";
                for (size_t i = 0; i < inner.entries.size(); ++i) {
                    if (i > 0)
                        out << ", ";
                    if (inner.entries[i].first) {
                        print_value(*inner.entries[i].first, out);
                    } else {
                        out << "<null>";
                    }
                    out << ": ";
                    if (inner.entries[i].second) {
                        print_value(*inner.entries[i].second, out);
                    } else {
                        out << "<null>";
                    }
                }
                out << "}";
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                out << "uuid(" << inner.hex << ")";
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                out << "timestamp(" << inner.unix_ms << ")";
            }
        },
        v.node);
}

// ============================================================================
// Convenience constructor implementations
// ============================================================================

Value make_optional_some(Value inner) {
    return Value{OptionalValue{std::make_unique<Value>(std::move(inner))}};
}

Value make_optional_none() {
    return Value{OptionalValue{nullptr}};
}

Value make_callable(std::vector<std::string> params,
                    const ir::Expr *body,
                    std::shared_ptr<const EvalContext> captured_context) {
    return Value{CallableValue{
        .params = std::move(params),
        .body = body,
        .captured_context = std::move(captured_context),
    }};
}

Value make_struct(std::string type_name, std::unordered_map<std::string, Value> fields) {
    StructValue sv;
    sv.type_name = std::move(type_name);
    for (auto &[name, val] : fields) {
        sv.fields.emplace(name, std::make_unique<Value>(std::move(val)));
    }
    return Value{std::move(sv)};
}

Value make_enum(std::string enum_name, std::string variant, std::vector<Value> payload) {
    EnumValue ev;
    ev.enum_name = std::move(enum_name);
    ev.variant = std::move(variant);
    ev.payload.reserve(payload.size());
    for (auto &item : payload) {
        ev.payload.push_back(std::make_unique<Value>(std::move(item)));
    }
    return Value{std::move(ev)};
}

Value make_list(std::vector<Value> items) {
    ListValue lv;
    for (auto &item : items) {
        lv.items.push_back(std::make_unique<Value>(std::move(item)));
    }
    return Value{std::move(lv)};
}

Value make_set(std::vector<Value> items) {
    // Canonicalize: drop duplicates, then order by structural comparison so
    // that any two structurally-equal inputs collapse to the same value.
    std::vector<Value> canonical;
    canonical.reserve(items.size());
    for (auto &item : items) {
        bool dup = false;
        for (const auto &existing : canonical) {
            if (structurally_equal(existing, item)) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            canonical.push_back(std::move(item));
        }
    }
    std::sort(canonical.begin(), canonical.end(), [](const Value &a, const Value &b) {
        return compare_values(a, b) < 0;
    });

    SetValue sv;
    sv.items.reserve(canonical.size());
    for (auto &v : canonical) {
        sv.items.push_back(std::make_unique<Value>(std::move(v)));
    }
    return Value{std::move(sv)};
}

Value make_map(std::vector<std::pair<Value, Value>> entries) {
    // Canonicalize: last-write-wins on duplicate keys, then order by key.
    std::vector<std::pair<Value, Value>> canonical;
    canonical.reserve(entries.size());
    for (auto &entry : entries) {
        bool found = false;
        for (auto &existing : canonical) {
            if (structurally_equal(existing.first, entry.first)) {
                existing.second = std::move(entry.second);
                found = true;
                break;
            }
        }
        if (!found) {
            canonical.push_back(std::move(entry));
        }
    }
    std::sort(canonical.begin(),
              canonical.end(),
              [](const std::pair<Value, Value> &a, const std::pair<Value, Value> &b) {
                  return compare_values(a.first, b.first) < 0;
              });

    MapValue mv;
    mv.entries.reserve(canonical.size());
    for (auto &kv : canonical) {
        mv.entries.emplace_back(std::make_unique<Value>(std::move(kv.first)),
                                std::make_unique<Value>(std::move(kv.second)));
    }
    return Value{std::move(mv)};
}

std::optional<Value> make_uuid(std::string spelling) {
    // Accept canonical 8-4-4-4-12 hex forms, the dashed-but-uppercase form, and
    // the bare 32-hex form. Normalize to 32 lowercase hex chars without dashes.
    std::string hex;
    hex.reserve(spelling.size());
    for (char c : spelling) {
        if (c == '-' || c == '{' || c == '}' || c == ' ' || c == '\t') {
            continue;
        }
        char lower = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        if (!((lower >= '0' && lower <= '9') || (lower >= 'a' && lower <= 'f'))) {
            return std::nullopt;
        }
        hex.push_back(lower);
    }
    if (hex.size() != 32) {
        return std::nullopt;
    }
    return Value{UuidValue{std::move(hex)}};
}

Value make_timestamp(int64_t unix_ms) {
    return Value{TimestampValue{unix_ms}};
}

// ============================================================================
// clone_value implementation
// ============================================================================

Value clone_value(const Value &v) {
    return std::visit(
        [](const auto &inner) -> Value {
            using T = std::decay_t<decltype(inner)>;
            if constexpr (std::is_same_v<T, NoneValue>) {
                return make_none();
            } else if constexpr (std::is_same_v<T, BoolValue>) {
                return make_bool(inner.value);
            } else if constexpr (std::is_same_v<T, IntValue>) {
                return make_int(inner.value);
            } else if constexpr (std::is_same_v<T, FloatValue>) {
                return make_float(inner.value);
            } else if constexpr (std::is_same_v<T, StringValue>) {
                return make_string(inner.value);
            } else if constexpr (std::is_same_v<T, DecimalValue>) {
                return make_decimal(inner.spelling);
            } else if constexpr (std::is_same_v<T, DurationValue>) {
                return make_duration(inner.spelling);
            } else if constexpr (std::is_same_v<T, StructValue>) {
                StructValue sv;
                sv.type_name = inner.type_name;
                for (const auto &[name, val] : inner.fields) {
                    if (val) {
                        sv.fields.emplace(name, std::make_unique<Value>(clone_value(*val)));
                    }
                }
                return Value{std::move(sv)};
            } else if constexpr (std::is_same_v<T, ListValue>) {
                ListValue lv;
                for (const auto &item : inner.items) {
                    if (item) {
                        lv.items.push_back(std::make_unique<Value>(clone_value(*item)));
                    }
                }
                return Value{std::move(lv)};
            } else if constexpr (std::is_same_v<T, EnumValue>) {
                EnumValue ev;
                ev.enum_name = inner.enum_name;
                ev.variant = inner.variant;
                ev.payload.reserve(inner.payload.size());
                for (const auto &item : inner.payload) {
                    if (item) {
                        ev.payload.push_back(std::make_unique<Value>(clone_value(*item)));
                    } else {
                        ev.payload.push_back(nullptr);
                    }
                }
                return Value{std::move(ev)};
            } else if constexpr (std::is_same_v<T, OptionalValue>) {
                if (inner.inner) {
                    return make_optional_some(clone_value(*inner.inner));
                }
                return make_optional_none();
            } else if constexpr (std::is_same_v<T, CallableValue>) {
                return make_callable(inner.params, inner.body, inner.captured_context);
            } else if constexpr (std::is_same_v<T, SetValue>) {
                SetValue sv;
                sv.items.reserve(inner.items.size());
                for (const auto &item : inner.items) {
                    if (item) {
                        sv.items.push_back(std::make_unique<Value>(clone_value(*item)));
                    }
                }
                return Value{std::move(sv)};
            } else if constexpr (std::is_same_v<T, MapValue>) {
                MapValue mv;
                mv.entries.reserve(inner.entries.size());
                for (const auto &kv : inner.entries) {
                    Value key = kv.first ? clone_value(*kv.first) : make_none();
                    Value val = kv.second ? clone_value(*kv.second) : make_none();
                    mv.entries.emplace_back(std::make_unique<Value>(std::move(key)),
                                            std::make_unique<Value>(std::move(val)));
                }
                return Value{std::move(mv)};
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                return Value{UuidValue{inner.hex}};
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                return Value{TimestampValue{inner.unix_ms}};
            }
        },
        v.node);
}

} // namespace ahfl::evaluator
