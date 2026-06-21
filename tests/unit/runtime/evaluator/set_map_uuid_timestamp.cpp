// RFC P7 runtime additions — basic evaluation tests for Set / Map / UUID /
// Timestamp values. Exercises construction (make_*), canonicalization,
// equality, JSON round-trip, member access (.length/.size), index access
// (map[key] / set[member]), and evaluator integration for SetLiteralExpr /
// MapLiteralExpr.

#include "runtime/evaluator/eval_context.hpp"
#include "runtime/evaluator/evaluator.hpp"
#include "runtime/evaluator/value.hpp"
#include "runtime/evaluator/value_json.hpp"
#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/base/support/ownership.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::evaluator;
using namespace ahfl::ir;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

ExprArena &test_expr_arena() {
    static ExprArena arena;
    return arena;
}

ExprRef make_expr_ptr(ExprNode node) {
    return test_expr_arena().make(std::move(node));
}

Expr make_expr(ExprNode node) {
    return Expr{std::move(node), {}, {}};
}

// Helper: build a Set from moved Values. Value is move-only (SetValue/MapValue
// hold unique_ptr), so initializer_list construction would copy — use this.
template <typename... Ts>
Value make_set_of(Ts &&...values) {
    std::vector<Value> items;
    items.reserve(sizeof...(Ts));
    (items.push_back(std::forward<Ts>(values)), ...);
    return make_set(std::move(items));
}

// ============================================================================
// Set value tests
// ============================================================================

void test_set_basic() {
    auto s = make_set_of(make_int(3), make_int(1), make_int(2));
    auto *sv = std::get_if<SetValue>(&s.node);
    check(sv != nullptr, "set.is_set");
    check(sv != nullptr && sv->items.size() == 3, "set.size_3");
}

void test_set_dedup_and_order() {
    // Duplicates collapse; remaining items are sorted by structural comparison.
    auto s = make_set_of(make_int(2), make_int(1), make_int(2), make_int(3), make_int(1));
    auto *sv = std::get_if<SetValue>(&s.node);
    check(sv != nullptr && sv->items.size() == 3, "set.dedup.size");
    if (sv && sv->items.size() == 3) {
        bool ordered = true;
        for (size_t i = 0; i < sv->items.size(); ++i) {
            auto *iv = std::get_if<IntValue>(&sv->items[i]->node);
            ordered = ordered && iv != nullptr && iv->value == static_cast<int64_t>(i + 1);
        }
        check(ordered, "set.dedup.sorted_ascending");
    }
}

void test_set_order_independent_equality() {
    auto a = make_set_of(make_int(1), make_int(2), make_int(3));
    auto b = make_set_of(make_int(3), make_int(1), make_int(2));
    // Different insertion order must canonicalize to the same value.
    check(value_to_json(a) == value_to_json(b), "set.equality.order_independent");
}

void test_set_unequal() {
    auto a = make_set_of(make_int(1), make_int(2));
    auto b = make_set_of(make_int(1), make_int(2), make_int(3));
    check(value_to_json(a) != value_to_json(b), "set.unequal.different_size");
}

void test_set_string_keys() {
    auto s = make_set_of(make_string("b"), make_string("a"), make_string("a"));
    auto *sv = std::get_if<SetValue>(&s.node);
    check(sv != nullptr && sv->items.size() == 2, "set.string.dedup");
}

// ============================================================================
// Map value tests
// ============================================================================

void test_map_basic() {
    std::vector<std::pair<Value, Value>> entries;
    entries.emplace_back(make_string("a"), make_int(1));
    entries.emplace_back(make_string("b"), make_int(2));
    auto m = make_map(std::move(entries));
    auto *mv = std::get_if<MapValue>(&m.node);
    check(mv != nullptr, "map.is_map");
    check(mv != nullptr && mv->entries.size() == 2, "map.size_2");
}

void test_map_last_write_wins() {
    std::vector<std::pair<Value, Value>> entries;
    entries.emplace_back(make_string("k"), make_int(1));
    entries.emplace_back(make_string("k"), make_int(99));
    auto m = make_map(std::move(entries));
    auto *mv = std::get_if<MapValue>(&m.node);
    check(mv != nullptr && mv->entries.size() == 1, "map.dup.single_entry");
    if (mv && mv->entries.size() == 1 && mv->entries[0].second) {
        auto *iv = std::get_if<IntValue>(&mv->entries[0].second->node);
        check(iv != nullptr && iv->value == 99, "map.dup.last_value");
    }
}

