#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <string>
#include <string_view>

namespace {

[[nodiscard]] ahfl::ParseResult parse(std::string_view source) {
    const ahfl::Frontend frontend;
    return frontend.parse_text("inner_attr.ahfl", std::string(source));
}

} // namespace

// ---------------------------------------------------------------------------
// RFC 0013 P6: `#![no_prelude]` inner attribute. It sets Program::suppress_prelude
// so the project loader can skip implicit prelude injection for the source unit.
// ---------------------------------------------------------------------------

TEST_CASE("#![no_prelude] sets Program::suppress_prelude") {
    const auto result = parse("#![no_prelude]\nmodule app::main;\n");
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.program != nullptr);
    CHECK(result.program->suppress_prelude);
}

TEST_CASE("a file without an inner attribute leaves suppress_prelude false") {
    const auto result = parse("module app::main;\n");
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.program != nullptr);
    CHECK_FALSE(result.program->suppress_prelude);
}

TEST_CASE("an unknown inner attribute is rejected with UNKNOWN_INNER_ATTRIBUTE") {
    const auto result = parse("#![no_such]\nmodule app::main;\n");
    REQUIRE(result.has_errors());
    bool found = false;
    for (const auto &entry : result.diagnostics.entries()) {
        if (entry.code.value_or("").find("UNKNOWN_INNER_ATTRIBUTE") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}
