#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "tooling/formatter/formatter.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::ast::NodeKind;

[[nodiscard]] ahfl::ParseResult parse_only(std::string_view filename,
                                           const std::string &source) {
    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_text(std::string(filename), source);
    if (parse_result.has_errors()) {
        MESSAGE("parse errors detected");
        for (const auto &entry : parse_result.diagnostics.entries()) {
            MESSAGE("  parse: " << entry.message);
        }
    }
    REQUIRE_FALSE(parse_result.has_errors());
    REQUIRE(parse_result.program != nullptr);
    return parse_result;
}

[[nodiscard]] const ahfl::ast::EnumDecl *
find_enum_decl(const ahfl::ast::Program &program, std::string_view name) {
    for (const auto &decl : program.declarations) {
        if (decl->kind != NodeKind::EnumDecl) continue;
        const auto *e = static_cast<const ahfl::ast::EnumDecl *>(decl.get());
        if (e->name == name) return e;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Wave-19 Lane 3b F1 — RFC d-1 Enum variant named fields (struct variant):
//   parse → AST → formatter → surface text, with exact-string equality.
//
// NOTE: This POC intentionally stops at the parser/AST/printer boundary —
// no resolver, typechecker, TypedHIR or IR work is performed. Downstream
// passes only see a new named_fields vector, which is (by design) empty
// for every non-struct variant so existing codepaths remain untouched.
// ---------------------------------------------------------------------------

TEST_CASE("Enum struct variants: 1-field / 2-field / 3-field with default (roundtrip)") {
    // Canonical surface form matches formatter output exactly: 4-space indent,
    // trailing comma after the last variant. Parse → AST → formatter must
    // reproduce the source verbatim.
    const std::string source = R"AHFL(enum Geometry {
    Origin,
    Point(x: Int, y: Int),
    Single(name: String),
    Box(width: Int, height: Int, label: String = "box"),
}
)AHFL";

    // 1. Parse must succeed without errors.
    const auto parse_result = parse_only("enum_struct_variant_rt.ahfl", source);
    const auto *program = parse_result.program.get();

    const auto *geometry = find_enum_decl(*program, "Geometry");
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->variants.size() == 4);

    // 2. Unit variant — payload and named_fields must both be empty.
    {
        const auto &v = geometry->variants[0];
        CHECK(v->name == "Origin");
        CHECK(v->payload.empty());
        CHECK(v->named_fields.empty());
    }

    // 3. 2-field struct variant — Point(x: Int, y: Int).
    {
        const auto &v = geometry->variants[1];
        CHECK(v->name == "Point");
        CHECK(v->payload.empty());
        REQUIRE(v->named_fields.size() == 2);
        CHECK(v->named_fields[0]->name == "x");
        REQUIRE(v->named_fields[0]->type != nullptr);
        CHECK(v->named_fields[0]->type->spelling() == "Int");
        CHECK(v->named_fields[0]->default_value == nullptr);
        CHECK(v->named_fields[1]->name == "y");
        REQUIRE(v->named_fields[1]->type != nullptr);
        CHECK(v->named_fields[1]->type->spelling() == "Int");
        CHECK(v->named_fields[1]->default_value == nullptr);
    }

    // 4. 1-field struct variant — Single(name: String).
    {
        const auto &v = geometry->variants[2];
        CHECK(v->name == "Single");
        CHECK(v->payload.empty());
        REQUIRE(v->named_fields.size() == 1);
        CHECK(v->named_fields[0]->name == "name");
        REQUIRE(v->named_fields[0]->type != nullptr);
        CHECK(v->named_fields[0]->type->spelling() == "String");
        CHECK(v->named_fields[0]->default_value == nullptr);
    }

    // 5. 3-field struct variant with default initialiser.
    {
        const auto &v = geometry->variants[3];
        CHECK(v->name == "Box");
        CHECK(v->payload.empty());
        REQUIRE(v->named_fields.size() == 3);
        CHECK(v->named_fields[0]->name == "width");
        CHECK(v->named_fields[0]->type->spelling() == "Int");
        CHECK(v->named_fields[0]->default_value == nullptr);
        CHECK(v->named_fields[1]->name == "height");
        CHECK(v->named_fields[1]->type->spelling() == "Int");
        CHECK(v->named_fields[1]->default_value == nullptr);
        CHECK(v->named_fields[2]->name == "label");
        CHECK(v->named_fields[2]->type->spelling() == "String");
        REQUIRE(v->named_fields[2]->default_value != nullptr);
        CHECK(v->named_fields[2]->default_value->text == R"("box")");
    }

    // 6. Formatter round-trip: format(original) must equal original text
    //    (trailing newline handled by format_source by default).
    const auto fmt = ahfl::formatter::format_source(source);
    CHECK(fmt.success);
    CHECK(fmt.formatted == source);

    // 7. AST-printer outline must mention the named-field variants
    //    (basic coverage signal; exact printer strings are not pinned
    //     because outline format is a debug surface only).
    {
        std::ostringstream oss;
        ahfl::dump_program_outline(*program, oss);
        const std::string outline = oss.str();
        CHECK(outline.find("variant Origin") != std::string::npos);
        CHECK(outline.find("variant Point") != std::string::npos);
        CHECK(outline.find("x: Int") != std::string::npos);
        CHECK(outline.find("y: Int") != std::string::npos);
        CHECK(outline.find("variant Single") != std::string::npos);
        CHECK(outline.find("name: String") != std::string::npos);
        CHECK(outline.find("variant Box") != std::string::npos);
        CHECK(outline.find("label: String") != std::string::npos);
        CHECK(outline.find("= \"box\"") != std::string::npos);
    }
}

TEST_CASE("Enum struct variants coexist with positional (tuple) variants") {
    // Ensures the grammar labelled alternatives disambiguate correctly:
    //   * `Value(Int, Int)`    — tuple payload, no `:` after any IDENT
    //   * `Named(a: Int, b: Int)` — struct payload
    //   * `Empty`              — unit variant
    const std::string source = R"AHFL(enum Mixed {
    Empty,
    Tuple(Int, String),
    Named(a: Int, b: String),
}
)AHFL";

    const auto parse_result = parse_only("enum_mixed_variants.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *mixed = find_enum_decl(*program, "Mixed");
    REQUIRE(mixed != nullptr);
    REQUIRE(mixed->variants.size() == 3);

    CHECK(mixed->variants[0]->payload.empty());
    CHECK(mixed->variants[0]->named_fields.empty());

    REQUIRE(mixed->variants[1]->payload.size() == 2);
    CHECK(mixed->variants[1]->named_fields.empty());
    CHECK(mixed->variants[1]->payload[0]->spelling() == "Int");
    CHECK(mixed->variants[1]->payload[1]->spelling() == "String");

    CHECK(mixed->variants[2]->payload.empty());
    REQUIRE(mixed->variants[2]->named_fields.size() == 2);
    CHECK(mixed->variants[2]->named_fields[0]->name == "a");
    CHECK(mixed->variants[2]->named_fields[0]->type->spelling() == "Int");
    CHECK(mixed->variants[2]->named_fields[1]->name == "b");
    CHECK(mixed->variants[2]->named_fields[1]->type->spelling() == "String");

    const auto fmt = ahfl::formatter::format_source(source);
    CHECK(fmt.success);
    CHECK(fmt.formatted == source);
}