void test_map_order_independent_equality() {
    std::vector<std::pair<Value, Value>> a_entries;
    a_entries.emplace_back(make_string("x"), make_int(1));
    a_entries.emplace_back(make_string("y"), make_int(2));
    auto a = make_map(std::move(a_entries));

    std::vector<std::pair<Value, Value>> b_entries;
    b_entries.emplace_back(make_string("y"), make_int(2));
    b_entries.emplace_back(make_string("x"), make_int(1));
    auto b = make_map(std::move(b_entries));

    check(value_to_json(a) == value_to_json(b), "map.equality.order_independent");
}

// ============================================================================
// UUID value tests
// ============================================================================

void test_uuid_canonical() {
    auto u = make_uuid("550e8400-e29b-41d4-a716-446655440000");
    check(u.has_value(), "uuid.canonical.parsed");
    if (u) {
        auto *uv = std::get_if<UuidValue>(&u->node);
        check(uv != nullptr && uv->hex == "550e8400e29b41d4a716446655440000", "uuid.canonical.hex");
    }
}

void test_uuid_uppercase_normalized() {
    auto u = make_uuid("550E8400-E29B-41D4-A716-446655440000");
    check(u.has_value(), "uuid.upper.parsed");
    if (u) {
        auto *uv = std::get_if<UuidValue>(&u->node);
        check(uv != nullptr && uv->hex == "550e8400e29b41d4a716446655440000", "uuid.upper.lowercased");
    }
}

void test_uuid_bare_hex() {
    auto u = make_uuid("550e8400e29b41d4a716446655440000");
    check(u.has_value(), "uuid.bare.parsed");
}

void test_uuid_invalid() {
    check(!make_uuid("not-a-uuid").has_value(), "uuid.invalid.garbage");
    check(!make_uuid("550e8400e29b41d4a71644665544000").has_value(), "uuid.invalid.too_short");
    check(!make_uuid("550e8400e29b41d4a7164466554400000").has_value(), "uuid.invalid.too_long");
}

void test_uuid_equality() {
    auto a = make_uuid("550e8400-e29b-41d4-a716-446655440000");
    auto b = make_uuid("550E8400E29B41D4A716446655440000");
    check(a.has_value() && b.has_value() && value_to_json(*a) == value_to_json(*b),
          "uuid.equality.normalize");
}

// ============================================================================
// Timestamp value tests
// ============================================================================

void test_timestamp_basic() {
    auto t = make_timestamp(1700000000123LL);
    auto *tv = std::get_if<TimestampValue>(&t.node);
    check(tv != nullptr && tv->unix_ms == 1700000000123LL, "timestamp.value");
}

void test_timestamp_equality() {
    auto a = make_timestamp(100);
    auto b = make_timestamp(100);
    auto c = make_timestamp(200);
    check(value_to_json(a) == value_to_json(b), "timestamp.equal.equal");
    check(value_to_json(a) != value_to_json(c), "timestamp.unequal.different");
}

// ============================================================================
// clone_value for new kinds
// ============================================================================

void test_clone_set() {
    auto s = make_set_of(make_int(1), make_int(2));
    auto cloned = clone_value(s);
    check(value_to_json(s) == value_to_json(cloned), "clone.set.equal");
    // Mutating clone (by re-making) does not affect the source value.
    auto rebuilt = make_set_of(make_int(1), make_int(2));
    check(value_to_json(s) == value_to_json(rebuilt), "clone.set.source_intact");
}

void test_clone_map() {
    std::vector<std::pair<Value, Value>> entries;
    entries.emplace_back(make_string("k"), make_int(42));
    auto m = make_map(std::move(entries));
    auto cloned = clone_value(m);
    check(value_to_json(m) == value_to_json(cloned), "clone.map.equal");
}

void test_clone_uuid_timestamp() {
    auto u = make_uuid("550e8400e29b41d4a716446655440000");
    check(u.has_value(), "clone.uuid.source_valid");
    if (u) {
        check(value_to_json(*u) == value_to_json(clone_value(*u)), "clone.uuid.equal");
    }
    auto t = make_timestamp(7);
    check(value_to_json(t) == value_to_json(clone_value(t)), "clone.timestamp.equal");
}

// ============================================================================
// value_kind for new kinds
// ============================================================================

