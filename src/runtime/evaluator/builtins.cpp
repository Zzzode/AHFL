#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/evaluator.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::evaluator {

namespace {

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

EvalResult make_error(std::string message) {
    EvalResult result;
    result.value = make_none();
    std::move(result.diagnostics.error()).message(std::move(message)).emit();
    return result;
}

EvalResult arg_count_error(size_t expected, size_t got) {
    return make_error("builtin expected " + std::to_string(expected) + " arguments, got " +
                      std::to_string(got));
}

[[nodiscard]] std::optional<int64_t> duration_to_millis(const DurationValue &duration) {
    std::string spelling = duration.spelling;
    bool seconds = false;
    if (spelling.size() >= 2 && spelling.substr(spelling.size() - 2) == "ms") {
        spelling = spelling.substr(0, spelling.size() - 2);
    } else if (!spelling.empty() && spelling.back() == 's') {
        spelling = spelling.substr(0, spelling.size() - 1);
        seconds = true;
    }

    try {
        std::size_t consumed = 0;
        auto millis = std::stoll(spelling, &consumed);
        if (consumed != spelling.size()) {
            return std::nullopt;
        }
        if (seconds) {
            millis *= 1000;
        }
        return millis;
    } catch (...) {
        return std::nullopt;
    }
}

// ----------------------------------------------------------------------------
// Container builtins (List)
// ----------------------------------------------------------------------------

/// list_raw_length(xs: List<T>) -> Int
EvalResult builtin_list_raw_length(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *lv = std::get_if<ListValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(lv->items.size())), {}};
    }
    return make_error("list_raw_length: argument must be a List");
}

/// list_raw_get(xs: List<T>, i: Int) -> T
EvalResult builtin_list_raw_get(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *lv = std::get_if<ListValue>(&args[0].node);
    auto *iv = std::get_if<IntValue>(&args[1].node);
    if (!lv)
        return make_error("list_raw_get: first argument must be a List");
    if (!iv)
        return make_error("list_raw_get: second argument must be an Int");
    int64_t idx = iv->value;
    if (idx < 0 || static_cast<size_t>(idx) >= lv->items.size()) {
        return make_error("list_raw_get: index out of bounds");
    }
    return EvalResult{clone_value(*lv->items[static_cast<size_t>(idx)]), {}};
}

/// list_raw_set(xs: List<T>, i: Int, x: T) -> List<T>
EvalResult builtin_list_raw_set(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3)
        return arg_count_error(3, args.size());
    auto *lv = std::get_if<ListValue>(&args[0].node);
    auto *iv = std::get_if<IntValue>(&args[1].node);
    if (!lv)
        return make_error("list_raw_set: first argument must be a List");
    if (!iv)
        return make_error("list_raw_set: second argument must be an Int");
    int64_t idx = iv->value;
    if (idx < 0 || static_cast<size_t>(idx) >= lv->items.size()) {
        return make_error("list_raw_set: index out of bounds");
    }
    // Functional update — return a new list with the element replaced
    ListValue new_list;
    new_list.items.reserve(lv->items.size());
    for (size_t i = 0; i < lv->items.size(); ++i) {
        if (i == static_cast<size_t>(idx)) {
            new_list.items.push_back(std::make_unique<Value>(clone_value(args[2])));
        } else {
            new_list.items.push_back(std::make_unique<Value>(clone_value(*lv->items[i])));
        }
    }
    return EvalResult{Value{std::move(new_list)}, {}};
}

/// list_raw_alloc(n: Int) -> List<T>
EvalResult builtin_list_raw_alloc(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    auto *iv = std::get_if<IntValue>(&args[0].node);
    if (!iv)
        return make_error("list_raw_alloc: argument must be an Int");
    if (iv->value < 0)
        return make_error("list_raw_alloc: size must be non-negative");
    ListValue list;
    list.items.reserve(static_cast<size_t>(iv->value));
    for (int64_t i = 0; i < iv->value; ++i) {
        list.items.push_back(std::make_unique<Value>(make_none()));
    }
    return EvalResult{Value{std::move(list)}, {}};
}

/// list_from_array(a, b, c, ...) -> List<T>
///
/// Takes any number of arguments and constructs a list containing them,
/// in order.  This is the runtime backing of list literal syntax `[a, b, c]`
/// after desugaring.
EvalResult builtin_list_from_array(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    ListValue list;
    list.items.reserve(args.size());
    for (const auto &arg : args) {
        list.items.push_back(std::make_unique<Value>(clone_value(arg)));
    }
    return EvalResult{Value{std::move(list)}, {}};
}

// ----------------------------------------------------------------------------
// List algorithms (M2-1): sort / dedup / slice. Pure C++ over ListValue,
// comparing elements via the same primitive cascade as cmp_raw_compare.
// ----------------------------------------------------------------------------

// Three-way compare of two primitive Values: -1 / 0 / 1. Mixed / unknown
// kinds compare equal, matching cmp_raw_compare's identity fallback so sort
// and dedup terminate with a total order over primitives.
static int compare_values(const Value &a, const Value &b) {
    if (const auto *ia = std::get_if<IntValue>(&a.node)) {
        if (const auto *ib = std::get_if<IntValue>(&b.node))
            return ia->value < ib->value ? -1 : ia->value > ib->value ? 1 : 0;
    } else if (const auto *fa = std::get_if<FloatValue>(&a.node)) {
        if (const auto *fb = std::get_if<FloatValue>(&b.node))
            return fa->value < fb->value ? -1 : fa->value > fb->value ? 1 : 0;
    } else if (const auto *ba = std::get_if<BoolValue>(&a.node)) {
        if (const auto *bb = std::get_if<BoolValue>(&b.node))
            return (!ba->value && bb->value) ? -1 : (ba->value && !bb->value) ? 1 : 0;
    } else if (const auto *sa = std::get_if<StringValue>(&a.node)) {
        if (const auto *sb = std::get_if<StringValue>(&b.node)) {
            const int c = sa->value.compare(sb->value);
            return c < 0 ? -1 : c > 0 ? 1 : 0;
        }
    }
    return 0;
}

/// list_sort<T>(xs: List<T>) -> List<T> — stable ascending sort over primitive T.
EvalResult builtin_list_sort(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *lv = std::get_if<ListValue>(&args[0].node);
    if (lv == nullptr)
        return make_error("list_sort: argument must be a List");
    std::vector<const Value *> elems;
    elems.reserve(lv->items.size());
    for (const auto &item : lv->items) elems.push_back(item.get());
    std::stable_sort(elems.begin(), elems.end(),
                     [](const Value *a, const Value *b) { return compare_values(*a, *b) < 0; });
    ListValue out;
    out.items.reserve(elems.size());
    for (const Value *e : elems) out.items.push_back(std::make_unique<Value>(clone_value(*e)));
    return EvalResult{Value{std::move(out)}, {}};
}

/// list_dedup<T>(xs: List<T>) -> List<T> — drop consecutive equal elements
/// (call after sort for full uniqueness).
EvalResult builtin_list_dedup(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *lv = std::get_if<ListValue>(&args[0].node);
    if (lv == nullptr)
        return make_error("list_dedup: argument must be a List");
    ListValue out;
    for (const auto &item : lv->items) {
        if (!out.items.empty() && compare_values(*out.items.back(), *item) == 0)
            continue;
        out.items.push_back(std::make_unique<Value>(clone_value(*item)));
    }
    return EvalResult{Value{std::move(out)}, {}};
}

// ----------------------------------------------------------------------------
// Container builtins (Set)
// ----------------------------------------------------------------------------

/// set_raw_size(s: Set<T>) -> Int
EvalResult builtin_set_raw_size(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *sv = std::get_if<SetValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(sv->items.size())), {}};
    }
    return make_error("set_raw_size: argument must be a Set");
}

/// set_raw_contains(s: Set<T>, x: T) -> Bool
EvalResult builtin_set_raw_contains(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *sv = std::get_if<SetValue>(&args[0].node);
    if (!sv)
        return make_error("set_raw_contains: first argument must be a Set");
    for (const auto &item : sv->items) {
        if (structurally_equal(*item, args[1])) {
            return EvalResult{make_bool(true), {}};
        }
    }
    return EvalResult{make_bool(false), {}};
}

