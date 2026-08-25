// RFC 0017 (BMC Contract Semantics) slice 2 unit tests: the SMT encoding layer.
//
// Exercises encode_predicate over the data-predicate subset:
//   * per-node encoding to the expected SMT-LIB 2 term text,
//   * byte-identical determinism across repeated encodings,
//   * divide/modulo divisor-non-zero obligations,
//   * optional integer overflow-guard obligations,
//   * the verifiable-subset boundary reported as a structured rejection.

#include "verification/formal/smt_encode.hpp"

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/expr.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace ahfl::formal;
namespace ir = ahfl::ir;

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

// ---------------------------------------------------------------------------
// Small IR builders over a standalone arena. The arena index doubles as the
// stable node id, matching Program::expr_arena semantics.
// ---------------------------------------------------------------------------

ir::ExprRef int_lit(ir::ExprArena &arena, const std::string &spelling) {
    return arena.make(ir::IntegerLiteralExpr{.spelling = spelling});
}

ir::ExprRef bool_lit(ir::ExprArena &arena, bool value) {
    return arena.make(ir::BoolLiteralExpr{.value = value});
}

ir::ExprRef str_lit(ir::ExprArena &arena, const std::string &spelling) {
    return arena.make(ir::StringLiteralExpr{.spelling = spelling});
}

ir::ExprRef path(ir::ExprArena &arena, const std::string &root, std::vector<std::string> members) {
    ir::Path p;
    p.root_kind = ir::PathRootKind::Identifier;
    p.root_name = root;
    p.members = std::move(members);
    return arena.make(ir::PathExpr{.path = std::move(p)});
}

ir::ExprRef binary(ir::ExprArena &arena, ir::ExprBinaryOp op, ir::ExprRef lhs, ir::ExprRef rhs) {
    return arena.make(ir::BinaryExpr{.op = op, .lhs = lhs, .rhs = rhs});
}

ir::ExprRef unary(ir::ExprArena &arena, ir::ExprUnaryOp op, ir::ExprRef operand) {
    return arena.make(ir::UnaryExpr{.op = op, .operand = operand});
}

// ---------------------------------------------------------------------------
// Per-node encoding
// ---------------------------------------------------------------------------

void test_bool_literal() {
    ir::ExprArena arena;
    auto t = encode_predicate(bool_lit(arena, true));
    check(t.ok() && t.term == "true", "bool literal true");
    auto f = encode_predicate(bool_lit(arena, false));
    check(f.ok() && f.term == "false", "bool literal false");
}

void test_integer_literal() {
    ir::ExprArena arena;
    auto pos = encode_predicate(int_lit(arena, "42"));
    check(pos.ok() && pos.term == "42", "positive integer literal");
    auto neg = encode_predicate(int_lit(arena, "-7"));
    check(neg.ok() && neg.term == "(- 7)", "negative integer literal encodes as (- n)");
}

void test_path_symbol() {
    ir::ExprArena arena;
    auto simple = encode_predicate(path(arena, "input", {}));
    check(simple.ok() && simple.term == "input", "bare path root symbol");
    auto nested = encode_predicate(path(arena, "input", {"score", "value"}));
    check(nested.ok() && nested.term == "input__score__value",
          "dotted path flattens to __-joined symbol");
}

void test_member_access() {
    ir::ExprArena arena;
    auto base = path(arena, "output", {});
    auto member = arena.make(ir::MemberAccessExpr{.base = base, .member = "count"});
    auto r = encode_predicate(member);
    check(r.ok() && r.term == "output__count", "member access folds into flat symbol");
}

void test_binary_comparison() {
    ir::ExprArena arena;
    auto e = binary(arena, ir::ExprBinaryOp::GreaterEqual, path(arena, "input", {"n"}),
                    int_lit(arena, "0"));
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(>= input__n 0)", "greater-equal comparison");
}

