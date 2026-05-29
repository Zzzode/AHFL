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
        auto v1 = ahfl::abi::current_version(ahfl::abi::AbiDomain::DurableStore);
        auto v2 = ahfl::abi::current_version(ahfl::abi::AbiDomain::Executor);
        auto v3 = ahfl::abi::current_version(ahfl::abi::AbiDomain::Persistence);
        auto v4 = ahfl::abi::current_version(ahfl::abi::AbiDomain::IR);
        auto v5 = ahfl::abi::current_version(ahfl::abi::AbiDomain::Package);
        auto v6 = ahfl::abi::current_version(ahfl::abi::AbiDomain::RuntimeSession);

        bool all_valid = (v1.major > 0) && (v2.major > 0) && (v3.major > 0) &&
                         (v4.major > 0) && (v5.major > 0) && (v6.major > 0) &&
                         (!v1.schema_hash.empty()) && (!v2.schema_hash.empty()) &&
                         (!v3.schema_hash.empty()) && (!v4.schema_hash.empty()) &&
                         (!v5.schema_hash.empty()) && (!v6.schema_hash.empty()) &&
                         (ahfl::abi::domain_name(ahfl::abi::AbiDomain::DurableStore) == "DurableStore") &&
                         (ahfl::abi::format_version(v1) == "1.0.0");
        check(all_valid, "current_version returns valid version for each domain");
    }

    // Test 2: check_compatibility: same version -> FullyCompatible
    {
        auto v = ahfl::abi::current_version(ahfl::abi::AbiDomain::DurableStore);
        auto result = ahfl::abi::check_compatibility(
            ahfl::abi::AbiDomain::DurableStore, v, v);
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