/// set_from_array(a, b, c, ...) -> Set<T>
///
/// Takes any number of arguments and constructs a set containing them.
/// Duplicates are removed. This is the runtime backing of set literal
/// syntax `{a, b, c}` after desugaring.
EvalResult builtin_set_from_array(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    SetValue set;
    for (const auto &arg : args) {
        // Deduplicate: skip if structurally equal to an existing item.
        bool duplicate = false;
        for (const auto &existing : set.items) {
            if (structurally_equal(*existing, arg)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            set.items.push_back(std::make_unique<Value>(clone_value(arg)));
        }
    }
    return EvalResult{Value{std::move(set)}, {}};
}

EvalResult builtin_set_raw_empty(const std::vector<Value> &args, const EvalContext &ctx) {
    if (!args.empty())
        return arg_count_error(0, args.size());
    return builtin_set_from_array(args, ctx);
}

EvalResult builtin_set_raw_singleton(const std::vector<Value> &args, const EvalContext &ctx) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    return builtin_set_from_array(args, ctx);
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// Set algebra (M2-2): union / intersection / difference / is_subset. Pure C++
// over SetValue.items with structurally_equal membership; no element-type
// callback needed (unlike sort_by / map_values).
// ----------------------------------------------------------------------------

EvalResult builtin_set_union(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *a = std::get_if<SetValue>(&args[0].node);
    const auto *b = std::get_if<SetValue>(&args[1].node);
    if (a == nullptr || b == nullptr)
        return make_error("set_union: (Set, Set) required");
    SetValue out;
    const auto add_all = [&](const SetValue *src) {
        for (const auto &e : src->items) {
            bool present = false;
            for (const auto &o : out.items)
                if (structurally_equal(*o, *e)) { present = true; break; }
            if (!present) out.items.push_back(std::make_unique<Value>(clone_value(*e)));
        }
    };
    add_all(a);
    add_all(b);
    return EvalResult{Value{std::move(out)}, {}};
}

EvalResult builtin_set_intersection(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *a = std::get_if<SetValue>(&args[0].node);
    const auto *b = std::get_if<SetValue>(&args[1].node);
    if (a == nullptr || b == nullptr)
        return make_error("set_intersection: (Set, Set) required");
    SetValue out;
    for (const auto &ea : a->items) {
        for (const auto &eb : b->items) {
            if (structurally_equal(*ea, *eb)) {
                out.items.push_back(std::make_unique<Value>(clone_value(*ea)));
                break;
            }
        }
    }
    return EvalResult{Value{std::move(out)}, {}};
}

EvalResult builtin_set_difference(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *a = std::get_if<SetValue>(&args[0].node);
    const auto *b = std::get_if<SetValue>(&args[1].node);
    if (a == nullptr || b == nullptr)
        return make_error("set_difference: (Set, Set) required");
    SetValue out;
    for (const auto &ea : a->items) {
        bool in_b = false;
        for (const auto &eb : b->items)
            if (structurally_equal(*ea, *eb)) { in_b = true; break; }
        if (!in_b) out.items.push_back(std::make_unique<Value>(clone_value(*ea)));
    }
    return EvalResult{Value{std::move(out)}, {}};
}

EvalResult builtin_set_is_subset(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *a = std::get_if<SetValue>(&args[0].node);
    const auto *b = std::get_if<SetValue>(&args[1].node);
    if (a == nullptr || b == nullptr)
        return make_error("set_is_subset: (Set, Set) required");
    for (const auto &ea : a->items) {
        bool in_b = false;
        for (const auto &eb : b->items)
            if (structurally_equal(*ea, *eb)) { in_b = true; break; }
        if (!in_b) return EvalResult{make_bool(false), {}};
    }
    return EvalResult{make_bool(true), {}};
}

// Container builtins (Map)
// ----------------------------------------------------------------------------

/// map_raw_size(m: Map<K, V>) -> Int
EvalResult builtin_map_raw_size(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *mv = std::get_if<MapValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(mv->entries.size())), {}};
    }
    return make_error("map_raw_size: argument must be a Map");
}

/// map_raw_contains_key(m: Map<K, V>, k: K) -> Bool
EvalResult builtin_map_raw_contains_key(const std::vector<Value> &args,
                                        const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *mv = std::get_if<MapValue>(&args[0].node);
    if (!mv)
        return make_error("map_raw_contains_key: first argument must be a Map");
    for (const auto &entry : mv->entries) {
        if (structurally_equal(*entry.first, args[1])) {
            return EvalResult{make_bool(true), {}};
        }
    }
    return EvalResult{make_bool(false), {}};
}

/// map_raw_get(m: Map<K, V>, k: K) -> V
EvalResult builtin_map_raw_get(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *mv = std::get_if<MapValue>(&args[0].node);
    if (!mv)
        return make_error("map_raw_get: first argument must be a Map");
    for (const auto &entry : mv->entries) {
        if (structurally_equal(*entry.first, args[1])) {
            return EvalResult{clone_value(*entry.second), {}};
        }
    }
    return make_error("map_raw_get: key not found");
}

/// map_from_entries(k1, v1, k2, v2, ...) -> Map<K, V>
///
/// Takes an even number of arguments interpreted as alternating key-value
/// pairs and constructs a map.  Duplicate keys keep the last value
/// (last-write-wins).  This is the runtime backing of map literal syntax
/// `{k1: v1, k2: v2}` after desugaring.
EvalResult builtin_map_from_entries(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() % 2 != 0) {
        return make_error("map_from_entries: expected even number of arguments");
    }
    MapValue map;
    for (size_t i = 0; i < args.size(); i += 2) {
        const Value &key = args[i];
        const Value &val = args[i + 1];
        // Last-write-wins for duplicate keys: remove existing entry first.
        bool found = false;
        for (auto it = map.entries.begin(); it != map.entries.end(); ++it) {
            if (structurally_equal(*(*it).first, key)) {
                (*it).second = std::make_unique<Value>(clone_value(val));
                found = true;
                break;
            }
        }
        if (!found) {
            map.entries.emplace_back(std::make_unique<Value>(clone_value(key)),
                                     std::make_unique<Value>(clone_value(val)));
        }
    }
    return EvalResult{Value{std::move(map)}, {}};
}

EvalResult builtin_map_raw_empty(const std::vector<Value> &args, const EvalContext &ctx) {
    if (!args.empty())
        return arg_count_error(0, args.size());
    return builtin_map_from_entries(args, ctx);
}

EvalResult builtin_map_raw_singleton(const std::vector<Value> &args, const EvalContext &ctx) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    return builtin_map_from_entries(args, ctx);
}

// Map enumeration + functional insert (M2-3): back pure-AHFL map_values /
// filter_keys without needing a closure-calling builtin.
EvalResult builtin_map_raw_keys(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *mv = std::get_if<MapValue>(&args[0].node);
    if (mv == nullptr)
        return make_error("map_raw_keys: argument must be a Map");
    ListValue out;
    out.items.reserve(mv->entries.size());
    for (const auto &entry : mv->entries)
        out.items.push_back(std::make_unique<Value>(clone_value(*entry.first)));
    return EvalResult{Value{std::move(out)}, {}};
}

EvalResult builtin_map_raw_values(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *mv = std::get_if<MapValue>(&args[0].node);
    if (mv == nullptr)
        return make_error("map_raw_values: argument must be a Map");
    ListValue out;
    out.items.reserve(mv->entries.size());
    for (const auto &entry : mv->entries)
        out.items.push_back(std::make_unique<Value>(clone_value(*entry.second)));
    return EvalResult{Value{std::move(out)}, {}};
}

/// map_raw_insert<K, V>(m, k, v) -> Map<K, V>: functional insert — returns a
/// new map with k mapped to v (replacing if present).
EvalResult builtin_map_raw_insert(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3)
        return arg_count_error(3, args.size());
    const auto *mv = std::get_if<MapValue>(&args[0].node);
    if (mv == nullptr)
        return make_error("map_raw_insert: (Map, K, V) required");
    MapValue out;
    bool replaced = false;
    for (const auto &entry : mv->entries) {
        if (structurally_equal(*entry.first, args[1])) {
            out.entries.emplace_back(std::make_unique<Value>(clone_value(args[1])),
                                     std::make_unique<Value>(clone_value(args[2])));
            replaced = true;
        } else {
            out.entries.emplace_back(std::make_unique<Value>(clone_value(*entry.first)),
                                     std::make_unique<Value>(clone_value(*entry.second)));
        }
    }
    if (!replaced) {
        out.entries.emplace_back(std::make_unique<Value>(clone_value(args[1])),
                                 std::make_unique<Value>(clone_value(args[2])));
    }
    return EvalResult{Value{std::move(out)}, {}};
}

