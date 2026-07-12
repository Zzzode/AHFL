#include <tooling/abi/compat_check.hpp>
#include <tooling/abi/version_info.hpp>

#include <cstdio>

static int test_count = 0;
static int pass_count = 0;

static void check(bool condition, const char* name) {
    ++test_count;
    if (condition) { ++pass_count; std::printf("  PASS: %s\n", name); }
    else { std::printf("  FAIL: %s\n", name); }
}

int main() {
    std::printf("ABI Compatibility Tests\n");
    std::printf("=======================\n\n");

    // Test 1: current_version returns valid version for each domain
    {
        auto executor = ahfl::abi::current_version(ahfl::abi::AbiDomain::Executor);
        auto ir = ahfl::abi::current_version(ahfl::abi::AbiDomain::IR);
        auto package = ahfl::abi::current_version(ahfl::abi::AbiDomain::Package);

        bool all_valid = (executor.major > 0) && (ir.major > 0) && (package.major > 0) &&
                         (!executor.schema_hash.empty()) && (!ir.schema_hash.empty()) &&
                         (!package.schema_hash.empty()) &&
                         (ahfl::abi::domain_name(ahfl::abi::AbiDomain::Executor) == "Executor") &&
                         (ahfl::abi::format_version(executor) == "1.1.0");
        check(all_valid, "current_version returns valid version for each domain");
    }

    // Test 2: check_compatibility: same version -> FullyCompatible
    {
        auto v = ahfl::abi::current_version(ahfl::abi::AbiDomain::Executor);
        auto result = ahfl::abi::check_compatibility(
            ahfl::abi::AbiDomain::Executor, v, v);
        check(result.level == ahfl::abi::CompatibilityLevel::FullyCompatible,
              "check_compatibility: same version is FullyCompatible");
    }

    // Test 3: check_compatibility: different major -> BreakingChange
    {
        ahfl::abi::AbiVersion source{1, 0, 0, "abcdef0123456789"};
        ahfl::abi::AbiVersion target{2, 0, 0, "abcdef0123456789"};
        auto result = ahfl::abi::check_compatibility(
            ahfl::abi::AbiDomain::IR, source, target);
        check(result.level == ahfl::abi::CompatibilityLevel::BreakingChange,
              "check_compatibility: different major is BreakingChange");
    }

    // Test 4: check_compatibility: same major, different minor -> BackwardCompatible
    {
        ahfl::abi::AbiVersion source{1, 0, 0, "abcdef0123456789"};
        ahfl::abi::AbiVersion target{1, 1, 0, "abcdef0123456789"};
        auto result = ahfl::abi::check_compatibility(
            ahfl::abi::AbiDomain::Package, source, target);
        bool compat = result.level == ahfl::abi::CompatibilityLevel::BackwardCompatible &&
                      ahfl::abi::is_compatible(source, target);
        check(compat, "check_compatibility: same major different minor is BackwardCompatible");
    }

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return (pass_count == test_count) ? 0 : 1;
}
