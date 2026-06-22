#include "runtime/evaluator/builtins.hpp"
#include "runtime/evaluator/evaluator.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
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
    return make_error("builtin expected " + std::to_string(expected) +
                      " arguments, got " + std::to_string(got));
}

// ----------------------------------------------------------------------------
// Container builtins (List)
// ----------------------------------------------------------------------------

/// list_raw_length(xs: List<T>) -> Int
EvalResult builtin_list_raw_length(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    if (auto *lv = std::get_if<ListValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(lv->items.size())), {}};
    }
    return make_error("list_raw_length: argument must be a List");
}

/// list_raw_get(xs: List<T>, i: Int) -> T
EvalResult builtin_list_raw_get(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *lv = std::get_if<ListValue>(&args[0].node);
    auto *iv = std::get_if<IntValue>(&args[1].node);
    if (!lv) return make_error("list_raw_get: first argument must be a List");
    if (!iv) return make_error("list_raw_get: second argument must be an Int");
    int64_t idx = iv->value;
    if (idx < 0 || static_cast<size_t>(idx) >= lv->items.size()) {
        return make_error("list_raw_get: index out of bounds");
    }
    return EvalResult{clone_value(*lv->items[static_cast<size_t>(idx)]), {}};
}

/// list_raw_set(xs: List<T>, i: Int, x: T) -> List<T>
EvalResult builtin_list_raw_set(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 3) return arg_count_error(3, args.size());
    auto *lv = std::get_if<ListValue>(&args[0].node);
    auto *iv = std::get_if<IntValue>(&args[1].node);
    if (!lv) return make_error("list_raw_set: first argument must be a List");
    if (!iv) return make_error("list_raw_set: second argument must be an Int");
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
// Container builtins (Set)
// ----------------------------------------------------------------------------

/// set_raw_size(s: Set<T>) -> Int
EvalResult builtin_set_raw_size(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    if (auto *sv = std::get_if<SetValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(sv->items.size())), {}};
    }
    return make_error("set_raw_size: argument must be a Set");
}

