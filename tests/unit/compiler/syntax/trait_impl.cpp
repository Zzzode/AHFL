#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/ast.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ahfl::ast::ImplItemKind;
using ahfl::ast::NodeKind;
using ahfl::ast::TraitItemKind;

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

[[nodiscard]] const ahfl::ast::TraitDecl *
find_trait_decl(const ahfl::ast::Program &program, std::string_view name) {
    for (const auto &decl : program.declarations) {
        if (decl->kind != NodeKind::TraitDecl) {
            continue;
        }
        const auto *trait = static_cast<const ahfl::ast::TraitDecl *>(decl.get());
        if (trait->name == name) {
            return trait;
        }
    }
    return nullptr;
}

[[nodiscard]] const ahfl::ast::ImplDecl *
first_impl_decl(const ahfl::ast::Program &program) {
    for (const auto &decl : program.declarations) {
        if (decl->kind == NodeKind::ImplDecl) {
            return static_cast<const ahfl::ast::ImplDecl *>(decl.get());
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Acceptance signal #1: trait with associated constant.
// ---------------------------------------------------------------------------

TEST_CASE("Trait with assoc const, fn, assoc type, super-traits list") {
    const std::string source = R"AHFL(
trait Foo<T>: Eq + Hash + Ord {
    fn bar(self, other: Self) -> Bool;
    type Item;
    const NAME: String = "foo";
}
)AHFL";

    const auto parse_result = parse_only("trait_with_const.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *trait = find_trait_decl(*program, "Foo");
    REQUIRE(trait != nullptr);
    CHECK(trait->name == "Foo");

    // Super-traits list (P3c gap #2).
    REQUIRE(trait->super_traits.size() == 3);
    CHECK(trait->super_traits[0]->spelling() == "Eq");
    CHECK(trait->super_traits[1]->spelling() == "Hash");
    CHECK(trait->super_traits[2]->spelling() == "Ord");

    // Three item kinds must be present.
    REQUIRE(trait->items.size() == 3);

    // TraitItemKind::Fn (1).
    CHECK(trait->items[0]->kind == TraitItemKind::Fn);
    CHECK(trait->items[0]->name == "bar");
    REQUIRE(trait->items[0]->params.size() == 2);
    // Self param: bare "self" (gap #4).
    const auto &param = trait->items[0]->params[0];
    CHECK(param->is_self == true);
    CHECK(param->is_self_mut == false);
    CHECK(param->name == "self");
    CHECK(param->type == nullptr);  // bare self has no explicit type.
    // Other param: regular typed param.
    const auto &other = trait->items[0]->params[1];
    CHECK(other->is_self == false);
    CHECK(other->name == "other");
    REQUIRE(other->type != nullptr);
    CHECK(other->type->spelling() == "Self");

    // TraitItemKind::AssocType (2).
    CHECK(trait->items[1]->kind == TraitItemKind::AssocType);
    REQUIRE(trait->items[1]->assoc_type != nullptr);
    CHECK(trait->items[1]->assoc_type->name == "Item");
    CHECK(trait->items[1]->assoc_type->default_type == nullptr);

    // TraitItemKind::AssocConst (3, new in P3c gap #1).
    CHECK(trait->items[2]->kind == TraitItemKind::AssocConst);
    REQUIRE(trait->items[2]->assoc_const != nullptr);
    CHECK(trait->items[2]->assoc_const->name == "NAME");
    REQUIRE(trait->items[2]->assoc_const->type != nullptr);
    CHECK(trait->items[2]->assoc_const->type->spelling() == "String");
    REQUIRE(trait->items[2]->assoc_const->default_value != nullptr);
}

// ---------------------------------------------------------------------------
// Self-param five-shape coverage.
// ---------------------------------------------------------------------------

TEST_CASE("Self param accepts bare self, mut self, self:Type, mut self:Type") {
    const std::string source = R"AHFL(
trait SelfShapes {
    fn a(self) -> Int;
    fn b(mut self) -> Int;
    fn c(self: Self) -> Int;
    fn d(mut self: Self) -> Int;
    fn e(other: Self) -> Int;
}
)AHFL";

    const auto parse_result = parse_only("self_shapes.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *trait = find_trait_decl(*program, "SelfShapes");
    REQUIRE(trait != nullptr);
    REQUIRE(trait->items.size() == 5);

    // a: bare self
    {
        const auto &p0 = trait->items[0]->params[0];
        CHECK(p0->is_self == true);
        CHECK(p0->is_self_mut == false);
        CHECK(p0->name == "self");
        CHECK(p0->type == nullptr);
    }
    // b: mut self
    {
        const auto &p0 = trait->items[1]->params[0];
        CHECK(p0->is_self == true);
        CHECK(p0->is_self_mut == true);
        CHECK(p0->name == "self");
        CHECK(p0->type == nullptr);
    }
    // c: self: Self
    {
        const auto &p0 = trait->items[2]->params[0];
        CHECK(p0->is_self == true);
        CHECK(p0->is_self_mut == false);
        CHECK(p0->name == "self");
        REQUIRE(p0->type != nullptr);
        CHECK(p0->type->spelling() == "Self");
    }
    // d: mut self: Self
    {
        const auto &p0 = trait->items[3]->params[0];
        CHECK(p0->is_self == true);
        CHECK(p0->is_self_mut == true);
        CHECK(p0->name == "self");
        REQUIRE(p0->type != nullptr);
        CHECK(p0->type->spelling() == "Self");
    }
    // e: other: Self (regular param, not self)
    {
        const auto &p0 = trait->items[4]->params[0];
        CHECK(p0->is_self == false);
        CHECK(p0->is_self_mut == false);
        CHECK(p0->name == "other");
        REQUIRE(p0->type != nullptr);
        CHECK(p0->type->spelling() == "Self");
    }
}

// ---------------------------------------------------------------------------
// Acceptance signal #2: impl with where clause (gap #3) +
// all three ImplItem kinds.
// ---------------------------------------------------------------------------

TEST_CASE("Impl with where clause, fn, assoc type, assoc const") {
    const std::string source = R"AHFL(
struct Wrapper<T> {
    value: T;
}

impl<T> Display for Wrapper<T> where T: Display + Eq {
    fn show(self: Self) -> String {
        return "";
    }
    type Repr = T;
    const TAG: Int = 42;
}
)AHFL";

    const auto parse_result = parse_only("impl_where.ahfl", source);
    const auto *program = parse_result.program.get();
    const auto *impl = first_impl_decl(*program);
    REQUIRE(impl != nullptr);

    // Impl has where clause.
    CHECK(impl->trait_ref != nullptr);
    CHECK(impl->trait_ref->spelling() == "Display");
    CHECK(impl->target_type->spelling() == "Wrapper<T>");
    REQUIRE(impl->where_clause != nullptr);
    REQUIRE(impl->where_clause->constraints.size() == 1);
    CHECK(impl->where_clause->constraints[0]->subject->spelling() == "T");
    REQUIRE(impl->where_clause->constraints[0]->bounds.size() == 2);
    CHECK(impl->where_clause->constraints[0]->bounds[0]->spelling() == "Display");
    CHECK(impl->where_clause->constraints[0]->bounds[1]->spelling() == "Eq");

    // Three impl methods: methods bucket + unified items vector.
    REQUIRE(impl->methods.size() == 1);
    REQUIRE(impl->assoc_items.size() == 1);
    REQUIRE(impl->const_items.size() == 1);
    REQUIRE(impl->items.size() == 3);

    // ImplItemKind::Fn (1).
    CHECK(impl->items[0]->kind == ImplItemKind::Fn);
    REQUIRE(impl->items[0]->fn_def != nullptr);
    CHECK(impl->items[0]->fn_def->name == "show");
    const auto &p0 = impl->items[0]->fn_def->params[0];
    CHECK(p0->is_self == true);
    REQUIRE(p0->type != nullptr);
    CHECK(p0->type->spelling() == "Self");

    // ImplItemKind::AssocType (2).
    CHECK(impl->items[1]->kind == ImplItemKind::AssocType);
    REQUIRE(impl->items[1]->assoc_type != nullptr);
    CHECK(impl->items[1]->assoc_type->name == "Repr");
    REQUIRE(impl->items[1]->assoc_type->type != nullptr);
    CHECK(impl->items[1]->assoc_type->type->spelling() == "T");
    // Per-kind bucket is in sync.
    CHECK(impl->assoc_items[0]->name == "Repr");
    CHECK(impl->assoc_items[0]->type->spelling() == "T");

    // ImplItemKind::AssocConst (3, P3c gap #1).
    CHECK(impl->items[2]->kind == ImplItemKind::AssocConst);
    REQUIRE(impl->items[2]->assoc_const != nullptr);
    CHECK(impl->items[2]->assoc_const->name == "TAG");
    REQUIRE(impl->items[2]->assoc_const->type != nullptr);
    CHECK(impl->items[2]->assoc_const->type->spelling() == "Int");
    REQUIRE(impl->items[2]->assoc_const->value != nullptr);
    // Per-kind bucket is in sync.
    CHECK(impl->const_items[0]->name == "TAG");
    CHECK(impl->const_items[0]->type->spelling() == "Int");
}
