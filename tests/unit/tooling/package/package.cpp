#include "tooling/package/registry.hpp"
#include "tooling/package/resolver.hpp"

#include <iostream>

namespace {

using namespace ahfl::package;

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char* name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

void test_parse_version_basic() {
    auto v = Resolver::parse_version("1.2.3");
    check(v.major == 1, "parse_version major");
    check(v.minor == 2, "parse_version minor");
    check(v.patch == 3, "parse_version patch");
    check(v.prerelease.empty(), "parse_version no prerelease");

    auto pre = Resolver::parse_version("2.0.0-alpha");
    check(pre.major == 2, "parse_version prerelease major");
    check(pre.minor == 0, "parse_version prerelease minor");
    check(pre.patch == 0, "parse_version prerelease patch");
    check(pre.prerelease == "alpha", "parse_version prerelease tag");

    auto formatted = Resolver::format_version(v);
    check(formatted == "1.2.3", "format_version roundtrip");
}

void test_satisfies_caret_constraint() {
    auto v120 = Resolver::parse_version("1.2.0");
    auto v130 = Resolver::parse_version("1.3.0");
    auto v190 = Resolver::parse_version("1.9.9");
    auto v200 = Resolver::parse_version("2.0.0");
    auto v110 = Resolver::parse_version("1.1.0");

    // ^1.2.0 means >=1.2.0 <2.0.0
    check(Resolver::satisfies(v120, "^1.2.0"), "caret exact match");
    check(Resolver::satisfies(v130, "^1.2.0"), "caret higher minor");
    check(Resolver::satisfies(v190, "^1.2.0"), "caret high minor+patch");
    check(!Resolver::satisfies(v200, "^1.2.0"), "caret rejects next major");
    check(!Resolver::satisfies(v110, "^1.2.0"), "caret rejects lower minor");

    // Tilde: ~1.2.0 means >=1.2.0 <1.3.0
    auto v125 = Resolver::parse_version("1.2.5");
    check(Resolver::satisfies(v120, "~1.2.0"), "tilde exact match");
    check(Resolver::satisfies(v125, "~1.2.0"), "tilde higher patch");
    check(!Resolver::satisfies(v130, "~1.2.0"), "tilde rejects next minor");
}

void test_resolve_simple_dependency_graph() {
    Resolver resolver;

    resolver.add_available("ahfl-stdlib", {
        Resolver::parse_version("1.0.0"),
        Resolver::parse_version("1.1.0"),
        Resolver::parse_version("2.0.0"),
    });

    resolver.add_available("ahfl-http", {
        Resolver::parse_version("0.5.0"),
        Resolver::parse_version("0.6.0"),
    });

    resolver.add_dependency(Dependency{"ahfl-stdlib", "^1.0.0"});
    resolver.add_dependency(Dependency{"ahfl-http", ">=0.5.0"});

    auto result = resolver.resolve();
    check(result.success, "resolve succeeds");
    check(result.resolved.size() == 2, "resolve returns 2 packages");

    // Should pick highest satisfying version
    if (result.resolved.size() >= 1) {
        check(result.resolved[0].name == "ahfl-stdlib", "resolve stdlib name");
        check(Resolver::format_version(result.resolved[0].version) == "1.1.0",
              "resolve picks highest stdlib");
    }
    if (result.resolved.size() >= 2) {
        check(result.resolved[1].name == "ahfl-http", "resolve http name");
        check(Resolver::format_version(result.resolved[1].version) == "0.6.0",
              "resolve picks highest http");
    }
}

void test_resolve_detects_conflict() {
    Resolver resolver;

    resolver.add_available("ahfl-stdlib", {
        Resolver::parse_version("1.0.0"),
        Resolver::parse_version("1.1.0"),
        Resolver::parse_version("2.0.0"),
    });

    // Two constraints that resolve to different versions
    resolver.add_dependency(Dependency{"ahfl-stdlib", "^1.0.0"});  // -> 1.1.0
    resolver.add_dependency(Dependency{"ahfl-stdlib", "^2.0.0"});  // -> 2.0.0

    auto result = resolver.resolve();
    check(!result.success, "conflict detected");
    check(result.error == ResolveError::VersionConflict, "conflict error type");
    check(!result.error_message.empty(), "conflict has error message");
}

} // namespace

int main() {
    test_parse_version_basic();
    test_satisfies_caret_constraint();
    test_resolve_simple_dependency_graph();
    test_resolve_detects_conflict();

    std::cerr << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? 0 : 1;
}
