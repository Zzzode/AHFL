#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] ahfl::ParseResult parse(std::string_view source) {
    const ahfl::Frontend frontend;
    return frontend.parse_text("capacity_refinement.ahfl", std::string(source));
}

// Find fn `name` and return the `collection_capacity` of its single parameter's
// NamedType. Fails the assertion if the shape is unexpected.
[[nodiscard]] std::optional<std::uint64_t> param_capacity(const ahfl::ast::Program &program,
                                                          std::string_view fn_name) {
    for (const auto &decl : program.declarations) {
        const auto *fn = std::get_if<ahfl::ast::FnDecl>(&decl);
        if (fn == nullptr || fn->name != fn_name) {
            continue;
        }
        REQUIRE(fn->params.size() == 1);
        const auto &param = fn->params.front();
        REQUIRE(param != nullptr);
        REQUIRE(param->type != nullptr);
        REQUIRE(param->type->is<ahfl::ast::NamedType>());
        return param->type->as<ahfl::ast::NamedType>().collection_capacity;
    }
    FAIL("fn not found");
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// RFC 0013 P4: `List<T> where length <= N` refinement sugar. It is a pure
// front-end normalization onto the same NamedType::collection_capacity field
// the nominal `List<T>(N)` form (RFC 0025) populates, so both spellings produce
// an identical AST and all downstream typecheck / SMV machinery is shared.
// ---------------------------------------------------------------------------

TEST_CASE("where-length refinement lowers to the same capacity as the (N) suffix") {
    const auto paren = parse(
        "fn f(xs: List<Int>(16)) -> Int effect Pure decreases 0 { return 0; }");
    REQUIRE_FALSE(paren.has_errors());
    REQUIRE(paren.program != nullptr);

    const auto where_form = parse(
        "fn f(xs: List<Int> where length <= 16) -> Int effect Pure decreases 0 { return 0; }");
    REQUIRE_FALSE(where_form.has_errors());
    REQUIRE(where_form.program != nullptr);

    const auto paren_cap = param_capacity(*paren.program, "f");
    const auto where_cap = param_capacity(*where_form.program, "f");
    REQUIRE(paren_cap.has_value());
    REQUIRE(where_cap.has_value());
    CHECK(*paren_cap == 16);
    CHECK(*where_cap == 16);
    CHECK(*paren_cap == *where_cap);
}

TEST_CASE("where-length refinement accepts zero and large capacities") {
    const auto zero = parse(
        "fn f(xs: List<Int> where length <= 0) -> Int effect Pure decreases 0 { return 0; }");
    REQUIRE_FALSE(zero.has_errors());
    CHECK(param_capacity(*zero.program, "f") == std::optional<std::uint64_t>{0});
}

TEST_CASE("an unsupported refinement measure is rejected with INVALID_CAPACITY_REFINEMENT") {
    const auto bad = parse(
        "fn f(xs: List<Int> where size <= 16) -> Int effect Pure decreases 0 { return 0; }");
    REQUIRE(bad.has_errors());
    bool found = false;
    for (const auto &entry : bad.diagnostics.entries()) {
        if (entry.code.value_or("").find("INVALID_CAPACITY_REFINEMENT") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("a plain unbounded collection type has no capacity") {
    const auto plain = parse(
        "fn f(xs: List<Int>) -> Int effect Pure decreases 0 { return 0; }");
    REQUIRE_FALSE(plain.has_errors());
    CHECK_FALSE(param_capacity(*plain.program, "f").has_value());
}
