#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/type_relations.hpp"

namespace {

[[nodiscard]] ahfl::TypePtr string_type() {
    return ahfl::TypeContext::global().string();
}

[[nodiscard]] ahfl::TypePtr bounded_string_type(std::int64_t minimum, std::int64_t maximum) {
    return ahfl::TypeContext::global().bounded_string(minimum, maximum);
}

} // namespace

TEST_CASE("type relations preserve bounded string subtyping") {
    const auto string = string_type();
    const auto bounded = bounded_string_type(2, 8);
    const auto wider = bounded_string_type(0, 16);
    const auto narrower = bounded_string_type(4, 6);

    CHECK(ahfl::is_subtype_of(*bounded, *string));
    CHECK(ahfl::is_subtype_of(*bounded, *wider));
    CHECK_FALSE(ahfl::is_subtype_of(*bounded, *narrower));
}

TEST_CASE("type relations support covariant container element types") {
    auto &tc = ahfl::TypeContext::global();
    const auto list_bounded = tc.list(bounded_string_type(2, 8));
    const auto list_string = tc.list(string_type());
    const auto set_bounded = tc.set(bounded_string_type(2, 8));
    const auto set_string = tc.set(string_type());
    const auto optional_bounded = tc.optional(bounded_string_type(2, 8));
    const auto optional_string = tc.optional(string_type());

    // Container covariance: List<A> <: List<B> if A <: B (same for Set, Optional)
    CHECK(ahfl::is_subtype_of(*list_bounded, *list_string));
    CHECK(ahfl::is_subtype_of(*set_bounded, *set_string));
    CHECK(ahfl::is_subtype_of(*optional_bounded, *optional_string));

    // Reverse direction should not hold (String is NOT <: BoundedString)
    CHECK_FALSE(ahfl::is_subtype_of(*list_string, *list_bounded));
    CHECK_FALSE(ahfl::is_subtype_of(*set_string, *set_bounded));
    CHECK_FALSE(ahfl::is_subtype_of(*optional_string, *optional_bounded));
}

TEST_CASE("type relations keep map keys invariant but values covariant") {
    auto &tc = ahfl::TypeContext::global();
    const auto bounded_to_int = tc.map(bounded_string_type(2, 8), tc.make(ahfl::TypeKind::Int));
    const auto string_to_int = tc.map(string_type(), tc.make(ahfl::TypeKind::Int));
    const auto string_to_bounded = tc.map(string_type(), bounded_string_type(2, 8));
    const auto string_to_string = tc.map(string_type(), string_type());

    // Map keys are invariant: BoundedString != String at the key position.
    CHECK_FALSE(ahfl::is_subtype_of(*bounded_to_int, *string_to_int));
    // Map values are covariant: BoundedString <: String, so Map<String,BS> <: Map<String,String>.
    CHECK(ahfl::is_subtype_of(*string_to_bounded, *string_to_string));
    // Reverse direction for values should not hold.
    CHECK_FALSE(ahfl::is_subtype_of(*string_to_string, *string_to_bounded));
}

TEST_CASE("type relations reject implicit numeric subtyping by default") {
    auto &tc = ahfl::TypeContext::global();
    const auto decimal_2 = tc.decimal(2);
    const auto decimal_4 = tc.decimal(4);
    const auto i32 = tc.make(ahfl::TypeKind::Int);
    const auto f64 = tc.make(ahfl::TypeKind::Float);

    CHECK_FALSE(ahfl::is_subtype_of(*i32, *f64));
    CHECK_FALSE(ahfl::is_assignable_to(*i32, *f64));
    CHECK_FALSE(ahfl::is_subtype_of(*i32, *decimal_2));
    CHECK_FALSE(ahfl::is_assignable_to(*i32, *decimal_2));
    CHECK_FALSE(ahfl::is_subtype_of(*decimal_2, *f64));
    CHECK_FALSE(ahfl::is_assignable_to(*decimal_2, *f64));
    CHECK_FALSE(ahfl::is_subtype_of(*decimal_2, *decimal_4));
    CHECK_FALSE(ahfl::is_assignable_to(*decimal_2, *decimal_4));
    CHECK_FALSE(ahfl::is_subtype_of(*decimal_4, *decimal_2));
    CHECK_FALSE(ahfl::is_assignable_to(*decimal_4, *decimal_2));
}

