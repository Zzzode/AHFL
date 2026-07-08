#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/semantics/pattern_usefulness.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
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

[[nodiscard]] ahfl::PatternConstructorId
find_int_constructor(const ahfl::PatternUsefulnessContext &context,
                     ahfl::PatternDomainId domain,
                     std::int64_t value) {
    for (const auto constructor : context.domain(domain).constructors) {
        if (context.constructor(constructor).int_value == value) {
            return constructor;
        }
    }
    throw std::logic_error("test int constructor not found");
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
    CHECK(analysis.redundant_or_branches.front().covering_row_indices.empty());
    CHECK(analysis.redundant_or_branches.front().covering_branch_indices ==
          std::vector<std::size_t>{1});
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
    CHECK(analysis.redundant_or_branches.front().covering_row_indices.empty());
    CHECK(analysis.redundant_or_branches.front().covering_branch_indices ==
          std::vector<std::size_t>{0});
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Some(False)");
}

TEST_CASE("or-pattern redundancy records covering row source") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto first_true = context.make_constructor_pattern(bool_domain.true_ctor, {});
    const auto second_true = context.make_constructor_pattern(bool_domain.true_ctor, {});
    const auto false_pattern = context.make_constructor_pattern(bool_domain.false_ctor, {});
    const auto bool_or = context.make_or_pattern({second_true, false_pattern});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = first_true},
        ahfl::PatternUsefulnessRow{.pattern = bool_or},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, bool_domain.domain, rows);

    REQUIRE(analysis.redundant_or_branches.size() == 1);
    CHECK(analysis.redundant_or_branches.front().row_index == 1);
    CHECK(analysis.redundant_or_branches.front().or_pattern == bool_or);
    CHECK(analysis.redundant_or_branches.front().branch_index == 0);
    CHECK(analysis.redundant_or_branches.front().covering_row_indices ==
          std::vector<std::size_t>{0});
    CHECK(analysis.redundant_or_branches.front().covering_branch_indices.empty());
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

TEST_CASE("open int range pattern covers enumerated literal witnesses conservatively") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);
    const auto other = context.add_constructor(open, "_", {});
    const auto one = context.add_int_constructor(open, 1);
    const auto two = context.add_int_constructor(open, 2);
    const auto five = context.add_int_constructor(open, 5);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(1, 3)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(two, {})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witness->constructor == other);
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    REQUIRE(analysis.overlaps.size() == 1);
    CHECK(analysis.overlaps.front().row_index == 1);
    CHECK(analysis.overlaps.front().previous_row_index == 0);
    CHECK(ahfl::render_pattern_witness(context, ahfl::PatternWitness{.constructor = one}) == "1");
    CHECK(ahfl::render_pattern_witness(context, ahfl::PatternWitness{.constructor = five}) == "5");
}

TEST_CASE("open int range pattern does not match non-int constructors") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);
    const auto other = context.add_constructor(open, "_", {});
    const auto text = context.add_constructor(open, "\"2\"", {});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(1, 3)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witness->constructor == other);
    CHECK(analysis.unreachable_rows.empty());

    const std::vector text_rows{
        rows.front(),
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(text, {})},
    };
    const auto text_analysis = ahfl::analyze_pattern_usefulness(context, open, text_rows);
    CHECK(text_analysis.unreachable_rows.empty());
}

TEST_CASE("open signed int range pattern matches negative constructors") {
    ahfl::PatternUsefulnessContext context;
    const auto open = context.add_domain(ahfl::PatternDomainKind::Open);
    const auto other = context.add_constructor(open, "_", {});
    const auto minus_two = context.add_int_constructor(open, -2);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(-3, -1)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(minus_two, {})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, open, rows);

    CHECK_FALSE(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witness->constructor == other);
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    CHECK(ahfl::render_pattern_witness(context, ahfl::PatternWitness{.constructor = minus_two}) ==
          "-2");
}

