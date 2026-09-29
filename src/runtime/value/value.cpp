#include "runtime/value/value.hpp"

#include "base/json/json_value.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::runtime {

// ============================================================================
// FieldMap: sorted-by-name flat storage for struct / struct-enum-variant fields
// ----------------------------------------------------------------------------
// Kept sorted by field name so iteration order is a pure function of the field
// set — the invariant RFC 0022 relies on for deterministic `value_json`. The
// sorted position is the field's canonical ordinal (Principle 2). Method bodies
// live here (not the header) because they manipulate `unique_ptr<Value>`, which
// requires `Value` to be a complete type.
// ============================================================================

FieldMap::iterator FieldMap::lower_bound(std::string_view name) {
    return std::lower_bound(entries_.begin(), entries_.end(), name,
                            [](const Entry &e, std::string_view n) { return e.name < n; });
}

FieldMap::const_iterator FieldMap::lower_bound(std::string_view name) const {
    return std::lower_bound(entries_.begin(), entries_.end(), name,
                            [](const Entry &e, std::string_view n) { return e.name < n; });
}

void FieldMap::set(std::string name, std::unique_ptr<Value> value) {
    auto it = lower_bound(name);
    if (it != entries_.end() && it->name == name) {
        it->value = std::move(value);
        return;
    }
    entries_.insert(it, Entry{std::move(name), std::move(value)});
}

FieldMap::iterator FieldMap::find(std::string_view name) {
    auto it = lower_bound(name);
    return (it != entries_.end() && it->name == name) ? it : entries_.end();
}

FieldMap::const_iterator FieldMap::find(std::string_view name) const {
    auto it = lower_bound(name);
    return (it != entries_.end() && it->name == name) ? it : entries_.end();
}

Value *FieldMap::get(std::string_view name) {
    auto it = find(name);
    return it != entries_.end() ? it->value.get() : nullptr;
}

const Value *FieldMap::get(std::string_view name) const {
    auto it = find(name);
    return it != entries_.end() ? it->value.get() : nullptr;
}

// ============================================================================
// Canonical double formatting (RFC 0022 prereq 1b)
// ----------------------------------------------------------------------------
// One locale-independent, shortest-round-trip float renderer shared by
// value_json, print_value, and the conformance-case canonicality gate, so a
// given double serializes identically in every artifact. The wire spelling
// itself lives in ahfl::json::format_wire_float (the base JSON library); this
// wrapper only adds the non-finite diagnostics conventions.
// ============================================================================

std::string format_double(double value, bool json_mode) {
    if (!std::isfinite(value)) {
        // JSON has no NaN/Inf literal; the diagnostic/print path shows the sign.
        if (json_mode) {
            return "null";
        }
        if (std::isnan(value)) {
            return "NaN";
        }
        return value < 0.0 ? "-Infinity" : "Infinity";
    }
    // Delegate to the shared wire SSOT so a given double renders identically in
    // every artifact (value_json, print_value, conformance canonicality gate).
    return json::format_wire_float(value);
}