TEST_CASE("nominal types prefer symbol identity over display names") {
    auto &tc = ahfl::TypeContext::global();
    const auto struct_left = tc.struct_type("pkg::Request", ahfl::SymbolId{1});
    const auto struct_same_name = tc.struct_type("pkg::Request", ahfl::SymbolId{2});
    const auto struct_same_symbol = tc.struct_type("pkg::RenamedRequest", ahfl::SymbolId{1});
    const auto struct_fallback_left = tc.struct_type("pkg::Request");
    const auto struct_fallback_right = tc.struct_type("pkg::Request");

    CHECK_FALSE(ahfl::are_types_equivalent(*struct_left, *struct_same_name));
    CHECK(ahfl::are_types_equivalent(*struct_left, *struct_same_symbol));
    CHECK(ahfl::are_types_equivalent(*struct_fallback_left, *struct_fallback_right));

    const auto enum_left = tc.enum_type("pkg::Priority", ahfl::SymbolId{3});
    const auto enum_same_name = tc.enum_type("pkg::Priority", ahfl::SymbolId{4});
    const auto enum_same_symbol = tc.enum_type("pkg::RenamedPriority", ahfl::SymbolId{3});
    const auto enum_fallback_left = tc.enum_type("pkg::Priority");
    const auto enum_fallback_right = tc.enum_type("pkg::Priority");

    CHECK_FALSE(ahfl::are_types_equivalent(*enum_left, *enum_same_name));
    CHECK(ahfl::are_types_equivalent(*enum_left, *enum_same_symbol));
    CHECK(ahfl::are_types_equivalent(*enum_fallback_left, *enum_fallback_right));

    const auto variant_left = tc.enum_variant_type("pkg::Priority", "High", ahfl::SymbolId{5});
    const auto variant_same_name = tc.enum_variant_type("pkg::Priority", "High", ahfl::SymbolId{6});
    const auto variant_same_symbol =
        tc.enum_variant_type("pkg::RenamedPriority", "High", ahfl::SymbolId{5});
    const auto variant_other_member =
        tc.enum_variant_type("pkg::RenamedPriority", "Low", ahfl::SymbolId{5});
    const auto variant_fallback_left = tc.enum_variant_type("pkg::Priority", "High");
    const auto variant_fallback_right = tc.enum_variant_type("pkg::Priority", "High");

    CHECK_FALSE(ahfl::are_types_equivalent(*variant_left, *variant_same_name));
    CHECK(ahfl::are_types_equivalent(*variant_left, *variant_same_symbol));
    CHECK_FALSE(ahfl::are_types_equivalent(*variant_left, *variant_other_member));
    CHECK(ahfl::are_types_equivalent(*variant_fallback_left, *variant_fallback_right));
}

