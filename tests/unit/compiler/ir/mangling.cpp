#include <doctest.h>

#include "ahfl/compiler/ir/mangling.hpp"
#include "ahfl/compiler/semantics/type_context.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using namespace ahfl;
using namespace ahfl::mangle;

[[nodiscard]] bool is_ascii_identifier_safe(std::string_view s) noexcept {
    for (const char c : s) {
        const unsigned char uc = static_cast<unsigned char>(c);
        const bool ok =
            (uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') ||
            (uc >= '0' && uc <= '9') || uc == '_';
        if (!ok) return false;
    }
    return true;
}

// Minimal mock resolver that returns canned canonical names for specific ids.
struct MockResolver {
    std::unordered_map<std::size_t, std::string> names;
    SymbolCanonicalNameFn fn() const {
        return [this](SymbolId id) -> std::optional<std::string> {
            const auto it = names.find(id.value);
            if (it == names.end()) return std::nullopt;
            return it->second;
        };
    }
};

// TypeContext-backed type factory. Produces fully-initialized Type objects
// whose `describe()` reports the correct textual form.
struct TypeArena {
    ahfl::TypeContext ctx;
    TypePtr make_int() { return ctx.make(ahfl::TypeKind::Int); }
    TypePtr make_string() { return ctx.string(); }
    TypePtr make_bool() { return ctx.make(ahfl::TypeKind::Bool); }
    TypePtr make_struct(std::string canonical) {
        return ctx.struct_type(std::move(canonical));
    }
    TypePtr make_list(TypePtr element) {
        return ctx.struct_type("List", std::nullopt, std::vector<TypePtr>{element});
    }
};

} // namespace

TEST_CASE("mangle_escape preserves alphanumerics and converts namespace separator") {
    // Alphanumerics are verbatim.
    CHECK(mangle::detail::mangle_escape("abc123XYZ") == "abc123XYZ");

    // `::` becomes a single `_`.
    CHECK(mangle::detail::mangle_escape("a::b::c") == "a_b_c");
    CHECK(mangle::detail::mangle_escape("::leading") == "_leading");
    CHECK(mangle::detail::mangle_escape("trailing::") == "trailing_");

    // Single `:` (not a namespace separator) is hex-escaped.
    CHECK(mangle::detail::mangle_escape("a:b") == "a_X3ab");

    // Spaces, punctuation and non-ASCII-safe bytes all become _Xhh.
    CHECK(mangle::detail::mangle_escape("hello world") == "hello_X20world");
    CHECK(mangle::detail::mangle_escape("a-b/c") == "a_X2db_X2fc");
}

TEST_CASE("mangle_escape is bijective over representative inputs") {
    const std::array<std::string_view, 10> inputs = {
        "a::b::c",
        "a_b_c",
        "a:b",
        "a:::b", // two `::` plus one stray `:`.
        "hello-world/run",
        "MyStruct<Int>",
        "Map<String, List<Int>>",
        "with space\ttab",
        "",
        "PureASCII123",
    };
    std::vector<std::string> escaped;
    escaped.reserve(inputs.size());
    for (const auto in : inputs) escaped.push_back(mangle::detail::mangle_escape(in));

    // Distinct inputs must produce distinct outputs (injectivity).
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        for (std::size_t j = i + 1; j < inputs.size(); ++j) {
            if (inputs[i] == inputs[j]) continue;
            CHECK(escaped[i] != escaped[j]);
        }
    }
    // Every escaped output is ASCII identifier-safe.
    for (const auto &s : escaped) CHECK(is_ascii_identifier_safe(s));
}

TEST_CASE("mangle_instance always starts with _inst_ prefix") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(42u, "mod::MyCap");
    const auto name = mangle_instance(
        SymbolId{42u}, std::span<const TypePtr>{}, r.fn());
    CHECK(name.substr(0, 6) == "_inst_");
}

TEST_CASE("mangle_instance embeds zero-padded SymbolId as 16-digit hex") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(1u, "a::Cap");
    r.names.emplace(256u, "a::Cap"); // same canonical name as id 1.
    const auto n1 = mangle_instance(
        SymbolId{1u}, std::span<const TypePtr>{}, r.fn());
    const auto n256 = mangle_instance(
        SymbolId{256u}, std::span<const TypePtr>{}, r.fn());
    // 0x01 = 0000000000000001
    CHECK(n1.find("_inst_0000000000000001_") == 0);
    // 0x100 = 0000000000000100
    CHECK(n256.find("_inst_0000000000000100_") == 0);
    // Distinct SymbolIds always differ even when canonical names collide.
    CHECK(n1 != n256);
}

TEST_CASE("mangle_instance embeds escaped canonical name with namespace preserved") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(7u, "alpha::beta::Gamma");
    const auto name = mangle_instance(
        SymbolId{7u}, std::span<const TypePtr>{}, r.fn());
    // "alpha::beta::Gamma" -> "alpha_beta_Gamma" via mangle_escape.
    CHECK(name.find("alpha_beta_Gamma") != std::string::npos);
}