void test_binary_logical() {
    ir::ExprArena arena;
    auto lhs = binary(arena, ir::ExprBinaryOp::Greater, path(arena, "input", {"a"}),
                      int_lit(arena, "0"));
    auto rhs = binary(arena, ir::ExprBinaryOp::Less, path(arena, "input", {"b"}),
                      int_lit(arena, "10"));
    auto e = binary(arena, ir::ExprBinaryOp::And, lhs, rhs);
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(and (> input__a 0) (< input__b 10))", "nested and of comparisons");
}

void test_not_equal_rewrite() {
    ir::ExprArena arena;
    auto e = binary(arena, ir::ExprBinaryOp::NotEqual, path(arena, "input", {"x"}),
                    int_lit(arena, "3"));
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(not (= input__x 3))", "not-equal rewrites to (not (= ...))");
}

void test_unary() {
    ir::ExprArena arena;
    auto e = unary(arena, ir::ExprUnaryOp::Not, bool_lit(arena, true));
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(not true)", "logical not");

    auto neg = unary(arena, ir::ExprUnaryOp::Negate, path(arena, "input", {"n"}));
    auto rn = encode_predicate(neg);
    check(rn.ok() && rn.term == "(- input__n)", "arithmetic negate");

    auto pos = unary(arena, ir::ExprUnaryOp::Positive, path(arena, "input", {"n"}));
    auto rp = encode_predicate(pos);
    check(rp.ok() && rp.term == "input__n", "unary plus is identity");
}

// ---------------------------------------------------------------------------
// Divide / modulo obligations
// ---------------------------------------------------------------------------

void test_divide_obligation() {
    ir::ExprArena arena;
    auto e = binary(arena, ir::ExprBinaryOp::Divide, path(arena, "input", {"a"}),
                    path(arena, "input", {"b"}));
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(div input__a input__b)", "divide encodes to (div ...)");
    check(r.obligations.size() == 1 &&
              r.obligations[0].predicate == "(not (= input__b 0))",
          "divide emits divisor-non-zero obligation");
}

void test_modulo_obligation() {
    ir::ExprArena arena;
    auto e = binary(arena, ir::ExprBinaryOp::Modulo, path(arena, "input", {"a"}),
                    int_lit(arena, "5"));
    auto r = encode_predicate(e);
    check(r.ok() && r.term == "(mod input__a 5)", "modulo encodes to (mod ...)");
    check(r.obligations.size() == 1 && r.obligations[0].predicate == "(not (= 5 0))",
          "modulo emits divisor-non-zero obligation");
}

// ---------------------------------------------------------------------------
// Optional overflow checks
// ---------------------------------------------------------------------------

