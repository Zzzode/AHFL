#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "base/toml/toml.hpp"

#include <string_view>

namespace {

[[nodiscard]] std::string_view slice(std::string_view input, ahfl::SourceRange range) {
    return input.substr(range.begin_offset, range.end_offset - range.begin_offset);
}

} // namespace

TEST_CASE("TOML parser accepts package-shaped TOML with source ranges") {
    constexpr std::string_view input = R"TOML(manifest_version = 1

[package]
name = "refund-audit"
version = "0.1.0"
edition = "2026"
kind = "application"

[module]
prefix = "refund_audit"
root = "src"

[exports]
modules = ["main", "agents"]
)TOML";

    auto result = ahfl::toml::parse(input);
    REQUIRE_FALSE(result.has_errors());

    const auto *name_entry = result.document.find_entry({"package", "name"});
    REQUIRE(name_entry != nullptr);
    CHECK(slice(input, name_entry->key_range) == "name");
    CHECK(slice(input, name_entry->value_range) == R"("refund-audit")");
    REQUIRE(name_entry->value != nullptr);
    CHECK(name_entry->value->kind == ahfl::toml::ValueKind::String);
    CHECK(name_entry->value->string_value == "refund-audit");

    const auto *modules = result.document.find({"exports", "modules"});
    REQUIRE(modules != nullptr);
    REQUIRE(modules->kind == ahfl::toml::ValueKind::Array);
    REQUIRE(modules->array_items.size() == 2);
    CHECK(modules->array_items[0]->string_value == "main");
    CHECK(modules->array_items[1]->string_value == "agents");
}

TEST_CASE("TOML parser accepts RFC-required value forms") {
    constexpr std::string_view input = R"TOML(dec = 42
hex = 0x2A
bin = 0b101010
flt = 1.5e2
flag = true
when = 2026-07-02T10:11:12Z
literal = 'raw\path'
multi = """
line
"""
inline = { source = "path", path = "../core", version = "0.1.0" }
)TOML";

    auto result = ahfl::toml::parse(input);
    REQUIRE_FALSE(result.has_errors());

    REQUIRE(result.document.find({"hex"}) != nullptr);
    CHECK(result.document.find({"hex"})->integer_value == 42);
    REQUIRE(result.document.find({"bin"}) != nullptr);
    CHECK(result.document.find({"bin"})->integer_value == 42);
    REQUIRE(result.document.find({"flt"}) != nullptr);
    CHECK(result.document.find({"flt"})->kind == ahfl::toml::ValueKind::Float);
    REQUIRE(result.document.find({"when"}) != nullptr);
    CHECK(result.document.find({"when"})->kind == ahfl::toml::ValueKind::DateTime);
    REQUIRE(result.document.find({"inline"}) != nullptr);
    CHECK(result.document.find({"inline"})->kind == ahfl::toml::ValueKind::InlineTable);
}

TEST_CASE("TOML parser accepts array of tables") {
    constexpr std::string_view input = R"TOML([targets.workflow]
kind = "handoff"
entry = "app::main::Workflow"

[[targets.workflow.capability_bindings]]
capability = "OrderQuery"
binding_key = "order.query"
)TOML";

    auto result = ahfl::toml::parse(input);
    REQUIRE_FALSE(result.has_errors());

    const auto *bindings = result.document.find({"targets", "workflow", "capability_bindings"});
    REQUIRE(bindings != nullptr);
    REQUIRE(bindings->kind == ahfl::toml::ValueKind::Array);
    REQUIRE(bindings->array_items.size() == 1);
    CHECK(bindings->array_items.front()->kind == ahfl::toml::ValueKind::Table);
    REQUIRE(bindings->array_items.front()->table_fields.size() == 2);
}

TEST_CASE("TOML parser rejects duplicate keys with key range") {
    constexpr std::string_view input = R"TOML([package]
name = "a"
name = "b"
)TOML";

    auto result = ahfl::toml::parse(input);
    REQUIRE(result.has_errors());
    CHECK(result.document.diagnostics.front().code == "toml.duplicate_key");
    CHECK(slice(input, result.document.diagnostics.front().range) == "name");
}

TEST_CASE("TOML parser rejects invalid escapes as syntax diagnostics") {
    constexpr std::string_view input = R"TOML(name = "bad\q"
)TOML";

    auto result = ahfl::toml::parse(input);
    REQUIRE(result.has_errors());
    CHECK(result.document.diagnostics.front().code == "toml.invalid_escape");
}