TEST_CASE("mangle_instance differentiates on type_args") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(11u, "m::C");
    const auto ti = types.make_int();
    const auto ts = types.make_string();
    const auto tb = types.make_bool();

    const auto base = mangle_instance(
        SymbolId{11u}, std::span<const TypePtr>{}, r.fn());
    const auto one_int = mangle_instance(
        SymbolId{11u}, std::span<const TypePtr>{&ti, 1}, r.fn());
    const auto two_int = mangle_instance(
        SymbolId{11u}, std::span<const TypePtr>{&ti, 1}, r.fn());
    const auto one_str = mangle_instance(
        SymbolId{11u}, std::span<const TypePtr>{&ts, 1}, r.fn());
    const auto three = mangle_instance(
        SymbolId{11u}, std::span<const TypePtr>{std::array<const TypePtr, 3>{ti, ts, tb}},
        r.fn());

    // Same inputs => identical output (determinism / stability).
    CHECK(one_int == two_int);

    // Varying type_args => distinct outputs (injectivity over key).
    CHECK(base != one_int);
    CHECK(one_int != one_str);
    CHECK(one_str != three);
}

TEST_CASE("mangle_instance embeds rich type descriptors via describe") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(23u, "m::C");
    const auto ts = types.make_string();
    const auto list_of_string = types.make_list(ts);
    const auto req_struct = types.make_struct("pkg::Request");

    const auto name = mangle_instance(
        SymbolId{23u},
        std::span<const TypePtr>{std::array<const TypePtr, 2>{list_of_string, req_struct}},
        r.fn());
    // describe() for ListT(StringT) returns "List<String>" and for StructT
    // returns its canonical_name "pkg::Request". `::` becomes `_`, `<` becomes
    // _X3c, `>` becomes _X3e, all other non-alphanumerics hex-escaped.
    CHECK(name.find("List_X3cString_X3e") != std::string::npos);
    CHECK(name.find("pkg_Request") != std::string::npos);
}

TEST_CASE("mangle_instance output is purely ASCII-identifier-safe") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(99u, "weird::name with spaces::MyCäp");
    const auto weird = types.make_struct("ns::T<Int, String>");
    const auto n = mangle_instance(
        SymbolId{99u}, std::span<const TypePtr>{&weird, 1}, r.fn());
    CHECK(is_ascii_identifier_safe(n));
    // No `::` leaks through.
    CHECK(n.find("::") == std::string::npos);
}

TEST_CASE("mangle_instance uses _UNKNOWN_SYMBOL_ sentinel for missing resolver entries") {
    TypeArena types;
    MockResolver r; // empty — SymbolId 1 maps to nothing.
    const auto unknown = mangle_instance(
        SymbolId{1u}, std::span<const TypePtr>{}, r.fn());
    CHECK(unknown.find("_UNKNOWN_SYMBOL_") != std::string::npos);
    CHECK(is_ascii_identifier_safe(unknown));

    // A no-op resolver also produces the sentinel.
    const auto no_resolver = mangle_instance(
        SymbolId{1u}, std::span<const TypePtr>{}, SymbolCanonicalNameFn{});
    CHECK(no_resolver.find("_UNKNOWN_SYMBOL_") != std::string::npos);
}

TEST_CASE("mangle_instance: full key bijection over cross product") {
    TypeArena types;
    MockResolver r;
    r.names.emplace(1u, "a::Cap");
    r.names.emplace(2u, "a::Cap");
    r.names.emplace(3u, "b::Cap");
    const auto t0 = types.make_int();
    const auto t1 = types.make_string();
    const auto t2 = types.make_bool();

    // Build every combination of (SymbolId in {1,2,3}) x
    // (type_args in {[], [t0], [t0,t1], [t0,t1,t2]}). Expect all 12 distinct.
    std::vector<std::size_t> ids = {1, 2, 3};
    std::vector<std::vector<TypePtr>> arg_sets = {
        {}, {t0}, {t0, t1}, {t0, t1, t2}
    };
    std::vector<std::string> outputs;
    outputs.reserve(ids.size() * arg_sets.size());
    for (const auto id : ids) {
        for (const auto &args : arg_sets) {
            outputs.push_back(mangle_instance(
                SymbolId{id}, std::span<const TypePtr>{args.data(), args.size()},
                r.fn()));
        }
    }
    // Pairwise uniqueness.
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        for (std::size_t j = i + 1; j < outputs.size(); ++j) {
            CHECK(outputs[i] != outputs[j]);
        }
    }
    // All ASCII identifier-safe.
    for (const auto &s : outputs) CHECK(is_ascii_identifier_safe(s));
}