// ----------------------------------------------------------------------------
// String builtins
// ----------------------------------------------------------------------------

/// string_raw_length(s: String) -> Int
EvalResult builtin_string_raw_length(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *sv = std::get_if<StringValue>(&args[0].node)) {
        // P5 placeholder: returns byte count (codepoint handling deferred to
        // proper stdlib string implementation)
        return EvalResult{make_int(static_cast<int64_t>(sv->value.size())), {}};
    }
    return make_error("string_raw_length: argument must be a String");
}

/// string_raw_concat(a: String, b: String) -> String
EvalResult builtin_string_raw_concat(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<StringValue>(&args[0].node);
    auto *b = std::get_if<StringValue>(&args[1].node);
    if (!a || !b)
        return make_error("string_raw_concat: both arguments must be Strings");
    return EvalResult{make_string(a->value + b->value), {}};
}

/// string_raw_substring(s: String, start: Int, end: Int) -> String
/// Clamps start/end to [0, len], and returns "" on reversed range.
EvalResult builtin_string_raw_substring(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3)
        return arg_count_error(3, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    const auto *start_i = std::get_if<IntValue>(&args[1].node);
    const auto *end_i = std::get_if<IntValue>(&args[2].node);
    if (!sv || !start_i || !end_i)
        return make_error("string_raw_substring: (String, Int, Int) required");
    const auto len = static_cast<int64_t>(sv->value.size());
    auto s = start_i->value;
    auto e = end_i->value;
    if (s < 0) s = 0;
    if (e < 0) e = 0;
    if (s > len) s = len;
    if (e > len) e = len;
    if (e <= s) return EvalResult{make_string(""), {}};
    const auto count = static_cast<size_t>(e - s);
    return EvalResult{make_string(sv->value.substr(static_cast<size_t>(s), count)), {}};
}

EvalResult builtin_string_is_empty(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *value = std::get_if<StringValue>(&args[0].node);
    if (value == nullptr)
        return make_error("string_is_empty: argument must be a String");
    return EvalResult{make_bool(value->value.empty()), {}};
}

EvalResult builtin_string_contains(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *haystack = std::get_if<StringValue>(&args[0].node);
    const auto *needle = std::get_if<StringValue>(&args[1].node);
    if (haystack == nullptr || needle == nullptr) {
        return make_error("string_contains: both arguments must be Strings");
    }
    return EvalResult{make_bool(haystack->value.find(needle->value) != std::string::npos), {}};
}

EvalResult builtin_string_starts_with(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *value = std::get_if<StringValue>(&args[0].node);
    const auto *prefix = std::get_if<StringValue>(&args[1].node);
    if (value == nullptr || prefix == nullptr) {
        return make_error("string_starts_with: both arguments must be Strings");
    }
    return EvalResult{make_bool(value->value.starts_with(prefix->value)), {}};
}

EvalResult builtin_string_ends_with(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *value = std::get_if<StringValue>(&args[0].node);
    const auto *suffix = std::get_if<StringValue>(&args[1].node);
    if (value == nullptr || suffix == nullptr) {
        return make_error("string_ends_with: both arguments must be Strings");
    }
    return EvalResult{make_bool(value->value.ends_with(suffix->value)), {}};
}

/// string_trim(s: String) -> String — strip ASCII whitespace from both ends.
EvalResult builtin_string_trim(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_trim: argument must be a String");
    constexpr std::string_view ws = " \t\n\r\f\v";
    const auto start = sv->value.find_first_not_of(ws);
    if (start == std::string::npos)
        return EvalResult{make_string(""), {}};
    const auto end = sv->value.find_last_not_of(ws);
    return EvalResult{make_string(sv->value.substr(start, end - start + 1)), {}};
}

/// string_trim_start(s: String) -> String — strip leading ASCII whitespace.
EvalResult builtin_string_trim_start(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_trim_start: argument must be a String");
    constexpr std::string_view ws = " \t\n\r\f\v";
    const auto start = sv->value.find_first_not_of(ws);
    if (start == std::string::npos)
        return EvalResult{make_string(""), {}};
    return EvalResult{make_string(sv->value.substr(start)), {}};
}

/// string_trim_end(s: String) -> String — strip trailing ASCII whitespace.
EvalResult builtin_string_trim_end(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_trim_end: argument must be a String");
    constexpr std::string_view ws = " \t\n\r\f\v";
    const auto end = sv->value.find_last_not_of(ws);
    if (end == std::string::npos)
        return EvalResult{make_string(""), {}};
    return EvalResult{make_string(sv->value.substr(0, end + 1)), {}};
}

/// string_replace(s: String, from: String, to: String) -> String — replace
/// every non-overlapping occurrence of `from` with `to`. Empty `from` is a
/// no-op (matches Rust std::string::replace behaviour on empty needle).
EvalResult builtin_string_replace(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3)
        return arg_count_error(3, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    const auto *from = std::get_if<StringValue>(&args[1].node);
    const auto *to = std::get_if<StringValue>(&args[2].node);
    if (sv == nullptr || from == nullptr || to == nullptr)
        return make_error("string_replace: (String, String, String) required");
    std::string result = sv->value;
    if (!from->value.empty()) {
        std::string out;
        out.reserve(result.size());
        size_t pos = 0;
        size_t last = 0;
        while ((pos = result.find(from->value, last)) != std::string::npos) {
            out.append(result, last, pos - last);
            out.append(to->value);
            last = pos + from->value.size();
        }
        out.append(result, last, std::string::npos);
        result = std::move(out);
    }
    return EvalResult{make_string(result), {}};
}

/// string_split(s: String, sep: String) -> List<String>. Empty sep splits into
/// individual characters; otherwise splits on every non-overlapping occurrence
/// of sep (trailing empty field preserved, Rust str::split semantics).
EvalResult builtin_string_split(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    const auto *sep = std::get_if<StringValue>(&args[1].node);
    if (sv == nullptr || sep == nullptr)
        return make_error("string_split: (String, String) required");
    ListValue list;
    const auto &s = sv->value;
    const auto &sep_s = sep->value;
    if (sep_s.empty()) {
        list.items.reserve(s.size());
        for (const char c : s) {
            list.items.push_back(std::make_unique<Value>(make_string(std::string(1, c))));
        }
        return EvalResult{Value{std::move(list)}, {}};
    }
    std::size_t start = 0;
    std::size_t pos = 0;
    while ((pos = s.find(sep_s, start)) != std::string::npos) {
        list.items.push_back(std::make_unique<Value>(make_string(s.substr(start, pos - start))));
        start = pos + sep_s.size();
    }
    list.items.push_back(std::make_unique<Value>(make_string(s.substr(start))));
    return EvalResult{Value{std::move(list)}, {}};
}

/// string_parse_int(s: String) -> Option<Int>. Parses a base-10 integer
/// spanning the entire string; returns None on any trailing garbage or overflow.
EvalResult builtin_string_parse_int(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_parse_int: argument must be a String");
    const auto &s = sv->value;
    long long n = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), n);
    if (res.ec == std::errc{} && res.ptr == s.data() + s.size()) {
        return EvalResult{make_option_some(make_int(static_cast<int64_t>(n))), {}};
    }
    return EvalResult{make_option_none(), {}};
}

/// string_split_whitespace(s: String) -> List<String>. Splits on runs of ASCII
/// whitespace; leading/trailing/consecutive whitespace produces no empty fields
/// (Rust str::split_whitespace semantics).
EvalResult builtin_string_split_whitespace(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_split_whitespace: argument must be a String");
    constexpr std::string_view ws = " \t\n\r\f\v";
    ListValue list;
    const auto &s = sv->value;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        while (i < n && ws.find(s[i]) != std::string_view::npos) ++i;
        if (i >= n) break;
        const std::size_t start = i;
        while (i < n && ws.find(s[i]) == std::string_view::npos) ++i;
        list.items.push_back(std::make_unique<Value>(make_string(s.substr(start, i - start))));
    }
    return EvalResult{Value{std::move(list)}, {}};
}