TEST_CASE("constraint skeleton traces nested Map->List->Struct nominal mismatch") {
    using Kind = ahfl::TypeConstraintNode::Kind;

    auto &tc = ahfl::TypeContext::global();
    const auto struct_a = tc.struct_type("pkg::StructA", ahfl::SymbolId{1});
    const auto struct_b = tc.struct_type("pkg::StructB", ahfl::SymbolId{2});

    const auto list_a = tc.list(struct_a);
    const auto list_b = tc.list(struct_b);

    const auto src = tc.map(tc.string(), list_a);
    const auto dst = tc.map(tc.string(), list_b);

    ahfl::TypeRelationOptions opts;
    opts.emit_constraint_skeleton = true;
    ahfl::TypeRelationContext ctx(opts);

    const bool assignable = ahfl::is_assignable_to(*src, *dst, ctx);

    // String -> String matches, but StructA vs StructB differs nominally, so
    // the overall assignable check must fail.
    CHECK_FALSE(assignable);

    const auto &skel = ctx.skeleton();
    const auto &root = skel.root;

    // Root is the synthetic And frame pushed by the public entry point.
    REQUIRE(root.kind == Kind::And);
    REQUIRE(root.children.size() >= 1);

    // The root's first child is the Or (subtype disjunction), whose first
    // child is the equivalence branch ("equiv").
    const auto &top_or = root.children[0];
    REQUIRE(top_or.kind == Kind::Or);
    REQUIRE(top_or.children.size() >= 1);

    const auto &equiv = top_or.children[0];
    // equiv is the "equiv" path composite (And node for map: key + value).
    REQUIRE(equiv.kind == Kind::And);
    REQUIRE(equiv.children.size() == 2);

    // First child: map.key relation between String and String -> satisfied.
    const auto &map_key = equiv.children[0];
    CHECK(map_key.path.find("map.key") != std::string::npos);
    CHECK(map_key.satisfied);

    // Second child: map.value (And -> list.element (And -> struct.name fails)).
    const auto &map_value = equiv.children[1];
    CHECK(map_value.path.find("map.value") != std::string::npos);
    CHECK(map_value.kind == Kind::And);
    CHECK_FALSE(map_value.satisfied);

    REQUIRE(map_value.children.size() >= 1);
    const auto &list_element = map_value.children[0];
    CHECK(list_element.path.find("list.element") != std::string::npos);
    CHECK(list_element.kind == Kind::And);
    CHECK_FALSE(list_element.satisfied);

    REQUIRE(list_element.children.size() == 1);
    const auto &struct_name = list_element.children[0];
    CHECK(struct_name.path.find("struct.name") != std::string::npos);
    CHECK(struct_name.kind == Kind::Relation);
    CHECK(struct_name.relation == "equivalent");
    CHECK_FALSE(struct_name.satisfied);
    CHECK(struct_name.left_describe.find("StructA") != std::string::npos);
    CHECK(struct_name.right_describe.find("StructB") != std::string::npos);

    // to_string() snapshot assertions.
    const std::string dump = skel.to_string();
    CHECK(dump.find("map.key") != std::string::npos);
    CHECK(dump.find("map.value") != std::string::npos);
    CHECK(dump.find("list.element") != std::string::npos);
    CHECK(dump.find("struct.name") != std::string::npos);
    CHECK(dump.find("StructA") != std::string::npos);
    CHECK(dump.find("StructB") != std::string::npos);
    CHECK(dump.find("[satisfied]") != std::string::npos);
    CHECK(dump.find("[failed]") != std::string::npos);
    CHECK(dump.find("RELATION(equivalent)") != std::string::npos);
    CHECK(dump.find("AND") != std::string::npos);
    CHECK(dump.find("OR") != std::string::npos);
}

TEST_CASE("constraint skeleton respects default-off flag (zero allocation path)") {
    auto &tc = ahfl::TypeContext::global();
    const auto s = tc.string();
    const auto t = tc.string();

    // Default options: emit_constraint_skeleton == false.
    ahfl::TypeRelationContext ctx_default;
    const bool ok = ahfl::are_types_equivalent(*s, *t, ctx_default);
    CHECK(ok);
    // The skeleton is left empty (no emission).
    CHECK(ctx_default.skeleton().root.kind == ahfl::TypeConstraintNode::Kind::Leaf);
    CHECK(ctx_default.skeleton().root.children.empty());
}

TEST_CASE("flat trace records assignable and exact-schema top-level steps") {
    // Minimal sanity test: with enable_trace=true, the free-function entry
    // points (is_assignable_to / is_exact_schema_match) record top-level
    // TypeRelationKind-tagged steps so that TypeCheckResult.relation_trace
    // can distinguish which API was actually invoked at the boundary.
    using namespace ahfl;

    auto &tc = TypeContext::global();
    const auto str = tc.string();
    const auto i32 = tc.make(TypeKind::Int);

    TypeRelationOptions opts;
    opts.enable_trace = true;
    TypeRelationContext ctx(opts);
    (void)is_assignable_to(*i32, *str, ctx);
    bool saw_assignable = false;
    for (const auto &s : ctx.trace().steps) {
        saw_assignable = saw_assignable || s.kind == TypeRelationKind::Assignable;
    }
    CHECK(saw_assignable);

    TypeRelationContext ctx2(opts);
    (void)is_exact_schema_match(*str, *str, ctx2);
    bool saw_exact = false;
    for (const auto &s : ctx2.trace().steps) {
        saw_exact = saw_exact || s.kind == TypeRelationKind::ExactSchema;
    }
    CHECK(saw_exact);

    // Equivalence failure must also produce a failed step.
    TypeRelationContext ctx3(opts);
    (void)are_types_equivalent(*str, *i32, ctx3);
    bool has_failed = false;
    for (const auto &s : ctx3.trace().steps) {
        has_failed = has_failed || s.result == TypeRelationResult::Rejected;
    }
    CHECK(has_failed);
}

