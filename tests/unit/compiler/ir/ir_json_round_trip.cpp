#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/lowering.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path golden_ir_dir() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR} / "tests" / "golden" / "ir";
#else
    return std::filesystem::path{"tests"} / "golden" / "ir";
#endif
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Every committed IR JSON golden that is `print_program_ir_json` output
// (format_version "ahfl.ir.v2"). KR5.9 requires parse -> print to reproduce
// each of these byte-for-byte.
//
// The `*.opt-ir.json` / `*.opt-ir.optimized.json` goldens are intentionally
// EXCLUDED: they are a different serialization (`"format":
// "AHFL_OPT_IR_V1"`) produced by the opt-IR pipeline's own emitter, not by
// IrJsonPrinter / print_program_ir_json, so parse_program_ir_json does not
// (and should not) accept them.
constexpr std::string_view kGoldenJsonFiles[] = {
    "ok_expr_temporal.json",
    "ok_workflow_value_flow.json",
};

} // namespace

TEST_CASE("IR JSON round-trips byte-identically for every golden") {
    const auto dir = golden_ir_dir();
    for (const auto &name : kGoldenJsonFiles) {
        CAPTURE(name);
        const auto path = dir / std::string(name);
        REQUIRE(std::filesystem::exists(path));

        const std::string original = read_file(path);
        REQUIRE_FALSE(original.empty());

        const auto program = ahfl::parse_program_ir_json(original);
        REQUIRE(program.has_value());

        std::ostringstream out;
        ahfl::print_program_ir_json(*program, out);
        const std::string reemitted = out.str();

        // Byte-identical round-trip: parse(original) -> print == original.
        CHECK(reemitted == original);
    }
}

TEST_CASE("IR JSON deserializer rejects malformed input") {
    CHECK_FALSE(ahfl::parse_program_ir_json("not json").has_value());
    CHECK_FALSE(ahfl::parse_program_ir_json("{}").has_value()); // missing declarations
    CHECK_FALSE(ahfl::parse_program_ir_json("[]").has_value()); // not an object
}

namespace {

// A deliberately field-complete TypeRef: a bounded generic collection
// (`collection_capacity` + `nominal_ref` + `params`) whose element is itself a
// nominal struct carrying its own `nominal_ref`, plus a null param slot and an
// Fn return in `first`. Exercises every field `clone_type_ref` /
// `type_refs_equal` must carry, at depth and across a null child.
[[nodiscard]] ahfl::ir::TypeRef make_full_type_ref() {
    using namespace ahfl::ir;
    TypeRef inner;
    inner.kind = TypeRefKind::Struct;
    inner.display_name = "User";
    inner.canonical_name = "app::User";
    inner.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                  .canonical_name = "app::User",
                                  .local_name = "User",
                                  .module_name = "app",
                                  .id = std::size_t{7}};

    TypeRef root;
    root.kind = TypeRefKind::Struct;
    root.display_name = "List<User>(4)";
    root.canonical_name = "std::collections::List";
    root.collection_capacity = std::uint64_t{4};
    root.nominal_ref = SymbolRef{.kind = SymbolRefKind::Type,
                                 .canonical_name = "std::collections::List",
                                 .local_name = "List",
                                 .module_name = "std::collections",
                                 .id = std::size_t{3}};
    root.first = ahfl::make_owned<TypeRef>(TypeRef{.kind = TypeRefKind::Bool,
                                                   .display_name = "Bool"});
    root.params.push_back(ahfl::make_owned<TypeRef>(std::move(inner)));
    root.params.push_back(nullptr); // null param slot must survive clone/equality
    return root;
}

} // namespace

TEST_CASE("clone_type_ref preserves every field including nominal_ref, capacity, nested + null") {
    const auto original = make_full_type_ref();
    const auto cloned = ahfl::ir::clone_type_ref(original);

    // A full deep clone is structurally equal to its source.
    CHECK(ahfl::ir::type_refs_equal(original, cloned));

    // Spot-check the fields the divergent clones used to drop.
    CHECK(cloned.collection_capacity == original.collection_capacity);
    CHECK(cloned.nominal_ref.kind == ahfl::ir::SymbolRefKind::Type);
    CHECK(cloned.nominal_ref.canonical_name == "std::collections::List");
    CHECK(cloned.nominal_ref.id == std::size_t{3});
    REQUIRE(cloned.params.size() == 2);
    REQUIRE(cloned.params[0] != nullptr);
    CHECK(cloned.params[1] == nullptr); // null slot preserved
    CHECK(cloned.params[0]->nominal_ref.canonical_name == "app::User");
    REQUIRE(cloned.first != nullptr);
    CHECK(cloned.first->kind == ahfl::ir::TypeRefKind::Bool);

    // Deep clone, not a shallow alias.
    CHECK(cloned.params[0].get() != original.params[0].get());
    CHECK(cloned.first.get() != original.first.get());
}

TEST_CASE("type_refs_equal uses id-first nominal identity") {
    const auto base = make_full_type_ref();

    SUBCASE("same canonical, DIFFERENT resolved id -> not equal (P0 identity)") {
        // The core probe: identical spelling, different resolved declaration.
        auto other = ahfl::ir::clone_type_ref(base);
        other.nominal_ref.id = std::size_t{4}; // base has id 3, canonical unchanged
        CHECK(other.nominal_ref.canonical_name == base.nominal_ref.canonical_name);
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("same id, differing local/module display -> identity equal") {
        // Identity is the id; incidental display names do not split it.
        auto other = ahfl::ir::clone_type_ref(base);
        other.nominal_ref.local_name = "ListAlias";
        other.nominal_ref.module_name = "std::alias";
        CHECK(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("name-only refs: same canonical -> equal, different -> not equal") {
        auto a = ahfl::ir::clone_type_ref(base);
        auto b = ahfl::ir::clone_type_ref(base);
        a.nominal_ref.id.reset();
        b.nominal_ref.id.reset();
        CHECK(ahfl::ir::type_refs_equal(a, b));
        b.nominal_ref.canonical_name = "std::collections::Set";
        CHECK_FALSE(ahfl::ir::type_refs_equal(a, b));
    }
    SUBCASE("nested param: same canonical different id -> not equal") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.params[0]->nominal_ref.id = std::size_t{42}; // inner base id is 7
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
}

TEST_CASE("type_refs_equal distinguishes on nominal kind, capacity, and nested params") {
    const auto base = make_full_type_ref();

    SUBCASE("nominal_ref kind difference is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        // base's nominal_ref kind is Type; a differing kind is a differing
        // identity even at the same id/canonical.
        other.nominal_ref.kind = ahfl::ir::SymbolRefKind::Const;
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("collection_capacity difference is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.collection_capacity = std::uint64_t{8};
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("null vs non-null param slot is observed") {
        auto other = ahfl::ir::clone_type_ref(base);
        other.params[1] = ahfl::make_owned<ahfl::ir::TypeRef>(
            ahfl::ir::TypeRef{.kind = ahfl::ir::TypeRefKind::Int});
        CHECK_FALSE(ahfl::ir::type_refs_equal(base, other));
    }
    SUBCASE("identical rebuild compares equal") {
        CHECK(ahfl::ir::type_refs_equal(base, make_full_type_ref()));
    }
}
