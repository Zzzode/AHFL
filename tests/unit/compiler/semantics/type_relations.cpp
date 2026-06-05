#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/semantics/type_relations.hpp"

namespace {

[[nodiscard]] ahfl::TypePtr string_type() {
    return ahfl::Type::string();
}

[[nodiscard]] ahfl::TypePtr bounded_string_type(std::int64_t minimum, std::int64_t maximum) {
    return ahfl::Type::bounded_string(minimum, maximum);
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

TEST_CASE("type relations keep collection element types invariant") {
    const auto list_bounded = ahfl::Type::list(bounded_string_type(2, 8));
    const auto list_string = ahfl::Type::list(string_type());
    const auto set_bounded = ahfl::Type::set(bounded_string_type(2, 8));
    const auto set_string = ahfl::Type::set(string_type());
    const auto optional_bounded = ahfl::Type::optional(bounded_string_type(2, 8));
    const auto optional_string = ahfl::Type::optional(string_type());

    CHECK_FALSE(ahfl::is_subtype_of(*list_bounded, *list_string));
    CHECK_FALSE(ahfl::is_subtype_of(*set_bounded, *set_string));
    CHECK_FALSE(ahfl::is_subtype_of(*optional_bounded, *optional_string));
}

TEST_CASE("type relations keep map key and value types invariant") {
    const auto bounded_to_int =
        ahfl::Type::map(bounded_string_type(2, 8), ahfl::Type::make(ahfl::TypeKind::Int));
    const auto string_to_int =
        ahfl::Type::map(string_type(), ahfl::Type::make(ahfl::TypeKind::Int));
    const auto string_to_bounded =
        ahfl::Type::map(string_type(), bounded_string_type(2, 8));
    const auto string_to_string =
        ahfl::Type::map(string_type(), string_type());

    CHECK_FALSE(ahfl::is_subtype_of(*bounded_to_int, *string_to_int));
    CHECK_FALSE(ahfl::is_subtype_of(*string_to_bounded, *string_to_string));
}

TEST_CASE("type relations keep decimal scales exact") {
    const auto decimal_2 = ahfl::Type::decimal(2);
    const auto decimal_4 = ahfl::Type::decimal(4);

    CHECK_FALSE(ahfl::is_subtype_of(*decimal_2, *decimal_4));
    CHECK_FALSE(ahfl::is_assignable_to(*decimal_2, *decimal_4));
}

TEST_CASE("nominal types prefer symbol identity over display names") {
    const auto left = ahfl::Type::struct_type("pkg::Request", ahfl::SymbolId{1});
    const auto right_same_name = ahfl::Type::struct_type("pkg::Request", ahfl::SymbolId{2});
    const auto right_same_symbol = ahfl::Type::struct_type("pkg::RenamedRequest", ahfl::SymbolId{1});
    const auto fallback_left = ahfl::Type::struct_type("pkg::Request");
    const auto fallback_right = ahfl::Type::struct_type("pkg::Request");

    CHECK_FALSE(ahfl::are_types_equivalent(*left, *right_same_name));
    CHECK(ahfl::are_types_equivalent(*left, *right_same_symbol));
    CHECK(ahfl::are_types_equivalent(*fallback_left, *fallback_right));
}