void test_value_kind_new() {
    check(value_kind(make_set_of(make_int(1))) == ValueKind::Set, "kind.set");
    std::vector<std::pair<Value, Value>> e;
    e.emplace_back(make_string("k"), make_int(1));
    check(value_kind(make_map(std::move(e))) == ValueKind::Map, "kind.map");
    auto u = make_uuid("550e8400e29b41d4a716446655440000");
    check(u.has_value() && value_kind(*u) == ValueKind::Uuid, "kind.uuid");
    check(value_kind(make_timestamp(0)) == ValueKind::Timestamp, "kind.timestamp");
}

// ============================================================================
// print_value for new kinds
// ============================================================================

void test_print_new() {
    std::ostringstream oss;
    print_value(make_set_of(make_int(1), make_int(2)), oss);
    check(oss.str().find("set{") == 0, "print.set.prefix");

    oss.str("");
    auto u = make_uuid("550e8400e29b41d4a716446655440000");
    if (u) {
        print_value(*u, oss);
        check(oss.str() == "uuid(550e8400e29b41d4a716446655440000)", "print.uuid");
    }

    oss.str("");
    print_value(make_timestamp(123), oss);
    check(oss.str() == "timestamp(123)", "print.timestamp");
}

// ============================================================================
// JSON round-trip for new kinds
// ============================================================================

void test_json_uuid_roundtrip() {
    auto u = make_uuid("550e8400e29b41d4a716446655440000");
    check(u.has_value(), "json.uuid.source");
    if (u) {
        auto json = value_to_json(*u);
        auto parsed = value_from_json(json);
        check(parsed.has_value(), "json.uuid.parsed");
        if (parsed) {
            check(value_to_json(*parsed) == json, "json.uuid.roundtrip");
        }
    }
}

void test_json_timestamp_roundtrip() {
    auto t = make_timestamp(1700000000123LL);
    auto json = value_to_json(t);
    auto parsed = value_from_json(json);
    check(parsed.has_value(), "json.timestamp.parsed");
    if (parsed) {
        check(value_to_json(*parsed) == json, "json.timestamp.roundtrip");
    }
}

// ============================================================================
// Evaluator: SetLiteralExpr / MapLiteralExpr
// ============================================================================

void test_eval_set_literal() {
    EvalContext ctx;
    std::vector<ExprRef> items;
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"3"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"})); // duplicate
    auto expr = make_expr(SetLiteralExpr{std::move(items)});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "eval.set.no_error");
    auto *sv = std::get_if<SetValue>(&result.value.node);
    check(sv != nullptr && sv->items.size() == 3, "eval.set.canonical_size");
    if (sv && sv->items.size() == 3) {
        auto *first = std::get_if<IntValue>(&sv->items[0]->node);
        check(first != nullptr && first->value == 1, "eval.set.sorted_first");
    }
}

void test_eval_map_literal() {
    EvalContext ctx;
    std::vector<MapEntryExpr> entries;
    entries.push_back(MapEntryExpr{make_expr_ptr(StringLiteralExpr{"b"}), make_expr_ptr(IntegerLiteralExpr{"2"})});
    entries.push_back(MapEntryExpr{make_expr_ptr(StringLiteralExpr{"a"}), make_expr_ptr(IntegerLiteralExpr{"1"})});
    auto expr = make_expr(MapLiteralExpr{std::move(entries)});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "eval.map.no_error");
    auto *mv = std::get_if<MapValue>(&result.value.node);
    check(mv != nullptr && mv->entries.size() == 2, "eval.map.size");
}

// ============================================================================
// Evaluator: equality / contains / length on new kinds
// ============================================================================

void test_eval_set_equality() {
    EvalContext ctx;
    std::vector<ExprRef> a;
    a.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    a.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    std::vector<ExprRef> b;
    b.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    b.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    auto eq = make_expr(BinaryExpr{ExprBinaryOp::Equal, make_expr_ptr(SetLiteralExpr{std::move(a)}),
                                   make_expr_ptr(SetLiteralExpr{std::move(b)})});
    auto result = eval_expr(eq, ctx);
    check(!result.has_errors(), "eval.set_eq.no_error");
    auto *bv = std::get_if<BoolValue>(&result.value.node);
    check(bv != nullptr && bv->value == true, "eval.set_eq.true");
}

void test_eval_set_length() {
    EvalContext ctx;
    std::vector<ExprRef> items;
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"3"}));
    auto base = make_expr_ptr(SetLiteralExpr{std::move(items)});
    auto expr = make_expr(MemberAccessExpr{std::move(base), "length"});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "eval.set_length.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 3, "eval.set_length.value");
}

