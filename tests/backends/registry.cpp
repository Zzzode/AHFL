#include "ahfl/backends/registry.hpp"
#include "ahfl/backends/driver.hpp"
#include "ahfl/ir/ir.hpp"

#include <cstdio>
#include <iterator>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

int test_count = 0;
int pass_count = 0;

void check(bool condition, const char *name) {
    ++test_count;
    if (condition) {
        ++pass_count;
        std::printf("  PASS: %s\n", name);
    } else {
        std::printf("  FAIL: %s\n", name);
    }
}

struct ExpectedBackend {
    ahfl::BackendKind kind;
    std::string_view name;
};

} // namespace

int main() {
    std::printf("=== Backend Registry Tests ===\n\n");

    constexpr ExpectedBackend expected_backends[] = {
        {ahfl::BackendKind::Ir, "ir"},
        {ahfl::BackendKind::IrJson, "ir-json"},
        {ahfl::BackendKind::NativeJson, "native-json"},
        {ahfl::BackendKind::ExecutionPlan, "execution-plan"},
        {ahfl::BackendKind::PackageReview, "package-review"},
        {ahfl::BackendKind::Summary, "summary"},
        {ahfl::BackendKind::Smv, "smv"},
        {ahfl::BackendKind::AssuranceJson, "assurance-json"},
    };

    auto &registry = ahfl::BackendRegistry::instance();
    const auto original_size = registry.size();

    for (const auto &expected : expected_backends) {
        const auto *entry = registry.find(expected.kind);
        check(entry != nullptr && entry->name == expected.name, "find built-in backend by kind");
        check(registry.find_by_name(expected.name) == entry, "find built-in backend by name");
    }

    check(registry.size() >= std::size(expected_backends), "registry contains built-in backends");
    check(registry.find_by_name("missing-backend") == nullptr, "missing backend lookup is null");

    ahfl::BackendEntry duplicate{
        ahfl::BackendKind::Ir,
        "duplicate-ir",
        "duplicate IR backend",
        [](const ahfl::EmitContext &) {},
    };
    check(!registry.register_backend(std::move(duplicate)), "duplicate backend kind is rejected");
    check(registry.size() == original_size, "duplicate registration does not mutate registry");

    ahfl::ir::Program program;
    std::ostringstream out;
    check(ahfl::emit_backend(ahfl::BackendKind::Ir, program, out, nullptr),
          "emit_backend returns true for registered backend");

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
