#include "cli/command_catalog.hpp"
#include "durable_store_import/artifacts.hpp"

#include <cstdio>
#include <optional>
#include <string_view>

namespace {

int test_count = 0;
int pass_count = 0;

struct ProviderCommandSpec {
    std::string_view token;
    std::string_view artifact_kind;
};

struct ProviderArtifactSpec {
    std::string_view kind;
    std::string_view command_token;
};

constexpr ProviderCommandSpec kProviderCommands[] = {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND(kind,                                      \
                                                       token,                                     \
                                                       usage_order,                               \
                                                       action_order,                              \
                                                       inference_order,                           \
                                                       package_order,                             \
                                                       capability_order,                          \
                                                       artifact_kind)                             \
    {token, #artifact_kind},
#include "cli/durable_store_import_provider_commands.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND
};

constexpr ProviderArtifactSpec kProviderArtifacts[] = {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                     \
                                                        artifact_type,                            \
                                                        builder,                                  \
                                                        printer,                                  \
                                                        command_token,                            \
                                                        visibility)                              \
    {#kind, command_token},
#include "cli/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
};

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

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::optional<std::string_view> emitted_id_from_token(std::string_view token) {
    constexpr std::string_view prefix = "emit-";
    if (!starts_with(token, prefix)) {
        return std::nullopt;
    }
    token.remove_prefix(prefix.size());
    return token;
}

bool durable_store_emit_commands_have_artifact_printers() {
    for (const auto command : ahfl::cli::command_list(ahfl::cli::CommandListKind::Action)) {
        const auto artifact_id = ahfl::cli::emitted_artifact_id(command);
        if (!artifact_id.has_value() || !starts_with(*artifact_id, "durable-store-import")) {
            continue;
        }

        if (ahfl::find_durable_store_import_artifact_printer(*artifact_id) == nullptr) {
            return false;
        }
    }
    return true;
}

bool durable_store_artifact_printers_have_emit_commands() {
    for (const auto &printer : ahfl::durable_store_import_artifact_printers()) {
        bool found = false;
        for (const auto command : ahfl::cli::command_list(ahfl::cli::CommandListKind::Action)) {
            const auto artifact_id = ahfl::cli::emitted_artifact_id(command);
            if (artifact_id.has_value() && *artifact_id == printer.artifact_id) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }
    return true;
}

bool provider_commands_have_artifact_nodes() {
    for (const auto &command : kProviderCommands) {
        bool found = false;
        for (const auto &artifact : kProviderArtifacts) {
            if (artifact.kind == command.artifact_kind &&
                artifact.command_token == command.token) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool provider_artifact_nodes_have_commands() {
    for (const auto &artifact : kProviderArtifacts) {
        bool found = false;
        for (const auto &command : kProviderCommands) {
            if (command.artifact_kind == artifact.kind &&
                command.token == artifact.command_token) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool provider_artifact_nodes_have_printers() {
    for (const auto &artifact : kProviderArtifacts) {
        const auto emitted_id = emitted_id_from_token(artifact.command_token);
        if (!emitted_id.has_value() ||
            ahfl::find_durable_store_import_artifact_printer(*emitted_id) == nullptr) {
            return false;
        }
    }
    return true;
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
    check(durable_store_emit_commands_have_artifact_printers(),
          "durable-store emit commands map to artifact printers");
    check(durable_store_artifact_printers_have_emit_commands(),
          "durable-store artifact printers map back to emit commands");
    check(provider_commands_have_artifact_nodes(),
          "provider commands map to provider artifact nodes");
    check(provider_artifact_nodes_have_commands(),
          "provider artifact nodes map back to provider commands");
    check(provider_artifact_nodes_have_printers(),
          "provider artifact nodes map to artifact printers");

    // --- Subcommand dispatch tests ---

    std::printf("\n=== Subcommand Dispatch Tests ===\n\n");

    // action_group_from_token
    check(ahfl::cli::action_group_from_token("emit") == ahfl::cli::ActionGroup::Emit,
          "action_group_from_token: emit");
    check(ahfl::cli::action_group_from_token("dump") == ahfl::cli::ActionGroup::Dump,
          "action_group_from_token: dump");
    check(ahfl::cli::action_group_from_token("verify") == ahfl::cli::ActionGroup::Verify,
          "action_group_from_token: verify");
    check(ahfl::cli::action_group_from_token("validate") == ahfl::cli::ActionGroup::Validate,
          "action_group_from_token: validate");
    check(!ahfl::cli::action_group_from_token("check").has_value(),
          "action_group_from_token: check returns nullopt");
    check(!ahfl::cli::action_group_from_token("emit-ir").has_value(),
          "action_group_from_token: flat token returns nullopt");

    // resolve_subcommand — core emit artifacts
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "ir") ==
              ahfl::cli::CommandKind::EmitIr,
          "resolve: emit ir");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "ir-json") ==
              ahfl::cli::CommandKind::EmitIrJson,
          "resolve: emit ir-json");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "scheduler-snapshot") ==
              ahfl::cli::CommandKind::EmitSchedulerSnapshot,
          "resolve: emit scheduler-snapshot");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "summary") ==
              ahfl::cli::CommandKind::EmitSummary,
          "resolve: emit summary");

    // resolve_subcommand — store domain (unified: both durable-store-import and store-import → store/)
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store/request") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportRequest,
          "resolve: emit store/request");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store/decision") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportDecision,
          "resolve: emit store/decision");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store/receipt") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportReceipt,
          "resolve: emit store/receipt");
    // resolve_subcommand — store-import (plain top-level, no domain prefix)
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store-import-descriptor") ==
              ahfl::cli::CommandKind::EmitStoreImportDescriptor,
          "resolve: emit store-import-descriptor");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store-import-review") ==
              ahfl::cli::CommandKind::EmitStoreImportReview,
          "resolve: emit store-import-review");

    // resolve_subcommand — provider domain
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "provider/write-attempt") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportProviderWriteAttempt,
          "resolve: emit provider/write-attempt");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "provider/driver-binding") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportProviderDriverBinding,
          "resolve: emit provider/driver-binding");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit,
              "provider/production-readiness-report") ==
              ahfl::cli::CommandKind::EmitDurableStoreImportProviderProductionReadinessReport,
          "resolve: emit provider/production-readiness-report");

    // resolve_subcommand — dump, verify, validate
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "ast") ==
              ahfl::cli::CommandKind::DumpAst,
          "resolve: dump ast");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "types") ==
              ahfl::cli::CommandKind::DumpTypes,
          "resolve: dump types");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "project") ==
              ahfl::cli::CommandKind::DumpProject,
          "resolve: dump project");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Verify, "formal") ==
              ahfl::cli::CommandKind::VerifyFormal,
          "resolve: verify formal");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Validate, "assurance") ==
              ahfl::cli::CommandKind::ValidateAssurance,
          "resolve: validate assurance");

    // resolve_subcommand — unknown artifacts
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "nonexistent").has_value(),
          "resolve: emit nonexistent returns nullopt");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "invalid").has_value(),
          "resolve: dump invalid returns nullopt");

    // command_short_name — roundtrip validation
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitIr) == "ir",
          "short_name: EmitIr -> ir");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitDurableStoreImportRequest) ==
              "store/request",
          "short_name: EmitDurableStoreImportRequest -> store/request");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitStoreImportDescriptor) ==
              "store-import-descriptor",
          "short_name: EmitStoreImportDescriptor -> store-import-descriptor");
    check(ahfl::cli::command_short_name(
              ahfl::cli::CommandKind::EmitDurableStoreImportProviderWriteAttempt) ==
              "provider/write-attempt",
          "short_name: provider write-attempt");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::DumpAst) == "ast",
          "short_name: DumpAst -> ast");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::VerifyFormal) == "formal",
          "short_name: VerifyFormal -> formal");

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