/// string_parse_float(s: String) -> Option<Float>. Parses a floating-point
/// literal spanning the entire string; None on trailing garbage or overflow.
EvalResult builtin_string_parse_float(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *sv = std::get_if<StringValue>(&args[0].node);
    if (sv == nullptr)
        return make_error("string_parse_float: argument must be a String");
    const auto &s = sv->value;
    double d = 0.0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), d);
    if (res.ec == std::errc{} && res.ptr == s.data() + s.size()) {
        return EvalResult{make_option_some(make_float(d)), {}};
    }
    return EvalResult{make_option_none(), {}};
}

// ----------------------------------------------------------------------------
// Numeric builtins
// ----------------------------------------------------------------------------

/// int_to_string(n: Int) -> String
EvalResult builtin_int_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *iv = std::get_if<IntValue>(&args[0].node)) {
        return EvalResult{make_string(std::to_string(iv->value)), {}};
    }
    return make_error("int_to_string: argument must be an Int");
}

/// bool_to_string(b: Bool) -> String
EvalResult builtin_bool_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *bv = std::get_if<BoolValue>(&args[0].node)) {
        return EvalResult{make_string(bv->value ? "true" : "false"), {}};
    }
    return make_error("bool_to_string: argument must be a Bool");
}

/// float_to_string(x: Float) -> String
EvalResult builtin_float_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *fv = std::get_if<FloatValue>(&args[0].node)) {
        std::ostringstream oss;
        oss << fv->value;
        return EvalResult{make_string(oss.str()), {}};
    }
    return make_error("float_to_string: argument must be a Float");
}

/// float_trunc_to_int(x: Float) -> Int
/// Truncates toward zero; saturates on out-of-range.
EvalResult builtin_float_trunc_to_int(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *fv = std::get_if<FloatValue>(&args[0].node)) {
        const double v = fv->value;
        if (v >= static_cast<double>(std::numeric_limits<int64_t>::max()))
            return EvalResult{make_int(std::numeric_limits<int64_t>::max()), {}};
        if (v <= static_cast<double>(std::numeric_limits<int64_t>::min()))
            return EvalResult{make_int(std::numeric_limits<int64_t>::min()), {}};
        if (std::isnan(v) || std::isinf(v))
            return EvalResult{make_int(0), {}};
        return EvalResult{make_int(static_cast<int64_t>(v)), {}};
    }
    return make_error("float_trunc_to_int: argument must be a Float");
}

/// int_to_float(n: Int) -> Float
/// Widening conversion; exact for values up to 2^53.
EvalResult builtin_int_to_float(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    if (auto *iv = std::get_if<IntValue>(&args[0].node)) {
        return EvalResult{make_float(static_cast<double>(iv->value)), {}};
    }
    return make_error("int_to_float: argument must be an Int");
}

/// string_raw_compare(a: String, b: String) -> Int (-1/0/1)
EvalResult builtin_string_raw_compare(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<StringValue>(&args[0].node);
    auto *b = std::get_if<StringValue>(&args[1].node);
    if (!a || !b)
        return make_error("string_raw_compare: both arguments must be Strings");
    int c = a->value.compare(b->value);
    int64_t sign = (c < 0) ? -1 : (c > 0) ? 1 : 0;
    return EvalResult{make_int(sign), {}};
}

/// cmp_raw_compare<T>(left: T, right: T) -> Ordering
///
/// P6a pragmatic generic comparison: runtime if-cascade over primitive types
/// (Int / Float / Bool / String). For unknown types returns Equal identity so
/// min / max / clamp still terminate deterministically until Ord trait dispatch
/// lands (see std/cmp.ahfl TODO(P6a-Ord) marker).
EvalResult builtin_cmp_raw_compare(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto &lhs = args[0];
    const auto &rhs = args[1];

    if (std::holds_alternative<IntValue>(lhs.node) &&
        std::holds_alternative<IntValue>(rhs.node)) {
        int64_t a = std::get<IntValue>(lhs.node).value;
        int64_t b = std::get<IntValue>(rhs.node).value;
        const char *v = (a < b) ? "Less" : (a > b) ? "Greater" : "Equal";
        return EvalResult{make_enum("std::cmp::Ordering", v), {}};
    }
    if (std::holds_alternative<FloatValue>(lhs.node) &&
        std::holds_alternative<FloatValue>(rhs.node)) {
        double a = std::get<FloatValue>(lhs.node).value;
        double b = std::get<FloatValue>(rhs.node).value;
        const char *v = (a < b) ? "Less" : (a > b) ? "Greater" : "Equal";
        return EvalResult{make_enum("std::cmp::Ordering", v), {}};
    }
    if (std::holds_alternative<BoolValue>(lhs.node) &&
        std::holds_alternative<BoolValue>(rhs.node)) {
        bool a = std::get<BoolValue>(lhs.node).value;
        bool b = std::get<BoolValue>(rhs.node).value;
        const char *v = (!a && b)   ? "Less"
                         : (a && !b) ? "Greater"
                                     : "Equal";
        return EvalResult{make_enum("std::cmp::Ordering", v), {}};
    }
    if (std::holds_alternative<StringValue>(lhs.node) &&
        std::holds_alternative<StringValue>(rhs.node)) {
        const auto &a = std::get<StringValue>(lhs.node).value;
        const auto &b = std::get<StringValue>(rhs.node).value;
        int c = a.compare(b);
        const char *v = (c < 0) ? "Less" : (c > 0) ? "Greater" : "Equal";
        return EvalResult{make_enum("std::cmp::Ordering", v), {}};
    }
    // Identity fallback for unknown T: Equal.
    return EvalResult{make_enum("std::cmp::Ordering", "Equal"), {}};
}

// ----------------------------------------------------------------------------
// Time builtins
// ----------------------------------------------------------------------------

/// wall_clock_now() -> Timestamp  (effect: Nondet)
EvalResult builtin_wall_clock_now(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (!args.empty())
        return arg_count_error(0, args.size());
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return EvalResult{make_timestamp(ms), {}};
}

/// timestamp_add(t: Timestamp, d: Duration) -> Timestamp
EvalResult builtin_timestamp_add(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *tv = std::get_if<TimestampValue>(&args[0].node);
    auto *dv = std::get_if<DurationValue>(&args[1].node);
    if (!tv)
        return make_error("timestamp_add: first argument must be a Timestamp");
    if (!dv)
        return make_error("timestamp_add: second argument must be a Duration");
    const auto ms = duration_to_millis(*dv);
    if (!ms.has_value())
        return make_error("timestamp_add: cannot parse duration");
    return EvalResult{make_timestamp(tv->unix_ms + *ms), {}};
}

EvalResult builtin_timestamp_sub(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *tv = std::get_if<TimestampValue>(&args[0].node);
    auto *dv = std::get_if<DurationValue>(&args[1].node);
    if (!tv)
        return make_error("timestamp_sub: first argument must be a Timestamp");
    if (!dv)
        return make_error("timestamp_sub: second argument must be a Duration");
    const auto ms = duration_to_millis(*dv);
    if (!ms.has_value())
        return make_error("timestamp_sub: cannot parse duration");
    return EvalResult{make_timestamp(tv->unix_ms - *ms), {}};
}

EvalResult builtin_duration_between(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    const auto *start = std::get_if<TimestampValue>(&args[0].node);
    const auto *end = std::get_if<TimestampValue>(&args[1].node);
    if (start == nullptr || end == nullptr) {
        return make_error("time_duration_between: both arguments must be Timestamps");
    }
    return EvalResult{make_duration(std::to_string(end->unix_ms - start->unix_ms)), {}};
}

// ----------------------------------------------------------------------------
// UUID builtins
// ----------------------------------------------------------------------------