void test_eval_set_contains_via_index() {
    EvalContext ctx;
    std::vector<ExprRef> items;
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"3"}));
    auto base = make_expr_ptr(SetLiteralExpr{std::move(items)});

    // set[2] -> true
    auto present = make_expr(
        IndexAccessExpr{base, make_expr_ptr(IntegerLiteralExpr{"2"})});
    auto present_result = eval_expr(present, ctx);
    check(!present_result.has_errors(), "eval.set_contains_present.no_error");
    auto *bv = std::get_if<BoolValue>(&present_result.value.node);
    check(bv != nullptr && bv->value == true, "eval.set_contains_present.true");

    // set[5] -> false (rebuild base because IndexAccessExpr moved it)
    std::vector<ExprRef> items2;
    items2.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items2.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    items2.push_back(make_expr_ptr(IntegerLiteralExpr{"3"}));
    auto base2 = make_expr_ptr(SetLiteralExpr{std::move(items2)});
    auto absent = make_expr(
        IndexAccessExpr{base2, make_expr_ptr(IntegerLiteralExpr{"5"})});
    auto absent_result = eval_expr(absent, ctx);
    check(!absent_result.has_errors(), "eval.set_contains_absent.no_error");
    auto *bv2 = std::get_if<BoolValue>(&absent_result.value.node);
    check(bv2 != nullptr && bv2->value == false, "eval.set_contains_absent.false");
}

void test_eval_map_index() {
    EvalContext ctx;
    std::vector<MapEntryExpr> entries;
    entries.push_back(
        MapEntryExpr{make_expr_ptr(StringLiteralExpr{"a"}), make_expr_ptr(IntegerLiteralExpr{"10"})});
    entries.push_back(
        MapEntryExpr{make_expr_ptr(StringLiteralExpr{"b"}), make_expr_ptr(IntegerLiteralExpr{"20"})});
    auto base = make_expr_ptr(MapLiteralExpr{std::move(entries)});
    auto expr = make_expr(IndexAccessExpr{std::move(base), make_expr_ptr(StringLiteralExpr{"b"})});
    auto result = eval_expr(expr, ctx);
    check(!result.has_errors(), "eval.map_index.no_error");
    auto *iv = std::get_if<IntValue>(&result.value.node);
    check(iv != nullptr && iv->value == 20, "eval.map_index.value");
}

void test_eval_map_index_missing() {
    EvalContext ctx;
    std::vector<MapEntryExpr> entries;
    entries.push_back(
        MapEntryExpr{make_expr_ptr(StringLiteralExpr{"a"}), make_expr_ptr(IntegerLiteralExpr{"1"})});
    auto base = make_expr_ptr(MapLiteralExpr{std::move(entries)});
    auto expr = make_expr(IndexAccessExpr{std::move(base), make_expr_ptr(StringLiteralExpr{"z"})});
    auto result = eval_expr(expr, ctx);
    check(result.has_errors(), "eval.map_index_missing.has_error");
}

void test_eval_uuid_equality() {
    // Bind two UUIDs into local scope and compare with ==.
    EvalContext ctx;
    auto a = make_uuid("550e8400e29b41d4a716446655440000");
    auto b = make_uuid("550E8400-E29B-41D4-A716-446655440000");
    check(a.has_value() && b.has_value(), "eval.uuid_eq.valid_sources");
    if (!a.has_value() || !b.has_value()) {
        return;
    }
    ctx.bind_local("a", std::move(*a));
    ctx.bind_local("b", std::move(*b));

    Path pa;
    pa.root_name = "a";
    Path pb;
    pb.root_name = "b";
    auto eq = make_expr(BinaryExpr{ExprBinaryOp::Equal, make_expr_ptr(PathExpr{pa}),
                                   make_expr_ptr(PathExpr{pb})});
    auto result = eval_expr(eq, ctx);
    check(!result.has_errors(), "eval.uuid_eq.no_error");
    auto *bv = std::get_if<BoolValue>(&result.value.node);
    check(bv != nullptr && bv->value == true, "eval.uuid_eq.normalized_equal");
}