TEST_CASE("bounded int domain reports missing finite range witness") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(-2, 2);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(-2, 0)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "1");
    REQUIRE(analysis.missing_witnesses.size() == 2);
    CHECK(ahfl::render_pattern_witness(context, analysis.missing_witnesses[0]) == "1");
    CHECK(ahfl::render_pattern_witness(context, analysis.missing_witnesses[1]) == "2");
}

TEST_CASE("bounded int domain proves exhaustive range coverage") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(-2, 2);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(-2, 0)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(1, 2)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witnesses.empty());
    CHECK(analysis.unreachable_rows.empty());
}

TEST_CASE("bounded int range coverage makes later singleton unreachable") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(-2, 2);
    const auto minus_one = find_int_constructor(context, domain, -1);

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(-2, 0)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(minus_one, {})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(1, 2)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    REQUIRE(analysis.overlaps.size() == 1);
    CHECK(analysis.overlaps.front().row_index == 1);
    CHECK(analysis.overlaps.front().previous_row_index == 0);
}

TEST_CASE("bounded int domain rejects invalid bounds") {
    ahfl::PatternUsefulnessContext context;
    CHECK_THROWS_AS(static_cast<void>(context.add_bounded_int_domain(2, -2)),
                    std::invalid_argument);
}

TEST_CASE("large bounded int domain proves exhaustive coverage without constructors") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(domain).constructors.empty());

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(0, 4999)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(5000, 10000)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witnesses.empty());
    CHECK(analysis.unreachable_rows.empty());
}

TEST_CASE("large bounded int domain reports interval missing witness without constructors") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(domain).constructors.empty());

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(0, 4999)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(5001, 10000)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "5000");
    REQUIRE(analysis.missing_witnesses.size() == 1);
    CHECK(ahfl::render_pattern_witness(context, analysis.missing_witnesses.front()) == "5000");
}

TEST_CASE("large bounded int domain reports unreachable covered interval row") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(domain).constructors.empty());

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(0, 9000)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(42, 99)},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(9001, 10000)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.unreachable_rows.size() == 1);
    CHECK(analysis.unreachable_rows.front().row_index == 1);
    CHECK(analysis.unreachable_rows.front().covering_row_indices == std::vector<std::size_t>{0});
    REQUIRE(analysis.overlaps.size() == 1);
    CHECK(analysis.overlaps.front().row_index == 1);
    CHECK(analysis.overlaps.front().previous_row_index == 0);
}

TEST_CASE("large bounded int domain reports redundant interval or-pattern branch") {
    ahfl::PatternUsefulnessContext context;
    const auto domain = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(domain).constructors.empty());

    const auto broad = context.make_int_range_pattern(0, 10);
    const auto duplicate_subset = context.make_int_range_pattern(5, 6);
    const auto int_or = context.make_or_pattern({broad, duplicate_subset});
    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = int_or},
        ahfl::PatternUsefulnessRow{.pattern = context.make_int_range_pattern(11, 10000)},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    REQUIRE(analysis.redundant_or_branches.size() == 1);
    CHECK(analysis.redundant_or_branches.front().row_index == 0);
    CHECK(analysis.redundant_or_branches.front().or_pattern == int_or);
    CHECK(analysis.redundant_or_branches.front().branch_index == 1);
    CHECK(analysis.redundant_or_branches.front().covering_row_indices.empty());
    CHECK(analysis.redundant_or_branches.front().covering_branch_indices ==
          std::vector<std::size_t>{0});
}

TEST_CASE("large bounded int constructor payload proves exhaustive coverage symbolically") {
    ahfl::PatternUsefulnessContext context;
    const auto option = context.add_domain();
    const auto payload = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(payload).constructors.empty());
    const auto none = context.add_constructor(option, "None", {});
    const auto some = context.add_constructor(option, "Some", {payload});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(
                                       some, {context.make_int_range_pattern(0, 4999)})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(
                                       some, {context.make_int_range_pattern(5000, 10000)})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(none, {})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, option, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.missing_witnesses.empty());
    CHECK(analysis.unreachable_rows.empty());
}

