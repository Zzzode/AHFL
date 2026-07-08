#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <vector>

namespace {

struct BoolDomain {
    ahfl::PatternDomainId domain;
    ahfl::PatternConstructorId false_ctor;
    ahfl::PatternConstructorId true_ctor;
};

[[nodiscard]] BoolDomain make_bool_domain(ahfl::PatternUsefulnessContext &context) {
    const auto domain = context.add_domain();
    const auto false_ctor = context.add_constructor(domain, "False", {});
    const auto true_ctor = context.add_constructor(domain, "True", {});
    return BoolDomain{.domain = domain, .false_ctor = false_ctor, .true_ctor = true_ctor};
}

} // namespace

TEST_CASE("pattern usefulness reports missing finite constructors by id") {
    ahfl::PatternUsefulnessContext context;
    const auto color = context.add_domain();
    const auto red = context.add_constructor(color, "Red", {});
    const auto green = context.add_constructor(color, "Green", {});
    const auto blue = context.add_constructor(color, "Blue", {});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(red, {})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(green, {})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, color, rows);

    REQUIRE(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Blue");
    CHECK(analysis.unreachable_rows.empty());

    const std::vector complete_rows{
        rows[0],
        rows[1],
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(blue, {})},
    };

    const auto complete = ahfl::analyze_pattern_usefulness(context, color, complete_rows);
    CHECK_FALSE(complete.missing_witness.has_value());
}

TEST_CASE("pattern usefulness treats wildcard coverage as making later rows unreachable") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_wildcard()},
        ahfl::PatternUsefulnessRow{.pattern =
                                       context.make_constructor_pattern(bool_domain.true_ctor, {})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, bool_domain.domain, rows);

    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    CHECK(analysis.unreachable_rows.front().covering_row_indices == std::vector<std::size_t>{0});
    REQUIRE(analysis.overlaps.size() == 1);
    CHECK(analysis.overlaps.front().row_index == 1);
    CHECK(analysis.overlaps.front().previous_row_index == 0);
}

TEST_CASE("guarded rows are useful but do not prove exhaustiveness") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(bool_domain.true_ctor, {}),
            .contributes_to_exhaustiveness = false,
        },
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(bool_domain.false_ctor, {})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, bool_domain.domain, rows);

    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "True");
    CHECK(analysis.unreachable_rows.empty());
}

TEST_CASE("or-pattern analysis reports branch redundancy") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto false_pattern = context.make_constructor_pattern(bool_domain.false_ctor, {});
    const auto true_pattern = context.make_constructor_pattern(bool_domain.true_ctor, {});
    const auto duplicate_true = context.make_constructor_pattern(bool_domain.true_ctor, {});
    const auto bool_or = context.make_or_pattern({false_pattern, true_pattern, duplicate_true});

    const std::vector rows{ahfl::PatternUsefulnessRow{.pattern = bool_or}};
    const auto analysis = ahfl::analyze_pattern_usefulness(context, bool_domain.domain, rows);

    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.redundant_or_branches.size() == 1);
    CHECK(analysis.redundant_or_branches.front().row_index == 0);
    CHECK(analysis.redundant_or_branches.front().or_pattern == bool_or);
    CHECK(analysis.redundant_or_branches.front().branch_index == 2);
}

TEST_CASE("nested constructor matrix produces payload witnesses") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto option = context.add_domain();
    const auto none = context.add_constructor(option, "None", {});
    const auto some = context.add_constructor(option, "Some", {bool_domain.domain});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(none, {})},
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                some, {context.make_constructor_pattern(bool_domain.true_ctor, {})})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, option, rows);

    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Some(False)");
    REQUIRE(analysis.missing_witnesses.size() == 1);
    CHECK(ahfl::render_pattern_witness(context, analysis.missing_witnesses.front()) ==
          "Some(False)");
}

TEST_CASE("struct constructor witness renders named fields") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto packet = context.add_domain();
    const auto empty = context.add_constructor(packet, "Empty", {});
    const auto data =
        context.add_constructor(packet,
                                "Data",
                                {bool_domain.domain, bool_domain.domain},
                                ahfl::PatternConstructorDisplay{
                                    .payload_kind = ahfl::PatternConstructorPayloadKind::Struct,
                                    .field_names = {"flag", "other"},
                                });

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(empty, {})},
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                data,
                {context.make_constructor_pattern(bool_domain.true_ctor, {}),
                 context.make_constructor_pattern(bool_domain.false_ctor, {})})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, packet, rows);

    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) ==
          "Data { flag: False, other: False }");
}

TEST_CASE("nested or-pattern redundancy is checked in row context") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto option = context.add_domain();
    const auto none = context.add_constructor(option, "None", {});
    const auto some = context.add_constructor(option, "Some", {bool_domain.domain});

    const auto true_or_true = context.make_or_pattern({
        context.make_constructor_pattern(bool_domain.true_ctor, {}),
        context.make_constructor_pattern(bool_domain.true_ctor, {}),
    });
    const auto some_true_or_true = context.make_constructor_pattern(some, {true_or_true});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = some_true_or_true},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(none, {})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(context, option, rows);

    REQUIRE(analysis.redundant_or_branches.size() == 1);
    CHECK(analysis.redundant_or_branches.front().row_index == 0);
    CHECK(analysis.redundant_or_branches.front().or_pattern == true_or_true);
    CHECK(analysis.redundant_or_branches.front().branch_index == 1);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Some(False)");
}

TEST_CASE("open domains do not claim complete exhaustiveness") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);

    const std::vector rows{ahfl::PatternUsefulnessRow{.pattern = context.make_wildcard()}};
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.unreachable_rows.empty());
    CHECK(analysis.redundant_or_branches.empty());
}

TEST_CASE("open domains report symbolic default witness after singleton literal") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);
    const auto other = context.add_constructor(open, "_", {});
    const auto one = context.add_constructor(open, "1", {});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(one, {})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witness->constructor == other);
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "_");
    CHECK(analysis.unreachable_rows.empty());
}

TEST_CASE("open domains report duplicate singleton literal as unreachable") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);
    (void)context.add_constructor(open, "_", {});
    const auto one = context.add_constructor(open, "1", {});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(one, {})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(one, {})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_wildcard()},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    REQUIRE(analysis.overlaps.size() == 3);
    CHECK(analysis.overlaps.front().row_index == 1);
    CHECK(analysis.overlaps.front().previous_row_index == 0);
}

TEST_CASE("never patterns do not overlap or cover finite witnesses") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_never()},
        ahfl::PatternUsefulnessRow{.pattern = context.make_wildcard()},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, bool_domain.domain, rows);

    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.unreachable_rows.empty());
    CHECK(analysis.overlaps.empty());
}