TEST_CASE("flat trace records list/set/map element mismatch paths") {
    // P1.2: collection mismatches must identify the failing inner component
    // via a tagged path like "list.element", "map.key", "map.value".
    using namespace ahfl;

    TypeRelationOptions opts;
    opts.enable_trace = true;

    auto &tc = TypeContext::global();
    const auto str = tc.string();
    const auto i32 = tc.make(TypeKind::Int);

    // List<int> vs List<string>
    {
        TypeRelationContext ctx(opts);
        const bool ok = are_types_equivalent(*tc.list(i32), *tc.list(str), ctx);
        CHECK_FALSE(ok);
        bool found = false;
        for (const auto &s : ctx.trace().steps) {
            if (s.path == "list.element")
                found = true;
        }
        CHECK(found);
    }
    // Map<int, string> vs Map<string, string> → fails at key
    {
        TypeRelationContext ctx(opts);
        const bool ok = are_types_equivalent(*tc.map(i32, str), *tc.map(str, str), ctx);
        CHECK_FALSE(ok);
        bool found_key = false, found_value = false;
        for (const auto &s : ctx.trace().steps) {
            if (s.path == "map.key")
                found_key = true;
            if (s.path == "map.value")
                found_value = true;
        }
        CHECK(found_key);
        // key mismatch short-circuits the value recursion (equivalent_pairwise
        // uses &&) — the value path is only exercised when keys match.
        CHECK_FALSE(found_value);
    }
    // Map<string, int> vs Map<string, string> → key matches, value fails
    {
        TypeRelationContext ctx(opts);
        const bool ok = are_types_equivalent(*tc.map(str, i32), *tc.map(str, str), ctx);
        CHECK_FALSE(ok);
        bool found_value = false;
        for (const auto &s : ctx.trace().steps) {
            if (s.path == "map.value")
                found_value = true;
        }
        CHECK(found_value);
    }
}

TEST_CASE("TypeRelationOptions::allow_bounded_string_relaxation gates BS <: String") {
    using namespace ahfl;
    auto &tc = TypeContext::global();
    const auto bs = tc.bounded_string(2, 8);
    const auto s = tc.string();

    // Default (ON): BoundedString <: String holds.
    {
        TypeRelationContext ctx;
        CHECK(ctx.subtype(*bs, *s));
        CHECK(ctx.assignable(*bs, *s));
    }
    // Strict (OFF): no relaxation — subtype fails.
    {
        TypeRelationOptions opts;
        opts.allow_bounded_string_relaxation = false;
        TypeRelationContext ctx(opts);
        CHECK_FALSE(ctx.subtype(*bs, *s));
        CHECK_FALSE(ctx.assignable(*bs, *s));
    }
    // OFF also disables BoundedString covariance.
    {
        TypeRelationOptions opts;
        opts.allow_bounded_string_relaxation = false;
        TypeRelationContext ctx(opts);
        const auto narrow = tc.bounded_string(3, 7);
        const auto wide = tc.bounded_string(0, 16);
        CHECK_FALSE(ctx.subtype(*narrow, *wide));
    }
}