TEST_CASE("large bounded int constructor payload reports missing symbolic witness") {
    ahfl::PatternUsefulnessContext context;
    const auto option = context.add_domain();
    const auto payload = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(payload).constructors.empty());
    const auto none = context.add_constructor(option, "None", {});
    const auto some = context.add_constructor(option, "Some", {payload});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(
                                       some, {context.make_int_range_pattern(0, 4999)})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(
                                       some, {context.make_int_range_pattern(5001, 10000)})},
        ahfl::PatternUsefulnessRow{.pattern = context.make_constructor_pattern(none, {})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, option, rows);

    CHECK(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Some(5000)");
}

TEST_CASE("large bounded int product payload keeps finite sibling dimensions precise") {
    ahfl::PatternUsefulnessContext context;
    const auto pair_domain = context.add_domain();
    const auto big_int = context.add_bounded_int_domain(0, 10000);
    CHECK(context.domain(big_int).constructors.empty());
    const auto bool_domain = make_bool_domain(context);
    const auto pair = context.add_constructor(pair_domain, "Pair", {big_int, bool_domain.domain});

    const auto any_int = context.make_int_range_pattern(0, 10000);
    const std::vector rows{
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                pair, {any_int, context.make_constructor_pattern(bool_domain.false_ctor, {})})},
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                pair,
                {context.make_int_range_pattern(0, 10000),
                 context.make_constructor_pattern(bool_domain.true_ctor, {})})},
    };
    const auto analysis = ahfl::analyze_pattern_usefulness(context, pair_domain, rows);

    CHECK(analysis.root_domain_is_finite);
    CHECK_FALSE(analysis.missing_witness.has_value());
    CHECK(analysis.unreachable_rows.empty());

    const std::vector missing_rows{rows.front()};
    const auto missing = ahfl::analyze_pattern_usefulness(context, pair_domain, missing_rows);
    REQUIRE(missing.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *missing.missing_witness) == "Pair(0, True)");
}

TEST_CASE("closed constructor products fall back to symbolic analysis when witness cap is reached") {
    ahfl::PatternUsefulnessContext context;
    const auto bool_domain = make_bool_domain(context);
    const auto pair_domain = context.add_domain();
    const auto pair =
        context.add_constructor(pair_domain, "Pair", {bool_domain.domain, bool_domain.domain});

    const std::vector rows{
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                pair,
                {context.make_constructor_pattern(bool_domain.false_ctor, {}),
                 context.make_constructor_pattern(bool_domain.false_ctor, {})})},
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                pair,
                {context.make_constructor_pattern(bool_domain.false_ctor, {}),
                 context.make_constructor_pattern(bool_domain.true_ctor, {})})},
        ahfl::PatternUsefulnessRow{
            .pattern = context.make_constructor_pattern(
                pair,
                {context.make_constructor_pattern(bool_domain.true_ctor, {}),
                 context.make_constructor_pattern(bool_domain.false_ctor, {})})},
    };

    const auto analysis = ahfl::analyze_pattern_usefulness(
        context, pair_domain, rows, ahfl::PatternUsefulnessOptions{.max_witnesses = 1});

    CHECK(analysis.root_domain_is_finite);
    REQUIRE(analysis.missing_witness.has_value());
    CHECK(ahfl::render_pattern_witness(context, *analysis.missing_witness) == "Pair(True, True)");
    REQUIRE(analysis.missing_witnesses.size() == 1);
    CHECK_FALSE(analysis.witness_limit_exceeded);
}

TEST_CASE("open int range pattern rejects invalid bounds") {
    ahfl::PatternUsefulnessContext context;
    CHECK_THROWS_AS(static_cast<void>(context.make_int_range_pattern(5, 3)), std::invalid_argument);
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