void test_overflow_check_opt_in() {
    ir::ExprArena arena;
    auto e = binary(arena, ir::ExprBinaryOp::Add, path(arena, "input", {"a"}),
                    path(arena, "input", {"b"}));

    auto off = encode_predicate(e);
    check(off.ok() && off.term == "(+ input__a input__b)" && off.obligations.empty(),
          "add without overflow checks emits no obligation");

    SmtEncodeOptions opts;
    opts.emit_overflow_checks = true;
    auto on = encode_predicate(e, opts);
    check(on.ok() && on.obligations.size() == 1,
          "add with overflow checks emits one obligation");
    check(on.obligations.size() == 1 &&
              on.obligations[0].predicate ==
                  "(and (<= (+ input__a input__b) 9223372036854775807) (>= (+ input__a "
                  "input__b) (- 9223372036854775808)))",
          "overflow obligation bounds the sum to INT64 range");
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

void test_determinism() {
    ir::ExprArena arena;
    auto build = [&](ir::ExprArena &a) {
        auto lhs = binary(a, ir::ExprBinaryOp::Add, path(a, "input", {"a"}), int_lit(a, "1"));
        return binary(a, ir::ExprBinaryOp::LessEqual, lhs, path(a, "output", {"cap"}));
    };
    auto first = encode_predicate(build(arena));
    ir::ExprArena arena2;
    auto second = encode_predicate(build(arena2));
    check(first.ok() && second.ok() && first.term == second.term,
          "identical expressions encode byte-identically across arenas");
}

// ---------------------------------------------------------------------------
// Verifiable-subset boundary -> rejection
// ---------------------------------------------------------------------------

void test_reject_string_content() {
    ir::ExprArena arena;
    auto r = encode_predicate(str_lit(arena, "\"hello\""));
    check(!r.ok() && r.rejection == SmtEncodeRejection::StringContent,
          "string literal is outside the verifiable subset");
}

void test_reject_unsupported_node() {
    ir::ExprArena arena;
    // A capability call has no data-predicate encoding.
    auto call = arena.make(ir::CallExpr{.callee = "classify", .arguments = {}, .callee_ref = {}});
    auto r = encode_predicate(call);
    check(!r.ok() && r.rejection == SmtEncodeRejection::UnsupportedNode,
          "capability call rejected as unsupported node");
}

void test_reject_null_expr() {
    auto r = encode_predicate(ir::ExprRef{});
    check(!r.ok() && r.rejection == SmtEncodeRejection::NullExpr, "null expr rejected");
}

void test_reject_propagates_through_operator() {
    ir::ExprArena arena;
    // A comparison whose rhs is a rejected string operand rejects the whole tree.
    auto e = binary(arena, ir::ExprBinaryOp::Equal, path(arena, "input", {"name"}),
                    str_lit(arena, "\"x\""));
    auto r = encode_predicate(e);
    check(!r.ok() && r.rejection == SmtEncodeRejection::StringContent,
          "rejection propagates from a nested operand");
}

void test_describe_rejection_nonempty() {
    bool all_nonempty = true;
    for (auto reason : {SmtEncodeRejection::UnsupportedNode, SmtEncodeRejection::UnsupportedOperator,
                        SmtEncodeRejection::StringContent, SmtEncodeRejection::UnsupportedType,
                        SmtEncodeRejection::NullExpr, SmtEncodeRejection::UnboundedQuantifier}) {
        if (describe_rejection(reason).empty()) {
            all_nonempty = false;
        }
    }
    check(all_nonempty, "every rejection reason has a human-readable description");
}

// ---------------------------------------------------------------------------
// RFC 0024/0025: bounded quantifier encoding via finite unrolling.
// ---------------------------------------------------------------------------

// A bounded List<Int>(capacity) type ref for the quantified-collection operand.
ir::TypeRef bounded_list_int(std::uint64_t capacity) {
    ir::TypeRef list;
    list.kind = ir::TypeRefKind::Struct;
    list.canonical_name = "std::collections::List";
    list.collection_capacity = capacity;
    auto element = ahfl::Owned<ir::TypeRef>(new ir::TypeRef{});
    element->kind = ir::TypeRefKind::Int;
    list.params.push_back(std::move(element));
    return list;
}

// forall x in <coll>: <body>, with `coll` a path carrying `coll_type`.
ir::ExprRef quantifier(ir::ExprArena &arena,
                       ir::QuantifierExpr::Kind kind,
                       const std::string &binder,
                       ir::TypeRef coll_type,
                       ir::ExprRef body) {
    ir::Path p;
    p.root_kind = ir::PathRootKind::Identifier;
    p.root_name = "coll";
    auto coll = arena.make(ir::PathExpr{.path = std::move(p)}, std::nullopt, std::move(coll_type));
    return arena.make(ir::QuantifierExpr{
        .kind = kind,
        .binder = binder,
        .value_binder = {},
        .collection = coll,
        .body = body,
    });
}

void test_quantifier_forall_unrolls_to_and() {
    ir::ExprArena arena;
    // forall x in coll: x > 0, coll : List<Int>(3)
    auto body = binary(arena, ir::ExprBinaryOp::Greater, path(arena, "x", {}), int_lit(arena, "0"));
    auto q =
        quantifier(arena, ir::QuantifierExpr::Kind::ForAll, "x", bounded_list_int(3), body);
    auto r = encode_predicate(q);
    check(r.ok() && r.term == "(and (> coll@0 0) (> coll@1 0) (> coll@2 0))",
          "forall unrolls to a conjunction with per-index element symbols");
}

void test_quantifier_exists_unrolls_to_or() {
    ir::ExprArena arena;
    auto body = binary(arena, ir::ExprBinaryOp::Equal, path(arena, "x", {}), int_lit(arena, "7"));
    auto q = quantifier(arena, ir::QuantifierExpr::Kind::Exists, "x", bounded_list_int(2), body);
    auto r = encode_predicate(q);
    check(r.ok() && r.term == "(or (= coll@0 7) (= coll@1 7))",
          "exists unrolls to a disjunction with per-index element symbols");
}

void test_quantifier_empty_collection_vacuous() {
    ir::ExprArena arena;
    auto body_all =
        binary(arena, ir::ExprBinaryOp::Greater, path(arena, "x", {}), int_lit(arena, "0"));
    auto forall =
        quantifier(arena, ir::QuantifierExpr::Kind::ForAll, "x", bounded_list_int(0), body_all);
    auto rf = encode_predicate(forall);
    check(rf.ok() && rf.term == "true", "forall over empty collection is vacuously true");

    auto body_ex =
        binary(arena, ir::ExprBinaryOp::Greater, path(arena, "x", {}), int_lit(arena, "0"));
    auto exists =
        quantifier(arena, ir::QuantifierExpr::Kind::Exists, "x", bounded_list_int(0), body_ex);
    auto re = encode_predicate(exists);
    check(re.ok() && re.term == "false", "exists over empty collection is vacuously false");
}

void test_quantifier_unbounded_rejected() {
    ir::ExprArena arena;
    // Collection type has no capacity => fail-closed.
    ir::TypeRef unbounded;
    unbounded.kind = ir::TypeRefKind::Struct;
    unbounded.canonical_name = "std::collections::List";
    auto element = ahfl::Owned<ir::TypeRef>(new ir::TypeRef{});
    element->kind = ir::TypeRefKind::Int;
    unbounded.params.push_back(std::move(element));

    auto body =
        binary(arena, ir::ExprBinaryOp::Greater, path(arena, "x", {}), int_lit(arena, "0"));
    auto q =
        quantifier(arena, ir::QuantifierExpr::Kind::ForAll, "x", std::move(unbounded), body);
    auto r = encode_predicate(q);
    check(!r.ok() && r.rejection == SmtEncodeRejection::UnboundedQuantifier,
          "quantifier over an unbounded collection is fail-closed");
}

void test_quantifier_determinism() {
    ir::ExprArena arena;
    auto body =
        binary(arena, ir::ExprBinaryOp::GreaterEqual, path(arena, "x", {}), int_lit(arena, "0"));
    auto q = quantifier(arena, ir::QuantifierExpr::Kind::ForAll, "x", bounded_list_int(4), body);
    auto a = encode_predicate(q);
    auto b = encode_predicate(q);
    check(a.ok() && b.ok() && a.term == b.term,
          "quantifier unrolling is byte-identical across repeated encodings");
}

} // namespace

int main() {
    test_bool_literal();
    test_integer_literal();
    test_path_symbol();
    test_member_access();
    test_binary_comparison();
    test_binary_logical();
    test_not_equal_rewrite();
    test_unary();

    test_divide_obligation();
    test_modulo_obligation();

    test_overflow_check_opt_in();

    test_determinism();

    test_reject_string_content();
    test_reject_unsupported_node();
    test_reject_null_expr();
    test_reject_propagates_through_operator();
    test_describe_rejection_nonempty();

    test_quantifier_forall_unrolls_to_and();
    test_quantifier_exists_unrolls_to_or();
    test_quantifier_empty_collection_vacuous();
    test_quantifier_unbounded_rejected();
    test_quantifier_determinism();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
