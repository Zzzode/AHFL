#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct NamedModuleSource {
    std::string module_name;
    std::string source;
};

void write_file(const std::filesystem::path &path, const std::string &content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

[[nodiscard]] std::filesystem::path module_source_path(const std::filesystem::path &root,
                                                       std::string_view module_name) {
    auto path = root;
    std::string module{module_name};
    std::size_t start = 0;
    while (true) {
        const auto separator = module.find("::", start);
        const auto segment = module.substr(start, separator - start);
        if (separator == std::string::npos) {
            path /= segment + ".ahfl";
            return path;
        }
        path /= segment;
        start = separator + 2;
    }
}

// Minimal agent+flow boilerplate appended to the APP entry module so the
// typechecker walks every top-level fn (uncalled fns would otherwise be
// skipped entirely by the pass manager). Identifiers avoid leading
// underscores (rejected by the AHFL lexer for user-facing names).
constexpr std::string_view kAppTail = R"AHFL(
struct DummyIn { tick: Int = 0; }
struct DummyOut { done: Bool = true; }
agent DummyAgent {
    input: DummyIn; context: DummyIn; output: DummyOut;
    states: [S]; initial: S; final: [S]; capabilities: [];
}
flow for DummyAgent {
    state S { return DummyOut { done: true }; }
}
)AHFL";

[[nodiscard]] ahfl::TypeCheckResult
typecheck_multi_module(std::string_view project_tag,
                       std::initializer_list<NamedModuleSource> modules) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("ahfl_type_mismatch_origin_" + std::string{project_tag});
    std::filesystem::remove_all(root);

    std::optional<std::filesystem::path> entry_path;
    std::size_t idx = 0;
    for (const auto &m : modules) {
        const auto path = module_source_path(root, m.module_name);
        const auto content = idx == 0 ? m.source + std::string{kAppTail} : m.source;
        write_file(path, content);
        if (idx == 0) {
            entry_path = path;
        }
        ++idx;
    }
    REQUIRE(entry_path.has_value());

    const ahfl::Frontend frontend;
    auto dump_first = [&]() -> std::string {
        std::ifstream f(*entry_path);
        if (!f) return {};
        return std::string{(std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>()};
    };
    const auto parse_result = frontend.parse_project(ahfl::ProjectInput{
        .entry_files = {*entry_path},
        .search_roots = {root, std::filesystem::path{"std"}},
        .inject_prelude = true,
    });
    if (parse_result.has_errors()) {
        std::ostringstream ss;
        parse_result.diagnostics.render(ss);
        std::cerr << "[PARSE-ERR-" << project_tag << "]\n"
                  << ss.str()
                  << "[FIRST MODULE]\n"
                  << dump_first() << "\n[END FIRST MODULE]\n";
    }
    REQUIRE_FALSE(parse_result.has_errors());

    const ahfl::Resolver resolver;
    const auto resolve_result = resolver.resolve(parse_result.graph);
    if (resolve_result.has_errors()) {
        std::ostringstream ss;
        resolve_result.diagnostics.render(ss);
        std::cerr << "[RESOLVE-ERR-" << project_tag << "]\n" << ss.str();
    }
    REQUIRE_FALSE(resolve_result.has_errors());

    const ahfl::TypeChecker type_checker;
    auto type_result = type_checker.check(parse_result.graph, resolve_result);
    auto merged = resolve_result.diagnostics;
    merged.append(type_result.diagnostics);
    type_result.diagnostics = merged;
    return type_result;
}