TEST_CASE("TypeRelationOptions::allow_numeric_widening explicitly enables numeric promotion") {
    using namespace ahfl;
    auto &tc = TypeContext::global();
    const auto i32 = tc.make(TypeKind::Int);
    const auto f64 = tc.make(TypeKind::Float);
    const auto decimal_2 = tc.decimal(2);
    const auto decimal_4 = tc.decimal(4);

    // Default (OFF): source-level AHFL has no implicit numeric subtyping.
    {
        TypeRelationContext ctx;
        CHECK_FALSE(ctx.subtype(*i32, *f64));
        CHECK_FALSE(ctx.assignable(*i32, *f64));
        CHECK_FALSE(ctx.subtype(*i32, *decimal_2));
        CHECK_FALSE(ctx.assignable(*i32, *decimal_2));
        CHECK_FALSE(ctx.subtype(*decimal_2, *decimal_4));
        CHECK_FALSE(ctx.assignable(*decimal_2, *decimal_4));
    }
    // Compatibility mode (ON): legacy widening remains available to callers
    // that deliberately opt out of source-level strictness.
    {
        TypeRelationOptions opts;
        opts.allow_numeric_widening = true;
        TypeRelationContext ctx(opts);
        CHECK(ctx.subtype(*i32, *f64));
        CHECK(ctx.assignable(*i32, *f64));
        CHECK(ctx.subtype(*i32, *decimal_2));
        CHECK(ctx.assignable(*i32, *decimal_2));
        CHECK(ctx.subtype(*decimal_2, *decimal_4));
        CHECK(ctx.assignable(*decimal_2, *decimal_4));
        CHECK_FALSE(ctx.subtype(*decimal_2, *f64));
        CHECK_FALSE(ctx.assignable(*decimal_2, *f64));
        CHECK_FALSE(ctx.subtype(*decimal_4, *decimal_2));
        CHECK_FALSE(ctx.assignable(*decimal_4, *decimal_2));
    }
    // Float never assigns to Int regardless of the flag.
    {
        TypeRelationContext ctx;
        CHECK_FALSE(ctx.subtype(*f64, *i32));
        CHECK_FALSE(ctx.assignable(*f64, *i32));
    }
}

TEST_CASE("MemoizedRelationSolver reuses proven and disproven relation states") {
    using namespace ahfl;
    auto &tc = TypeContext::global();
    const auto bounded = tc.bounded_string(2, 8);
    const auto string = tc.string();
    const auto list_bounded = tc.list(bounded);
    const auto list_string = tc.list(string);

    TypeRelationContext ctx;
    MemoizedRelationSolver solver(ctx);

    CHECK(solver.subtype(*list_bounded, *list_string));
    const auto memo_after_success = solver.memo_size();
    const auto hits_after_success = solver.stats().cache_hits;
    CHECK(solver.subtype(*list_bounded, *list_string));
    CHECK(solver.memo_size() == memo_after_success);
    CHECK(solver.stats().cache_hits > hits_after_success);

    CHECK_FALSE(solver.subtype(*list_string, *list_bounded));
    const auto memo_after_failure = solver.memo_size();
    const auto hits_after_failure = solver.stats().cache_hits;
    CHECK_FALSE(solver.subtype(*list_string, *list_bounded));
    CHECK(solver.memo_size() == memo_after_failure);
    CHECK(solver.stats().cache_hits > hits_after_failure);
    CHECK(solver.stats().proven > 0);
    CHECK(solver.stats().disproven > 0);
}

TEST_CASE("MemoizedRelationSolver applies coinductive visiting assumption") {
    using namespace ahfl;

    Type left;
    Type right;
    left.payload = types::ListT{.element = &left};
    right.payload = types::ListT{.element = &right};

    TypeRelationContext ctx;
    MemoizedRelationSolver solver(ctx);

    CHECK(solver.equivalent(left, right));
    CHECK(solver.stats().coinductive_assumptions == 1);
}

TEST_CASE("MemoizedRelationSolver fails closed at depth guard") {
    using namespace ahfl;
    auto &tc = TypeContext::global();
    const auto deep_int = tc.list(tc.list(tc.make(TypeKind::Int)));
    const auto deep_string = tc.list(tc.list(tc.string()));

    TypeRelationOptions opts;
    opts.max_solver_depth = 1;
    TypeRelationContext ctx(opts);
    MemoizedRelationSolver solver(ctx);

    CHECK_FALSE(solver.subtype(*deep_int, *deep_string));
    CHECK(solver.stats().depth_guard_rejections > 0);
}

TEST_CASE("TypeRelationContext member methods delegate to free functions") {
    using namespace ahfl;
    auto &tc = TypeContext::global();
    const auto s = tc.string();
    const auto bs = tc.bounded_string(2, 8);
    const auto i32 = tc.make(TypeKind::Int);

    TypeRelationContext ctx;

    // equivalent
    CHECK(ctx.equivalent(*s, *s));
    CHECK_FALSE(ctx.equivalent(*s, *i32));

    // subtype (default: BS <: String on)
    CHECK(ctx.subtype(*bs, *s));

    // assignable (default: numeric widening off)
    CHECK_FALSE(ctx.assignable(*i32, *tc.make(TypeKind::Float)));

    // exact_schema
    CHECK(ctx.exact_schema(*s, *s));
    CHECK_FALSE(ctx.exact_schema(*bs, *s));
}