/// uuid_new() -> UUID  (effect: Nondet)
EvalResult builtin_uuid_new(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (!args.empty())
        return arg_count_error(0, args.size());
    // P5 placeholder: simple random v4-like UUID (not cryptographically secure)
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(0, 0xF);
    std::string hex;
    hex.reserve(32);
    const char *hex_chars = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        hex.push_back(hex_chars[dist(rng)]);
    }
    // Set version (4) and variant (8/9/a/b) — byte 12 and 14
    hex[12] = '4';
    int variant_nibble = dist(rng) & 0x3;
    hex[16] = "89ab"[variant_nibble];
    if (auto v = make_uuid(hex)) {
        EvalResult result;
        result.value = std::move(*v);
        return result;
    }
    return make_error("uuid_new: internal error");
}

/// uuid_from_string(s: String) -> Option<UUID>
EvalResult builtin_uuid_from_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    auto *sv = std::get_if<StringValue>(&args[0].node);
    if (!sv)
        return make_error("uuid_from_string: argument must be a String");
    if (auto v = make_uuid(sv->value)) {
        return EvalResult{make_optional_some(std::move(*v)), {}};
    }
    return EvalResult{make_optional_none(), {}};
}

EvalResult builtin_uuid_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    const auto *uuid = std::get_if<UuidValue>(&args[0].node);
    if (uuid == nullptr)
        return make_error("uuid_to_string: argument must be a UUID");
    return EvalResult{make_string(uuid->hex), {}};
}

// ----------------------------------------------------------------------------
// JSON builtins — recursive-descent parser + emitter.
// ----------------------------------------------------------------------------