[[nodiscard]] bool diagnostic_contains(const ahfl::Diagnostic &d, std::string_view needle) {
    if (d.message.find(needle) != std::string::npos) {
        return true;
    }
    for (const auto &r : d.related) {
        if (r.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::size_t count_related_with(const ahfl::Diagnostic &d,
                                             std::string_view needle) {
    std::size_t n = 0;
    for (const auto &r : d.related) {
        if (r.message.find(needle) != std::string::npos) {
            ++n;
        }
    }
    return n;
}

[[nodiscard]] const ahfl::Diagnostic *
find_diagnostic_with_code(const ahfl::DiagnosticBag &bag, std::string_view code) {
    for (const auto &d : bag.entries()) {
        if (d.code.has_value() && *d.code == code) {
            return &d;
        }
    }
    return nullptr;
}

void dump_if_mismatch(const char *tag, const ahfl::DiagnosticBag &bag) {
    std::ostringstream ss;
    ss << "=== DIAG " << tag << " ===\n";
    for (const auto &d : bag.entries()) {
        ss << "code=" << (d.code ? *d.code : "<none>") << " | " << d.message << "\n";
        for (const auto &r : d.related) {
            ss << "  REL " << r.message << "\n";
        }
    }
    std::cerr << ss.str();
}

} // namespace

// ============================================================================
// N1: struct cross-module — both sides carry "declared here" notes
// ============================================================================
TEST_CASE("TypeMismatch nominal: struct A vs struct B across modules carries declared-here notes") {
    const auto result = typecheck_multi_module(
        "n1_struct_cross",
        {
            NamedModuleSource{"app::main",
                              R"AHFL(
module app::main;
import lib::definitions as defs;
fn consume(u: defs::User) -> Unit effect Pure decreases 0 { }
fn runTop() -> Unit effect Pure decreases 0 {
    let p: defs::Product = defs::Product { sku: "A1", price: 42 };
    let unit = consume(p);
}
)AHFL"},
            NamedModuleSource{"lib::definitions",
                              R"AHFL(
module lib::definitions;
import lib::definitions as self;
struct User { id: Int; name: String; }
struct Product { sku: String; price: Int; }
)AHFL"},
        }
    );
    dump_if_mismatch("n1", result.diagnostics);

    const auto *d = find_diagnostic_with_code(result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(d != nullptr);
    CHECK(diagnostic_contains(*d, "Product"));
    CHECK(diagnostic_contains(*d, "User"));
    CHECK(count_related_with(*d, "declared here in module 'lib::definitions'") == 2);
    CHECK(count_related_with(*d, "expected type 'lib::definitions::User' declared here") == 1);
    CHECK(count_related_with(*d, "actual type 'lib::definitions::Product' declared here") == 1);
}

// ============================================================================
// N2: enum cross-module — each enum anchors its own declared-here note.
// ============================================================================
TEST_CASE("TypeMismatch nominal: enum X vs enum Y across modules carries declared-here notes") {
    const auto result = typecheck_multi_module(
        "n2_enum_cross",
        {
            NamedModuleSource{"app::main",
                              R"AHFL(
module app::main;
import lib::colors as c;
import lib::shapes as s;
fn pick(col: c::Color) -> Unit effect Pure decreases 0 { }
fn runTop() -> Unit effect Pure decreases 0 {
    let shape: s::Shape = s::Shape::Circle;
    let unit = pick(shape);
}
)AHFL"},
            NamedModuleSource{"lib::colors",
                              R"AHFL(
module lib::colors;
import lib::colors as self;
enum Color {
    Red,
    Green,
    Blue,
}
)AHFL"},
            NamedModuleSource{"lib::shapes",
                              R"AHFL(
module lib::shapes;
import lib::shapes as self;
enum Shape {
    Circle,
    Square,
}
)AHFL"},
        }
    );
    dump_if_mismatch("n2", result.diagnostics);

    const auto *d = find_diagnostic_with_code(result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(d != nullptr);
    CHECK(diagnostic_contains(*d, "Color"));
    CHECK(diagnostic_contains(*d, "Shape"));
    CHECK(count_related_with(*d, "declared here in module 'lib::colors'") == 1);
    CHECK(count_related_with(*d, "declared here in module 'lib::shapes'") == 1);
    CHECK(count_related_with(*d, "expected type 'lib::colors::Color' declared here") == 1);
    CHECK(count_related_with(*d, "actual type 'lib::shapes::Shape' declared here") == 1);
}

// ============================================================================
// N3: same-module two structs — both declared-here notes anchor in that module
// ============================================================================
TEST_CASE("TypeMismatch nominal: two structs same-module carry declared-here notes") {
    const auto result = typecheck_multi_module(
        "n3_same_mod_structs",
        {
            NamedModuleSource{"app::main",
                              R"AHFL(
module app::main;
import lib::models as m;
fn show(p: m::Profile) -> Unit effect Pure decreases 0 { }
fn runTop() -> Unit effect Pure decreases 0 {
    let u: m::RawUser = m::RawUser { id: 1 };
    let unit = show(u);
}
)AHFL"},
            NamedModuleSource{"lib::models",
                              R"AHFL(
module lib::models;
import lib::models as self;
struct RawUser { id: Int; }
struct Profile { owner: RawUser; }
)AHFL"},
        }
    );
    dump_if_mismatch("n3", result.diagnostics);

    const auto *d = find_diagnostic_with_code(result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(d != nullptr);
    CHECK(count_related_with(*d, "declared here in module 'lib::models'") == 2);
    CHECK(count_related_with(*d, "expected type 'lib::models::Profile' declared here") == 1);
    CHECK(count_related_with(*d, "actual type 'lib::models::RawUser' declared here") == 1);
}

// ============================================================================
// A1: fn call — 1st argument mismatch carries argument #1 index + callable
// name + both-side nominal declared-here notes.
// ============================================================================
TEST_CASE("TypeMismatch per-argument: fn call argument #1 carries index and callable name") {
    const auto result = typecheck_multi_module(
        "a1_fn_arg1",
        {
            NamedModuleSource{"app::main",
                              R"AHFL(
module app::main;
import lib::api as api;
fn runTop() -> Unit effect Pure decreases 0 {
    let fake: api::Response = api::Response { status: 500 };
    let r = api::handle(fake, 1000);
}
)AHFL"},
            NamedModuleSource{"lib::api",
                              R"AHFL(
module lib::api;
import lib::api as self;
struct Request { body: String; }
struct Response { status: Int; }
fn handle(req: Request, timeoutMs: Int) -> Response effect Pure decreases 0 {
    return Response { status: 200 };
}
)AHFL"},
        }
    );
    dump_if_mismatch("a1", result.diagnostics);

    const auto *d = find_diagnostic_with_code(result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(d != nullptr);
    CHECK(count_related_with(*d, "argument #1") == 1);
    CHECK(count_related_with(*d, "declared in `handle`") == 1);
    CHECK(diagnostic_contains(*d, "Request"));
    CHECK(diagnostic_contains(*d, "Response"));
    CHECK(count_related_with(*d,
                             "expected type 'lib::api::Request' declared here in module") == 1);
    CHECK(count_related_with(*d,
                             "actual type 'lib::api::Response' declared here in module") == 1);
}

// ============================================================================
// A2: predicate call — 3rd argument mismatch carries argument #3 index +
// callable name + both-side nominal declared-here notes.
// ============================================================================
TEST_CASE("TypeMismatch per-argument: predicate call 3rd arg carries index and callable name") {
    const auto result = typecheck_multi_module(
        "a2_pred_arg3",
        {
            NamedModuleSource{"app::main",
                              R"AHFL(
module app::main;
import app::main as self;
struct Tag { label: String; }
struct Target { id: Int; }
struct Creds { token: String; }

predicate Valid(target: Target, reason: String, tag: Tag) -> Bool;

fn check() -> Bool effect Pure decreases 0 {
    let wrong: Creds = Creds { token: "x" };
    return Valid(Target { id: 7 }, "try", wrong);
}
)AHFL"},
        }
    );
    dump_if_mismatch("a2", result.diagnostics);

    const auto *d = find_diagnostic_with_code(result.diagnostics, "typecheck.TYPE_MISMATCH");
    REQUIRE(d != nullptr);
    CHECK(count_related_with(*d, "argument #3") == 1);
    CHECK(count_related_with(*d, "declared in `Valid`") == 1);
    CHECK(count_related_with(*d, "expected type 'app::main::Tag' declared here in module") == 1);
    CHECK(count_related_with(*d, "actual type 'app::main::Creds' declared here in module") == 1);
}

// ============================================================================
// N-neg: builtin types (Int vs Bool/String) — no declared-here notes for
// builtins because there is no user declaration site to anchor to.
// ============================================================================
TEST_CASE("TypeMismatch non-nominal: builtin Int vs Bool/String emits no declared-here notes") {
    const auto source = R"AHFL(
module app::main;
import app::main as self;
fn gate(b: Bool) -> Unit effect Pure decreases 0 { }
fn label(s: String) -> Unit effect Pure decreases 0 { }
fn runTop() -> Unit effect Pure decreases 0 {
    let n: Int = 42;
    let u1 = gate(n);
    let u2 = label(n);
}
)AHFL";
    const auto result = typecheck_multi_module(
        "nneg_builtins",
        {NamedModuleSource{"app::main", source}}
    );
    dump_if_mismatch("nneg", result.diagnostics);

    std::size_t mismatches = 0;
    for (const auto &entry : result.diagnostics.entries()) {
        if (!entry.code.has_value() || *entry.code != "typecheck.TYPE_MISMATCH") {
            continue;
        }
        ++mismatches;
        CHECK(count_related_with(entry, "declared here in module") == 0);
    }
    CHECK(mismatches == 2);
}
