#include "ahfl/cli/command_catalog.hpp"

#include <cstdio>

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

bool maps_to_backend(ahfl::cli::CommandKind command, ahfl::BackendKind expected_backend) {
    const auto backend = ahfl::cli::core_backend_for_command(command);
    return backend.has_value() && *backend == expected_backend;
}

} // namespace

int main() {
    std::printf("=== CLI Command Routing Tests ===\n\n");

    check(maps_to_backend(ahfl::cli::CommandKind::EmitIr, ahfl::BackendKind::Ir),
          "emit-ir maps to core IR backend");
    check(maps_to_backend(ahfl::cli::CommandKind::EmitSmv, ahfl::BackendKind::Smv),
          "emit-smv maps to core SMV backend");
    check(maps_to_backend(ahfl::cli::CommandKind::EmitAssuranceJson,
                          ahfl::BackendKind::AssuranceJson),
          "emit-assurance-json maps to core assurance backend");
    check(ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitNativeJson),
          "emit-native-json is a core backend command");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitRuntimeSession),
          "emit-runtime-session is not a core backend command");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitDurableStoreImportReview),
          "durable-store review is not a core backend command");

    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitExecutionPlan),
          "emit-execution-plan remains package-aware");
    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitRuntimeSession),
          "runtime session remains package-aware");
    check(!ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitSmv),
          "emit-smv is not package-aware");

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