// ----------------------------------------------------------------------------
// Used by make_set (de-dup + sort) and make_map (sort by key) so that
// structurally-equal container values always compare equal and print
// deterministically regardless of insertion order. Ordering is intentionally
// total only within same-Kind groups; cross-Kind order follows the ValueNode
// variant index, which keeps it a strict weak ordering.
// ============================================================================


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
                if (inner.named_payload.size() != r->named_payload.size()) {
                    return inner.named_payload.size() < r->named_payload.size() ? -1 : 1;
                }
                // FieldMap is name-sorted, so parallel iteration compares
                // fields in a canonical order without collecting/sorting names.
                for (auto li = inner.named_payload.begin(), ri = r->named_payload.begin();
                     li != inner.named_payload.end(); ++li, ++ri) {
                    if (int c = li->name.compare(ri->name); c != 0) {
                        return c;
                    }
                    const auto &lhs_value = li->value;
                    const auto &rhs_value = ri->value;
                    if (!lhs_value && !rhs_value) {
                        continue;
                    }
                    if (!lhs_value) {
                        return -1;
                    }
                    if (!rhs_value) {
                        return 1;
                    }
                    if (int c = compare_values(*lhs_value, *rhs_value); c != 0) {
                        return c;
                    }
                }
                return 0;
            } else if constexpr (std::is_same_v<T, InterpreterClosureHandle>) {
                // Interpreter-only: closures are not wire values. Their id is a
                // monotonic, process-local assignment ordinal, so comparing it
                // gives a total order that is stable for the process lifetime
                // (unlike a heap address, which can alias through reuse).
                const auto *r = std::get_if<InterpreterClosureHandle>(&rhs.node);
                if (inner.id == r->id) {
                    return 0;
                }
                return inner.id < r->id ? -1 : 1;
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                // RFC 0013 P3-gaps-B: unit has exactly one value; trivially equal.
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


bool structurally_equal(const Value &lhs, const Value &rhs) {
    // Same kind and zero under the canonical comparator. Kept as an explicit
    // visitor (rather than compare_values == 0) so each arm reads as the
    // equality contract of its kind.
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
                    if (!structurally_equal(*val, *it->value))
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
                if (inner.named_payload.size() != r->named_payload.size()) {
                    return false;
                }
                for (const auto &[name, value] : inner.named_payload) {
                    const auto rhs_it = r->named_payload.find(name);
                    if (rhs_it == r->named_payload.end()) {
                        return false;
                    }
                    if (!value && !rhs_it->value) {
                        continue;
                    }
                    if (!value || !rhs_it->value) {
                        return false;
                    }
                    if (!structurally_equal(*value, *rhs_it->value)) {
                        return false;
                    }
                }
                return true;
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
            } else if constexpr (std::is_same_v<T, InterpreterClosureHandle>) {
                // Interpreter-only: two closure values are equal iff they are
                // the same closure instance (same monotonic id). Two distinct
                // evaluations of one source lambda are different captures and
                // compare different; clones share the handle and stay equal.
                const auto *r = std::get_if<InterpreterClosureHandle>(&rhs.node);
                return inner.id == r->id;
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                // RFC 0013 P3-gaps-B: unit has exactly one value.
                return true;
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
                if (inner.enum_name == "std::option::Option") {
                    return ValueKind::Optional;
                }
                return ValueKind::Enum;
            } else if constexpr (std::is_same_v<T, SetValue>) {
                return ValueKind::Set;
            } else if constexpr (std::is_same_v<T, MapValue>) {
                return ValueKind::Map;
            } else if constexpr (std::is_same_v<T, UuidValue>) {
                return ValueKind::Uuid;
            } else if constexpr (std::is_same_v<T, TimestampValue>) {
                return ValueKind::Timestamp;
            } else if constexpr (std::is_same_v<T, InterpreterClosureHandle>) {
                return ValueKind::Callable;
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                return ValueKind::Unit;
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
                out << format_double(inner.value, /*json_mode=*/false);
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
                if (inner.enum_name == "std::option::Option") {
                    if (inner.variant == "Some" && inner.payload.size() == 1 &&
                        inner.payload.front()) {
                        out << "some(";
                        print_value(*inner.payload.front(), out);
                        out << ")";
                    } else {
                        out << "none";
                    }
                } else {
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
                    } else if (!inner.named_payload.empty()) {
                        out << " { ";
                        // FieldMap iterates in name-sorted order already.
                        bool first = true;
                        for (const auto &[name, value] : inner.named_payload) {
                            if (!first) {
                                out << ", ";
                            }
                            first = false;
                            out << name << ": ";
                            if (value) {
                                print_value(*value, out);
                            } else {
                                out << "<null>";
                            }
                        }
                        out << " }";
                    }
                }
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
            } else if constexpr (std::is_same_v<T, InterpreterClosureHandle>) {
                // Interpreter-only: render the kind and its stable id without
                // dereferencing the descriptor.
                out << "<lambda/" << inner.id << ">";
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                // RFC 0013 P3-gaps-B: the unit value renders as its literal spelling.
                out << "{}";
            }
        },
        v.node);
}

// ============================================================================
// Convenience constructor implementations
// ============================================================================

namespace {

// Monotonic closure-instance id (Principle 2: id identity, never a heap
// address). Process-local; ids are never put on the wire. Starts at 1 so 0
// stays available as a sentinel for "no closure" in future side tables.
std::uint64_t next_closure_id() {
    static constinit std::atomic<std::uint64_t> counter{1};
    return counter.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

Value make_interpreter_closure(InterpreterClosureRef closure) {
    return Value{InterpreterClosureHandle{
        .id = next_closure_id(),
        .descriptor = std::move(closure),
    }};
}

Value make_struct(std::string type_name, std::unordered_map<std::string, Value> fields) {
    StructValue sv;
    sv.type_name = std::move(type_name);
    for (auto &[name, val] : fields) {
        sv.fields.set(name, std::make_unique<Value>(std::move(val)));
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

Value make_enum(std::string enum_name,
                std::string variant,
                std::unordered_map<std::string, Value> named_payload) {
    EnumValue ev;
    ev.enum_name = std::move(enum_name);
    ev.variant = std::move(variant);
    for (auto &[name, item] : named_payload) {
        ev.named_payload.set(std::move(name), std::make_unique<Value>(std::move(item)));
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
                        sv.fields.set(name, std::make_unique<Value>(clone_value(*val)));
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
                for (const auto &[name, value] : inner.named_payload) {
                    if (value) {
                        ev.named_payload.set(name, std::make_unique<Value>(clone_value(*value)));
                    } else {
                        ev.named_payload.set(name, nullptr);
                    }
                }
                return Value{std::move(ev)};
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
            } else if constexpr (std::is_same_v<T, InterpreterClosureHandle>) {
                // Interpreter-only: the clone is the same closure instance
                // (same id, shared descriptor). Invocation copies the captured
                // EvalContext before binding arguments, so a clone can never
                // mutate another value's environment through the shared handle.
                return Value{inner};
            } else if constexpr (std::is_same_v<T, UnitValue>) {
                // RFC 0013 P3-gaps-B: zero-sized; clone is a fresh unit value.
                return make_unit();
            }
        },
        v.node);
}

} // namespace ahfl::runtime
