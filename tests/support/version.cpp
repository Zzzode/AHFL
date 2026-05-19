#include "ahfl/support/version.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const std::string &test_name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << test_name << "\n";
    }
}

void test_parse_basic() {
    auto v = ahfl::support::parse_version("1.2.3");
    check(v.major == 1, "parse.major");
    check(v.minor == 2, "parse.minor");
    check(v.patch == 3, "parse.patch");
    check(v.prerelease.empty(), "parse.no_prerelease");
}

void test_parse_prerelease() {
    auto v = ahfl::support::parse_version("2.0.0-beta.1");
    check(v.major == 2, "prerelease.major");
    check(v.prerelease == "beta.1", "prerelease.value");
}

void test_compatibility() {
    auto v1 = ahfl::support::parse_version("1.2.0");
    auto v2 = ahfl::support::parse_version("1.3.0");
    auto v3 = ahfl::support::parse_version("2.0.0");
    check(v1.is_compatible_with(v2), "compat.same_major_higher_minor");
    check(!v1.is_compatible_with(v3), "compat.different_major");
}

void test_to_string() {
    auto v = ahfl::support::parse_version("1.2.3");
    check(v.to_string() == "1.2.3", "to_string.basic");
}

} // anonymous namespace

int main() {
    test_parse_basic();
    test_parse_prerelease();
    test_compatibility();
    test_to_string();
    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