/// set_raw_contains(s: Set<T>, x: T) -> Bool
EvalResult builtin_set_raw_contains(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *sv = std::get_if<SetValue>(&args[0].node);
    if (!sv) return make_error("set_raw_contains: first argument must be a Set");
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

// ----------------------------------------------------------------------------
// Container builtins (Map)
// ----------------------------------------------------------------------------

/// map_raw_size(m: Map<K, V>) -> Int
EvalResult builtin_map_raw_size(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    if (auto *mv = std::get_if<MapValue>(&args[0].node)) {
        return EvalResult{make_int(static_cast<int64_t>(mv->entries.size())), {}};
    }
    return make_error("map_raw_size: argument must be a Map");
}

/// map_raw_contains_key(m: Map<K, V>, k: K) -> Bool
EvalResult builtin_map_raw_contains_key(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *mv = std::get_if<MapValue>(&args[0].node);
    if (!mv) return make_error("map_raw_contains_key: first argument must be a Map");
    for (const auto &entry : mv->entries) {
        if (structurally_equal(*entry.first, args[1])) {
            return EvalResult{make_bool(true), {}};
        }
    }
    return EvalResult{make_bool(false), {}};
}

/// map_raw_get(m: Map<K, V>, k: K) -> V
EvalResult builtin_map_raw_get(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *mv = std::get_if<MapValue>(&args[0].node);
    if (!mv) return make_error("map_raw_get: first argument must be a Map");
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

// ----------------------------------------------------------------------------
// String builtins
// ----------------------------------------------------------------------------

/// string_raw_length(s: String) -> Int
EvalResult builtin_string_raw_length(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    if (auto *sv = std::get_if<StringValue>(&args[0].node)) {
        // P5 placeholder: returns byte count (codepoint handling deferred to
        // proper stdlib string implementation)
        return EvalResult{make_int(static_cast<int64_t>(sv->value.size())), {}};
    }
    return make_error("string_raw_length: argument must be a String");
}

/// string_raw_concat(a: String, b: String) -> String
EvalResult builtin_string_raw_concat(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *a = std::get_if<StringValue>(&args[0].node);
    auto *b = std::get_if<StringValue>(&args[1].node);
    if (!a || !b) return make_error("string_raw_concat: both arguments must be Strings");
    return EvalResult{make_string(a->value + b->value), {}};
}

// ----------------------------------------------------------------------------
// Numeric builtins
// ----------------------------------------------------------------------------

/// int_to_string(n: Int) -> String
EvalResult builtin_int_to_string(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    if (auto *iv = std::get_if<IntValue>(&args[0].node)) {
        return EvalResult{make_string(std::to_string(iv->value)), {}};
    }
    return make_error("int_to_string: argument must be an Int");
}

// ----------------------------------------------------------------------------
// Time builtins
// ----------------------------------------------------------------------------

/// wall_clock_now() -> Timestamp  (effect: Nondet)
EvalResult builtin_wall_clock_now(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (!args.empty()) return arg_count_error(0, args.size());
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count();
    return EvalResult{make_timestamp(ms), {}};
}

/// timestamp_add(t: Timestamp, d: Duration) -> Timestamp
EvalResult builtin_timestamp_add(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 2) return arg_count_error(2, args.size());
    auto *tv = std::get_if<TimestampValue>(&args[0].node);
    auto *dv = std::get_if<DurationValue>(&args[1].node);
    if (!tv) return make_error("timestamp_add: first argument must be a Timestamp");
    if (!dv) return make_error("timestamp_add: second argument must be a Duration");
    // P5 placeholder: parse duration spelling for "s", "ms" suffixes
    // For now, interpret numeric prefix as milliseconds (best-effort)
    int64_t ms = 0;
    try {
        ms = std::stoll(dv->spelling);
    } catch (...) {
        // Try stripping common suffixes
        std::string s = dv->spelling;
        if (s.size() >= 2 && s.substr(s.size() - 2) == "ms") {
            s = s.substr(0, s.size() - 2);
        } else if (!s.empty() && s.back() == 's') {
            s = s.substr(0, s.size() - 1);
        }
        try {
            ms = std::stoll(s);
            // If we stripped "s" (not "ms"), multiply by 1000
            if (dv->spelling.size() >= 1 && dv->spelling.back() == 's' &&
                (dv->spelling.size() < 2 || dv->spelling[dv->spelling.size() - 2] != 'm')) {
                ms *= 1000;
            }
        } catch (...) {
            return make_error("timestamp_add: cannot parse duration");
        }
    }
    return EvalResult{make_timestamp(tv->unix_ms + ms), {}};
}

// ----------------------------------------------------------------------------
// UUID builtins
// ----------------------------------------------------------------------------

/// uuid_new() -> UUID  (effect: Nondet)
EvalResult builtin_uuid_new(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (!args.empty()) return arg_count_error(0, args.size());
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
    if (args.size() != 1) return arg_count_error(1, args.size());
    auto *sv = std::get_if<StringValue>(&args[0].node);
    if (!sv) return make_error("uuid_from_string: argument must be a String");
    if (auto v = make_uuid(sv->value)) {
        return EvalResult{make_optional_some(std::move(*v)), {}};
    }
    return EvalResult{make_optional_none(), {}};
}

// ----------------------------------------------------------------------------
// JSON builtins (placeholder stubs — full implementation deferred)
// ----------------------------------------------------------------------------

/// json_parse_raw(s: String) -> Option<JsonValue>
EvalResult builtin_json_parse_raw(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    // P5 placeholder: return None for now (full JSON parser deferred)
    return EvalResult{make_optional_none(), {}};
}

/// json_emit_raw(v: JsonValue) -> String
EvalResult builtin_json_emit_raw(const std::vector<Value> &args, const EvalContext & /*ctx*/) {
    if (args.size() != 1) return arg_count_error(1, args.size());
    // P5 placeholder: return empty string (full JSON emitter deferred)
    return EvalResult{make_string(""), {}};
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
    if (it == table_.end()) return nullptr;
    return &it->second;
}

void BuiltinTable::insert(std::string name, BuiltinFn fn) {
    table_.emplace(std::move(name), std::move(fn));
}

void BuiltinTable::populate() {
    // —— List ——
    insert("list_raw_length", builtin_list_raw_length);
    insert("list_raw_get", builtin_list_raw_get);
    insert("list_raw_set", builtin_list_raw_set);
    insert("list_from_array", builtin_list_from_array);

    // —— Set ——
    insert("set_raw_size", builtin_set_raw_size);
    insert("set_raw_contains", builtin_set_raw_contains);
    insert("set_from_array", builtin_set_from_array);

    // —— Map ——
    insert("map_raw_size", builtin_map_raw_size);
    insert("map_raw_contains_key", builtin_map_raw_contains_key);
    insert("map_raw_get", builtin_map_raw_get);
    insert("map_from_entries", builtin_map_from_entries);

    // —— String ——
    insert("string_raw_length", builtin_string_raw_length);
    insert("string_raw_concat", builtin_string_raw_concat);

    // —— Numeric ——
    insert("int_to_string", builtin_int_to_string);

    // —— Time ——
    insert("wall_clock_now", builtin_wall_clock_now);
    insert("timestamp_add", builtin_timestamp_add);

    // —— UUID ——
    insert("uuid_new", builtin_uuid_new);
    insert("uuid_from_string", builtin_uuid_from_string);

    // —— JSON (placeholders) ——
    insert("json_parse_raw", builtin_json_parse_raw);
    insert("json_emit_raw", builtin_json_emit_raw);
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