namespace json_impl {

using JsonV = Value; // runtime EnumValue carrying std::json::JsonValue

// ---- JSON text builder (escapes per RFC 8259) ----
void json_escape(std::string &out, std::string_view in) {
    out.push_back('"');
    for (char c : in) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

[[nodiscard]] bool emit_value(const JsonV &v, std::string &out);

[[nodiscard]] bool emit_enum_payload_list(const std::vector<std::unique_ptr<Value>> &items,
                                          std::string &out) {
    out.push_back('[');
    for (size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out.push_back(',');
        if (!emit_value(*items[i], out)) return false;
    }
    out.push_back(']');
    return true;
}

[[nodiscard]] bool emit_value(const JsonV &v, std::string &out) {
    if (auto *ev = std::get_if<EnumValue>(&v.node)) {
        const auto &var = ev->variant;
        if (var == "JNull") {
            out += "null";
            return true;
        }
        if (var == "JBool") {
            const Value *p = ev->associated.get();
            if (p == nullptr) return false;
            const auto *bv = std::get_if<BoolValue>(&p->node);
            if (bv == nullptr) return false;
            out += (bv->value ? "true" : "false");
            return true;
        }
        if (var == "JInt") {
            const Value *p = ev->associated.get();
            if (p == nullptr) return false;
            const auto *iv = std::get_if<IntValue>(&p->node);
            if (iv == nullptr) return false;
            out += std::to_string(iv->value);
            return true;
        }
        if (var == "JFloat") {
            const Value *p = ev->associated.get();
            if (p == nullptr) return false;
            const auto *fv = std::get_if<FloatValue>(&p->node);
            if (fv == nullptr) return false;
            std::ostringstream oss;
            oss << fv->value;
            out += oss.str();
            return true;
        }
        if (var == "JText") {
            const Value *p = ev->associated.get();
            if (p == nullptr) return false;
            const auto *sv = std::get_if<StringValue>(&p->node);
            if (sv == nullptr) return false;
            json_escape(out, sv->value);
            return true;
        }
        if (var == "JList") {
            // Payload is a List<...> via associated->ListValue or a payload vector.
            if (ev->associated) {
                if (const auto *lv = std::get_if<ListValue>(&ev->associated->node)) {
                    return emit_enum_payload_list(lv->items, out);
                }
            }
            return emit_enum_payload_list(ev->payload, out);
        }
        if (var == "JEntries") {
            // Emit as a JSON object: key-value pairs.
            out.push_back('{');
            if (ev->associated) {
                if (const auto *mv = std::get_if<MapValue>(&ev->associated->node)) {
                    for (size_t i = 0; i < mv->entries.size(); ++i) {
                        if (i != 0) out.push_back(',');
                        const Value *k = mv->entries[i].first.get();
                        const auto *ksv = std::get_if<StringValue>(&k->node);
                        if (ksv == nullptr) return false;
                        json_escape(out, ksv->value);
                        out.push_back(':');
                        if (!emit_value(*mv->entries[i].second, out)) return false;
                    }
                }
            }
            out.push_back('}');
            return true;
        }
        return false;
    }
    // Fallback: any primitive — treat as if user passed a non-JsonValue type.
    if (std::holds_alternative<IntValue>(v.node)) {
        out += std::to_string(std::get<IntValue>(v.node).value);
        return true;
    }
    if (std::holds_alternative<FloatValue>(v.node)) {
        std::ostringstream oss;
        oss << std::get<FloatValue>(v.node).value;
        out += oss.str();
        return true;
    }
    if (std::holds_alternative<BoolValue>(v.node)) {
        out += (std::get<BoolValue>(v.node).value ? "true" : "false");
        return true;
    }
    if (std::holds_alternative<StringValue>(v.node)) {
        json_escape(out, std::get<StringValue>(v.node).value);
        return true;
    }
    if (std::holds_alternative<NoneValue>(v.node)) {
        out += "null";
        return true;
    }
    return false;
}

// ---- Recursive-descent JSON parser ----
struct Parser {
    std::string_view src;
    size_t pos{0};
    [[nodiscard]] bool eof() const { return pos >= src.size(); }
    [[nodiscard]] char peek() const { return eof() ? '\0' : src[pos]; }
    void skip_ws() {
        while (!eof()) {
            char c = src[pos];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++pos;
            } else {
                break;
            }
        }
    }
    bool expect(char c) {
        skip_ws();
        if (!eof() && src[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    bool expect(std::string_view s) {
        skip_ws();
        if (pos + s.size() > src.size()) return false;
        if (src.substr(pos, s.size()) != s) return false;
        pos += s.size();
        return true;
    }

    std::optional<JsonV> parse_value() {
        skip_ws();
        char c = peek();
        if (c == '"') return parse_string();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || (c >= '0' && c <= '9')) return parse_number();
        return std::nullopt;
    }

    std::optional<JsonV> parse_null() {
        if (expect("null")) {
            return make_enum("std::json::JsonValue", "JNull");
        }
        return std::nullopt;
    }
    std::optional<JsonV> parse_bool() {
        if (expect("true")) {
            return make_enum("std::json::JsonValue", "JBool",
                             std::make_unique<Value>(make_bool(true)));
        }
        if (expect("false")) {
            return make_enum("std::json::JsonValue", "JBool",
                             std::make_unique<Value>(make_bool(false)));
        }
        return std::nullopt;
    }
    std::optional<JsonV> parse_number() {
        skip_ws();
        size_t start = pos;
        if (!eof() && src[pos] == '-') ++pos;
        bool is_float = false;
        if (!eof() && src[pos] == '0') {
            ++pos;
        } else {
            if (eof() || src[pos] < '1' || src[pos] > '9') return std::nullopt;
            while (!eof() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (!eof() && src[pos] == '.') {
            is_float = true;
            ++pos;
            if (eof() || src[pos] < '0' || src[pos] > '9') return std::nullopt;
            while (!eof() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        if (!eof() && (src[pos] == 'e' || src[pos] == 'E')) {
            is_float = true;
            ++pos;
            if (!eof() && (src[pos] == '+' || src[pos] == '-')) ++pos;
            if (eof() || src[pos] < '0' || src[pos] > '9') return std::nullopt;
            while (!eof() && src[pos] >= '0' && src[pos] <= '9') ++pos;
        }
        auto lexeme = std::string(src.substr(start, pos - start));
        if (is_float) {
            try {
                double d = std::stod(lexeme);
                return make_enum("std::json::JsonValue", "JFloat",
                                 std::make_unique<Value>(make_float(d)));
            } catch (...) {
                return std::nullopt;
            }
        } else {
            try {
                int64_t i = std::stoll(lexeme);
                return make_enum("std::json::JsonValue", "JInt",
                                 std::make_unique<Value>(make_int(i)));
            } catch (...) {
                // Overflow — try double.
                try {
                    double d = std::stod(lexeme);
                    return make_enum("std::json::JsonValue", "JFloat",
                                     std::make_unique<Value>(make_float(d)));
                } catch (...) {
                    return std::nullopt;
                }
            }
        }
    }
    std::optional<std::string> parse_raw_string() {
        if (!expect('"')) return std::nullopt;
        std::string out;
        while (!eof()) {
            char c = src[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (eof()) return std::nullopt;
                char e = src[pos++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > src.size()) return std::nullopt;
                    auto hex = src.substr(pos, 4);
                    pos += 4;
                    unsigned u = 0;
                    for (char h : hex) {
                        u <<= 4;
                        if (h >= '0' && h <= '9') u |= (h - '0');
                        else if (h >= 'a' && h <= 'f') u |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') u |= (h - 'A' + 10);
                        else return std::nullopt;
                    }
                    if (u < 0x80) {
                        out.push_back(static_cast<char>(u));
                    } else if (u < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (u >> 6)));
                        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (u >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((u >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (u & 0x3F)));
                    }
                    break;
                }
                default: return std::nullopt;
                }
            } else {
                if (static_cast<unsigned char>(c) < 0x20) return std::nullopt;
                out.push_back(c);
            }
        }
        return std::nullopt;
    }
    std::optional<JsonV> parse_string() {
        auto s = parse_raw_string();
        if (!s) return std::nullopt;
        return make_enum("std::json::JsonValue", "JText",
                         std::make_unique<Value>(make_string(std::move(*s))));
    }
    std::optional<JsonV> parse_array() {
        if (!expect('[')) return std::nullopt;
        ListValue list;
        skip_ws();
        if (peek() != ']') {
            while (true) {
                auto v = parse_value();
                if (!v) return std::nullopt;
                list.items.push_back(std::make_unique<Value>(std::move(*v)));
                skip_ws();
                if (peek() == ',') {
                    ++pos;
                    continue;
                }
                break;
            }
        }
        if (!expect(']')) return std::nullopt;
        auto list_val = std::make_unique<Value>();
        list_val->node = std::move(list);
        return make_enum("std::json::JsonValue", "JList", std::move(list_val));
    }
    std::optional<JsonV> parse_object() {
        if (!expect('{')) return std::nullopt;
        MapValue map;
        skip_ws();
        if (peek() != '}') {
            while (true) {
                auto key = parse_raw_string();
                if (!key) return std::nullopt;
                skip_ws();
                if (peek() != ':') return std::nullopt;
                ++pos;
                auto v = parse_value();
                if (!v) return std::nullopt;
                // Last-write-wins dedupe.
                auto k = std::make_unique<Value>(make_string(std::move(*key)));
                auto vv = std::make_unique<Value>(std::move(*v));
                bool found = false;
                for (auto &e : map.entries) {
                    if (structurally_equal(*e.first, *k)) {
                        e.second = std::move(vv);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    map.entries.emplace_back(std::move(k), std::move(vv));
                }
                skip_ws();
                if (peek() == ',') {
                    ++pos;
                    continue;
                }
                break;
            }
        }
        if (!expect('}')) return std::nullopt;
        auto map_val = std::make_unique<Value>();
        map_val->node = std::move(map);
        return make_enum("std::json::JsonValue", "JEntries", std::move(map_val));
    }
};

} // namespace json_impl

/// json_parse_raw(s: String) -> Option<JsonValue>
EvalResult builtin_json_parse_raw(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    auto *sv = std::get_if<StringValue>(&args[0].node);
    if (!sv)
        return make_error("json_parse_raw: argument must be a String");
    json_impl::Parser p{std::string_view(sv->value), 0};
    auto result = p.parse_value();
    if (!result) {
        return EvalResult{make_optional_none(), {}};
    }
    p.skip_ws();
    if (!p.eof()) {
        return EvalResult{make_optional_none(), {}};
    }
    return EvalResult{make_optional_some(std::move(*result)), {}};
}

/// json_emit_raw(v: JsonValue) -> String
EvalResult builtin_json_emit_raw(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    std::string out;
    if (!json_impl::emit_value(args[0], out)) {
        return make_error("json_emit_raw: value cannot be serialized to JSON");
    }
    return EvalResult{make_string(std::move(out)), {}};
}

// ----------------------------------------------------------------------------
// Decimal builtins — naive Int64 mantissa / Int32 scale arithmetic.
//
// Runtime representation: DecimalValue.spelling carries a canonical encoding
// "<mantissa>e<scale>" (signed mantissa, signed scale). This keeps precision
// semantics stable without third-party decimal libraries. All helpers parse /
// emit this canonical spelling.
// ----------------------------------------------------------------------------

namespace decimal_impl {

struct Dec {
    int64_t mant{0};
    int32_t scale{0};

    // Canonical encoding used by decimal builtins for DecimalValue.spelling.
    // Format: "s<scale>:<mantissa>" — e.g. "s2:123" represents 1.23,
    // "s0:-45" represents -45, "s-3:7" represents 7000.  The "s:" prefix is
    // intentionally disjoint from user literal spellings ("1.23d") so the
    // builtin encoding and source-level literals never collide in equality
    // without normalization.

    static std::string spell(int64_t m, int32_t s) {
        std::string out = "s";
        out += std::to_string(s);
        out.push_back(':');
        out += std::to_string(m);
        return out;
    }

    static bool parse(std::string_view v, Dec &out) {
        if (!v.starts_with('s')) return false;
        v.remove_prefix(1);
        auto colon = v.find(':');
        if (colon == std::string_view::npos) return false;
        auto scale_str = v.substr(0, colon);
        auto mant_str = v.substr(colon + 1);
        try {
            size_t p1 = 0, p2 = 0;
            long long s = std::stoll(std::string(scale_str), &p1);
            long long m = std::stoll(std::string(mant_str), &p2);
            if (p1 != scale_str.size() || p2 != mant_str.size()) return false;
            if (s > std::numeric_limits<int32_t>::max() ||
                s < std::numeric_limits<int32_t>::min())
                return false;
            out.mant = static_cast<int64_t>(m);
            out.scale = static_cast<int32_t>(s);
            return true;
        } catch (...) {
            return false;
        }
    }
};

int64_t pow10_i64(int32_t exp) {
    int64_t r = 1;
    for (int32_t i = 0; i < exp; ++i) r *= 10;
    return r;
}

// Rescale `d` to `target_scale` using truncation semantics.
bool rescale(const Dec &d, int32_t target_scale, Dec &out) {
    if (target_scale == d.scale) {
        out = d;
        return true;
    }
    if (target_scale > d.scale) {
        int32_t diff = target_scale - d.scale;
        int64_t mult = pow10_i64(diff);
        int64_t new_mant;
        if (__builtin_mul_overflow(d.mant, mult, &new_mant)) return false;
        out.mant = new_mant;
        out.scale = target_scale;
        return true;
    }
    int32_t diff = d.scale - target_scale;
    int64_t div = pow10_i64(diff);
    out.mant = d.mant / div; // truncate toward zero
    out.scale = target_scale;
    return true;
}

bool align(const Dec &a, const Dec &b, Dec &aa, Dec &bb) {
    int32_t s = std::max(a.scale, b.scale);
    return rescale(a, s, aa) && rescale(b, s, bb);
}

} // namespace decimal_impl

/// decimal_raw_make(mantissa: Int, scale: Int) -> Decimal
EvalResult builtin_decimal_raw_make(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *m = std::get_if<IntValue>(&args[0].node);
    auto *s = std::get_if<IntValue>(&args[1].node);
    if (!m || !s)
        return make_error("decimal_raw_make: both arguments must be Int");
    int32_t scale = static_cast<int32_t>(s->value);
    if (static_cast<int64_t>(scale) != s->value)
        return make_error("decimal_raw_make: scale out of int32 range");
    return EvalResult{make_decimal(decimal_impl::Dec::spell(m->value, scale)), {}};
}

EvalResult builtin_decimal_raw_add(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *b = std::get_if<DecimalValue>(&args[1].node);
    if (!a || !b) return make_error("decimal_raw_add: both arguments must be Decimal");
    decimal_impl::Dec da{}, db{}, aa{}, bb{};
    if (!decimal_impl::Dec::parse(a->spelling, da) || !decimal_impl::Dec::parse(b->spelling, db))
        return make_error("decimal_raw_add: malformed Decimal spelling");
    if (!decimal_impl::align(da, db, aa, bb))
        return make_error("decimal_raw_add: rescale overflow");
    int64_t sum;
    if (__builtin_add_overflow(aa.mant, bb.mant, &sum))
        return make_error("decimal_raw_add: mantissa overflow");
    return EvalResult{make_decimal(decimal_impl::Dec::spell(sum, aa.scale)), {}};
}

EvalResult builtin_decimal_raw_sub(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *b = std::get_if<DecimalValue>(&args[1].node);
    if (!a || !b) return make_error("decimal_raw_sub: both arguments must be Decimal");
    decimal_impl::Dec da{}, db{}, aa{}, bb{};
    if (!decimal_impl::Dec::parse(a->spelling, da) || !decimal_impl::Dec::parse(b->spelling, db))
        return make_error("decimal_raw_sub: malformed Decimal spelling");
    if (!decimal_impl::align(da, db, aa, bb))
        return make_error("decimal_raw_sub: rescale overflow");
    int64_t diff;
    if (__builtin_sub_overflow(aa.mant, bb.mant, &diff))
        return make_error("decimal_raw_sub: mantissa overflow");
    return EvalResult{make_decimal(decimal_impl::Dec::spell(diff, aa.scale)), {}};
}

EvalResult builtin_decimal_raw_mul(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *b = std::get_if<DecimalValue>(&args[1].node);
    if (!a || !b) return make_error("decimal_raw_mul: both arguments must be Decimal");
    decimal_impl::Dec da{}, db{};
    if (!decimal_impl::Dec::parse(a->spelling, da) || !decimal_impl::Dec::parse(b->spelling, db))
        return make_error("decimal_raw_mul: malformed Decimal spelling");
    int64_t prod;
    if (__builtin_mul_overflow(da.mant, db.mant, &prod))
        return make_error("decimal_raw_mul: mantissa overflow");
    int64_t s64 = static_cast<int64_t>(da.scale) + static_cast<int64_t>(db.scale);
    if (s64 > std::numeric_limits<int32_t>::max() ||
        s64 < std::numeric_limits<int32_t>::min())
        return make_error("decimal_raw_mul: scale overflow");
    return EvalResult{make_decimal(decimal_impl::Dec::spell(prod, static_cast<int32_t>(s64))), {}};
}

/// decimal_raw_scale(a: Decimal) -> Int
EvalResult builtin_decimal_raw_scale(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    if (!a) return make_error("decimal_raw_scale: argument must be Decimal");
    decimal_impl::Dec d{};
    if (!decimal_impl::Dec::parse(a->spelling, d))
        return make_error("decimal_raw_scale: malformed Decimal spelling");
    return EvalResult{make_int(static_cast<int64_t>(d.scale)), {}};
}

/// decimal_raw_with_scale(a: Decimal, new_scale: Int) -> Decimal
/// Safe cast: upsizing multiplies mantissa; downsizing truncates.
EvalResult builtin_decimal_raw_with_scale(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *s = std::get_if<IntValue>(&args[1].node);
    if (!a || !s) return make_error("decimal_raw_with_scale: bad argument types");
    decimal_impl::Dec d{}, out{};
    if (!decimal_impl::Dec::parse(a->spelling, d))
        return make_error("decimal_raw_with_scale: malformed Decimal spelling");
    int64_t ns64 = s->value;
    if (ns64 > std::numeric_limits<int32_t>::max() ||
        ns64 < std::numeric_limits<int32_t>::min())
        return make_error("decimal_raw_with_scale: scale out of int32 range");
    if (!decimal_impl::rescale(d, static_cast<int32_t>(ns64), out))
        return make_error("decimal_raw_with_scale: rescale overflow");
    return EvalResult{make_decimal(decimal_impl::Dec::spell(out.mant, out.scale)), {}};
}

/// decimal_raw_quantize(a: Decimal, new_scale: Int, mode_index: Int) -> Decimal
/// mode_index: maps RoundingMode ordinal: Ceiling=0, Floor=1, Truncate=2,
/// HalfUp=3, HalfDown=4, HalfEven=5.
EvalResult builtin_decimal_raw_quantize(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3)
        return arg_count_error(3, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *s = std::get_if<IntValue>(&args[1].node);
    auto *m = std::get_if<IntValue>(&args[2].node);
    if (!a || !s || !m)
        return make_error("decimal_raw_quantize: bad argument types");
    decimal_impl::Dec d{};
    if (!decimal_impl::Dec::parse(a->spelling, d))
        return make_error("decimal_raw_quantize: malformed Decimal spelling");
    int64_t ns64 = s->value;
    if (ns64 > std::numeric_limits<int32_t>::max() ||
        ns64 < std::numeric_limits<int32_t>::min())
        return make_error("decimal_raw_quantize: scale out of int32 range");
    int32_t target = static_cast<int32_t>(ns64);
    int64_t mode = m->value;
    if (target >= d.scale) {
        // Upsizing never needs rounding.
        decimal_impl::Dec out{};
        if (!decimal_impl::rescale(d, target, out))
            return make_error("decimal_raw_quantize: rescale overflow");
        return EvalResult{make_decimal(decimal_impl::Dec::spell(out.mant, out.scale)), {}};
    }
    int32_t diff = d.scale - target;
    int64_t div = decimal_impl::pow10_i64(diff);
    int64_t trunc = d.mant / div;
    int64_t rem = d.mant - trunc * div;
    if (rem != 0) {
        bool neg = d.mant < 0;
        auto rem_abs = static_cast<uint64_t>(rem < 0 ? -rem : rem);
        auto div_half = static_cast<uint64_t>(div / 2);
        bool away = false;
        switch (mode) {
        case 0: // Ceiling
            away = !neg;
            break;
        case 1: // Floor
            away = neg;
            break;
        case 2: // Truncate
            away = false;
            break;
        case 3: // HalfUp (tie away)
            if (rem_abs > div_half)
                away = true;
            else if (rem_abs == div_half)
                away = true;
            break;
        case 4: // HalfDown (tie to zero)
            if (rem_abs > div_half)
                away = true;
            break;
        case 5: // HalfEven (tie to even)
            if (rem_abs > div_half) {
                away = true;
            } else if (rem_abs == div_half) {
                away = (trunc & 1) != 0;
            }
            break;
        default:
            return make_error("decimal_raw_quantize: unknown rounding mode");
        }
        if (away) {
            int64_t delta = neg ? -1 : 1;
            trunc += delta;
        }
    }
    return EvalResult{make_decimal(decimal_impl::Dec::spell(trunc, target)), {}};
}

/// decimal_raw_eq(a, b) -> Bool — compares numeric value, not spelling.
EvalResult builtin_decimal_raw_eq(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *b = std::get_if<DecimalValue>(&args[1].node);
    if (!a || !b) return make_error("decimal_raw_eq: both arguments must be Decimal");
    decimal_impl::Dec da{}, db{}, aa{}, bb{};
    if (!decimal_impl::Dec::parse(a->spelling, da) || !decimal_impl::Dec::parse(b->spelling, db))
        return make_error("decimal_raw_eq: malformed Decimal spelling");
    if (!decimal_impl::align(da, db, aa, bb))
        return make_error("decimal_raw_eq: rescale overflow");
    return EvalResult{make_bool(aa.mant == bb.mant), {}};
}

/// decimal_raw_cmp(a, b) -> Ordering  (std::cmp::Ordering via enum)
EvalResult builtin_decimal_raw_cmp(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2)
        return arg_count_error(2, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    auto *b = std::get_if<DecimalValue>(&args[1].node);
    if (!a || !b) return make_error("decimal_raw_cmp: both arguments must be Decimal");
    decimal_impl::Dec da{}, db{}, aa{}, bb{};
    if (!decimal_impl::Dec::parse(a->spelling, da) || !decimal_impl::Dec::parse(b->spelling, db))
        return make_error("decimal_raw_cmp: malformed Decimal spelling");
    if (!decimal_impl::align(da, db, aa, bb))
        return make_error("decimal_raw_cmp: rescale overflow");
    const char *variant = "Equal";
    if (aa.mant < bb.mant) variant = "Less";
    else if (aa.mant > bb.mant) variant = "Greater";
    return EvalResult{make_enum("std::cmp::Ordering", variant), {}};
}

/// decimal_raw_to_string(a) -> String
EvalResult builtin_decimal_raw_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1)
        return arg_count_error(1, args.size());
    auto *a = std::get_if<DecimalValue>(&args[0].node);
    if (!a) return make_error("decimal_raw_to_string: argument must be Decimal");
    decimal_impl::Dec d{};
    if (!decimal_impl::Dec::parse(a->spelling, d))
        return make_error("decimal_raw_to_string: malformed Decimal spelling");
    // Render a short human readable form: `mant * 10^(-scale)`.
    std::string mant_str = std::to_string(d.mant);
    bool neg = !mant_str.empty() && mant_str.front() == '-';
    std::string digits = neg ? mant_str.substr(1) : mant_str;
    std::string out;
    if (neg) out.push_back('-');
    int32_t s = d.scale;
    if (s <= 0) {
        // Integer with (-s) trailing zeros appended.
        out += digits;
        out.append(static_cast<size_t>(-s), '0');
    } else if (static_cast<size_t>(s) >= digits.size()) {
        size_t zeros = static_cast<size_t>(s) - digits.size();
        out += "0.";
        out.append(zeros, '0');
        out += digits;
    } else {
        size_t dot = digits.size() - static_cast<size_t>(s);
        out += digits.substr(0, dot);
        out.push_back('.');
        out += digits.substr(dot);
    }
    return EvalResult{make_string(std::move(out)), {}};
}

} // anonymous namespace

// ============================================================================
// BuiltinTable
// ============================================================================

const BuiltinTable &BuiltinTable::instance() {
    static const BuiltinTable table = []() {
        BuiltinTable t;
        t.populate();
        return t;
    }();
    return table;
}

const BuiltinFn *BuiltinTable::find(std::string_view name) const {
    auto it = table_.find(std::string(name));
    if (it == table_.end())
        return nullptr;
    return &it->second;
}

std::vector<std::string> BuiltinTable::names() const {
    std::vector<std::string> result;
    result.reserve(table_.size());
    for (const auto &[name, _] : table_) {
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void BuiltinTable::insert(std::string name, BuiltinFn fn) {
    table_.emplace(std::move(name), std::move(fn));
}

void BuiltinTable::populate() {
    // —— List ——
    insert("list_raw_length", builtin_list_raw_length);
    insert("list_raw_get", builtin_list_raw_get);
    insert("list_raw_set", builtin_list_raw_set);
    insert("list_raw_alloc", builtin_list_raw_alloc);
    insert("list_from_array", builtin_list_from_array);
    insert("list_sort", builtin_list_sort);
    insert("list_dedup", builtin_list_dedup);

    // —— Set ——
    insert("set_raw_size", builtin_set_raw_size);
    insert("set_raw_contains", builtin_set_raw_contains);
    insert("set_from_array", builtin_set_from_array);
    insert("set_union", builtin_set_union);
    insert("set_intersection", builtin_set_intersection);
    insert("set_difference", builtin_set_difference);
    insert("set_is_subset", builtin_set_is_subset);
    insert("set_raw_empty", builtin_set_raw_empty);
    insert("set_raw_singleton", builtin_set_raw_singleton);

    // —— Map ——
    insert("map_raw_size", builtin_map_raw_size);
    insert("map_raw_contains_key", builtin_map_raw_contains_key);
    insert("map_raw_get", builtin_map_raw_get);
    insert("map_from_entries", builtin_map_from_entries);
    insert("map_raw_empty", builtin_map_raw_empty);
    insert("map_raw_singleton", builtin_map_raw_singleton);
    insert("map_raw_keys", builtin_map_raw_keys);
    insert("map_raw_values", builtin_map_raw_values);
    insert("map_raw_insert", builtin_map_raw_insert);

    // —— String ——
    insert("string_raw_length", builtin_string_raw_length);
    insert("string_raw_concat", builtin_string_raw_concat);
    insert("string_raw_substring", builtin_string_raw_substring);
    insert("string_is_empty", builtin_string_is_empty);
    insert("string_length", builtin_string_raw_length);
    insert("string_contains", builtin_string_contains);
    insert("string_starts_with", builtin_string_starts_with);
    insert("string_ends_with", builtin_string_ends_with);
    insert("string_trim", builtin_string_trim);
    insert("string_trim_start", builtin_string_trim_start);
    insert("string_trim_end", builtin_string_trim_end);
    insert("string_replace", builtin_string_replace);
    insert("string_split", builtin_string_split);
    insert("string_parse_int", builtin_string_parse_int);
    insert("string_split_whitespace", builtin_string_split_whitespace);
    insert("string_parse_float", builtin_string_parse_float);
    insert("string_concat", builtin_string_raw_concat);
    insert("string_raw_compare", builtin_string_raw_compare);
    insert("cmp_raw_compare", builtin_cmp_raw_compare);

    // —— Numeric ——
    insert("int_to_string", builtin_int_to_string);
    insert("bool_to_string", builtin_bool_to_string);
    insert("float_to_string", builtin_float_to_string);
    insert("float_trunc_to_int", builtin_float_trunc_to_int);
    insert("int_to_float", builtin_int_to_float);

    // —— Time ——
    insert("wall_clock_now", builtin_wall_clock_now);
    insert("timestamp_add", builtin_timestamp_add);
    insert("time_now", builtin_wall_clock_now);
    insert("time_add", builtin_timestamp_add);
    insert("time_sub", builtin_timestamp_sub);
    insert("time_duration_between", builtin_duration_between);

    // —— UUID ——
    insert("uuid_new", builtin_uuid_new);
    insert("uuid_from_string", builtin_uuid_from_string);
    insert("uuid_new_v4", builtin_uuid_new);
    insert("uuid_parse", builtin_uuid_from_string);
    insert("uuid_to_string", builtin_uuid_to_string);

    // —— JSON (real implementations: recursive-descent parser + emitter) ——
    insert("json_parse_raw", builtin_json_parse_raw);
    insert("json_emit_raw", builtin_json_emit_raw);

    // —— Decimal ——
    insert("decimal_raw_make", builtin_decimal_raw_make);
    insert("decimal_raw_add", builtin_decimal_raw_add);
    insert("decimal_raw_sub", builtin_decimal_raw_sub);
    insert("decimal_raw_mul", builtin_decimal_raw_mul);
    insert("decimal_raw_scale", builtin_decimal_raw_scale);
    insert("decimal_raw_with_scale", builtin_decimal_raw_with_scale);
    insert("decimal_raw_quantize", builtin_decimal_raw_quantize);
    insert("decimal_raw_eq", builtin_decimal_raw_eq);
    insert("decimal_raw_cmp", builtin_decimal_raw_cmp);
    insert("decimal_raw_to_string", builtin_decimal_raw_to_string);
}

// ============================================================================
// make_builtin_call_eval
// ============================================================================

CallEvalFn make_builtin_call_eval(const BuiltinTable &table) {
    return [&table](const ir::CallExpr &call, const EvalContext &ctx) -> EvalResult {
        // Look up the builtin by callee name
        const BuiltinFn *fn = table.find(call.callee);
        if (!fn) {
            return make_error("unknown builtin: " + call.callee);
        }

        // Evaluate all arguments
        std::vector<Value> args;
        args.reserve(call.arguments.size());
        DiagnosticBag diags;
        for (const auto &arg_ref : call.arguments) {
            if (!arg_ref) {
                return make_error("builtin call: null argument expression");
            }
            EvalResult arg_result = eval_expr(*arg_ref, ctx);
            if (arg_result.has_errors()) {
                // Merge diagnostics and bail on first error
                diags.append(std::move(arg_result.diagnostics));
                return EvalResult{make_none(), std::move(diags)};
            }
            diags.append(std::move(arg_result.diagnostics));
            args.push_back(std::move(arg_result.value));
        }

        // Dispatch to the builtin
        EvalResult result = (*fn)(args, ctx);
        // Merge accumulated diagnostics from argument evaluation
        result.diagnostics.append(std::move(diags));
        return result;
    };
}

} // namespace ahfl::evaluator