void test_eval_timestamp_equality() {
    EvalContext ctx;
    ctx.bind_local("t1", make_timestamp(1000));
    ctx.bind_local("t2", make_timestamp(1000));
    ctx.bind_local("t3", make_timestamp(2000));

    Path p1;
    p1.root_name = "t1";
    Path p2;
    p2.root_name = "t2";
    Path p3;
    p3.root_name = "t3";

    auto eq = make_expr(BinaryExpr{ExprBinaryOp::Equal, make_expr_ptr(PathExpr{p1}),
                                   make_expr_ptr(PathExpr{p2})});
    auto eq_result = eval_expr(eq, ctx);
    auto *bv = std::get_if<BoolValue>(&eq_result.value.node);
    check(bv != nullptr && bv->value == true, "eval.timestamp_eq.equal");

    auto neq = make_expr(BinaryExpr{ExprBinaryOp::NotEqual, make_expr_ptr(PathExpr{p1}),
                                    make_expr_ptr(PathExpr{p3})});
    auto neq_result = eval_expr(neq, ctx);
    auto *bv2 = std::get_if<BoolValue>(&neq_result.value.node);
    check(bv2 != nullptr && bv2->value == true, "eval.timestamp_neq.unequal");
}

void test_eval_map_equality() {
    EvalContext ctx;
    std::vector<MapEntryExpr> a;
    a.push_back(
        MapEntryExpr{make_expr_ptr(StringLiteralExpr{"k"}), make_expr_ptr(IntegerLiteralExpr{"1"})});
    std::vector<MapEntryExpr> b;
    b.push_back(
        MapEntryExpr{make_expr_ptr(StringLiteralExpr{"k"}), make_expr_ptr(IntegerLiteralExpr{"1"})});
    auto eq = make_expr(BinaryExpr{ExprBinaryOp::Equal, make_expr_ptr(MapLiteralExpr{std::move(a)}),
                                   make_expr_ptr(MapLiteralExpr{std::move(b)})});
    auto result = eval_expr(eq, ctx);
    check(!result.has_errors(), "eval.map_eq.no_error");
    auto *bv = std::get_if<BoolValue>(&result.value.node);
    check(bv != nullptr && bv->value == true, "eval.map_eq.equal");
}

// ============================================================================
// Backward compatibility: existing kinds still work after variant extension
// ============================================================================

void test_back_compat_existing_kinds() {
    EvalContext ctx;
    // Int arithmetic still works.
    auto add = make_expr(BinaryExpr{ExprBinaryOp::Add, make_expr_ptr(IntegerLiteralExpr{"1"}),
                                    make_expr_ptr(IntegerLiteralExpr{"2"})});
    auto add_result = eval_expr(add, ctx);
    auto *iv = std::get_if<IntValue>(&add_result.value.node);
    check(iv != nullptr && iv->value == 3, "back_compat.int_add");

    // String equality still works.
    auto seq = make_expr(BinaryExpr{ExprBinaryOp::Equal, make_expr_ptr(StringLiteralExpr{"x"}),
                                    make_expr_ptr(StringLiteralExpr{"x"})});
    auto seq_result = eval_expr(seq, ctx);
    auto *bv = std::get_if<BoolValue>(&seq_result.value.node);
    check(bv != nullptr && bv->value == true, "back_compat.string_eq");

    // List length still works.
    std::vector<ExprRef> items;
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"1"}));
    items.push_back(make_expr_ptr(IntegerLiteralExpr{"2"}));
    auto base = make_expr_ptr(ListLiteralExpr{std::move(items)});
    auto len = make_expr(MemberAccessExpr{std::move(base), "length"});
    auto len_result = eval_expr(len, ctx);
    auto *liv = std::get_if<IntValue>(&len_result.value.node);
    check(liv != nullptr && liv->value == 2, "back_compat.list_length");
}

} // anonymous namespace

int main() {
    test_set_basic();
    test_set_dedup_and_order();
    test_set_order_independent_equality();
    test_set_unequal();
    test_set_string_keys();
    test_map_basic();
    test_map_last_write_wins();
    test_map_order_independent_equality();
    test_uuid_canonical();
    test_uuid_uppercase_normalized();
    test_uuid_bare_hex();
    test_uuid_invalid();
    test_uuid_equality();
    test_timestamp_basic();
    test_timestamp_equality();
    test_clone_set();
    test_clone_map();
    test_clone_uuid_timestamp();
    test_value_kind_new();
    test_print_new();
    test_json_uuid_roundtrip();
    test_json_timestamp_roundtrip();
    test_eval_set_literal();
    test_eval_map_literal();
    test_eval_set_equality();
    test_eval_set_length();
    test_eval_set_contains_via_index();
    test_eval_map_index();
    test_eval_map_index_missing();
    test_eval_uuid_equality();
    test_eval_timestamp_equality();
    test_eval_map_equality();
    test_back_compat_existing_kinds();

    std::cout << pass_count << "/" << test_count << " tests passed.\n";
    return (pass_count == test_count) ? 0 : 1;
}
