#include "pipeline/persistence/durable_store_import/artifacts.hpp"
#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/option_table.hpp"
#include "tooling/cli/provider/provider_artifact_catalog.hpp"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int test_count = 0;
int pass_count = 0;

struct ProviderArtifactSpec {
    std::string_view kind;
    ahfl::cli::ProviderArtifactKind artifact_kind;
    std::string_view artifact_id;
    ahfl::cli::ProviderArtifactVisibility visibility;
    int order;
    std::size_t dependency_count;
};

constexpr ProviderArtifactSpec kProviderArtifacts[] = {
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(kind,                                      \
                                                        artifact_type,                             \
                                                        builder,                                   \
                                                        printer,                                   \
                                                        artifact_id,                               \
                                                        visibility,                                \
                                                        order,                                     \
                                                        dep_count,                                 \
                                                        dependencies)                              \
    {#kind,                                                                                        \
     ahfl::cli::ProviderArtifactKind::kind,                                                        \
     artifact_id,                                                                                  \
     ahfl::cli::ProviderArtifactVisibility::visibility,                                            \
     order,                                                                                        \
     dep_count},
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
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
        if (starts_with(printer.artifact_id, "durable-store-import-provider-")) {
            continue;
        }
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

bool provider_artifact_nodes_have_descriptor_metadata() {
    if (ahfl::cli::provider_artifact_descriptors().size() != std::size(kProviderArtifacts)) {
        return false;
    }

    for (const auto &artifact : kProviderArtifacts) {
        const auto *descriptor = ahfl::cli::provider_artifact_descriptor(artifact.artifact_kind);
        if (descriptor == nullptr) {
            return false;
        }
        if (descriptor->artifact_id != artifact.artifact_id ||
            descriptor->visibility != artifact.visibility || descriptor->order != artifact.order) {
            return false;
        }
        if (ahfl::cli::provider_artifact_id(artifact.artifact_kind) != artifact.artifact_id) {
            return false;
        }
        if (ahfl::cli::provider_artifact_visibility(artifact.artifact_kind) !=
            artifact.visibility) {
            return false;
        }
        std::string expected_cli_id = "provider/";
        expected_cli_id += artifact.artifact_id;
        if (ahfl::cli::provider_artifact_cli_id(artifact.artifact_kind) != expected_cli_id) {
            return false;
        }
        std::string expected_command_name = "emit-provider-artifact ";
        expected_command_name += expected_cli_id;
        if (ahfl::cli::provider_artifact_command_name(artifact.artifact_kind) !=
            expected_command_name) {
            return false;
        }
    }
    return true;
}

bool provider_artifact_nodes_have_printers() {
    for (const auto &artifact : kProviderArtifacts) {
        std::string durable_artifact_id = "durable-store-import-provider-";
        durable_artifact_id += artifact.artifact_id;
        if (ahfl::find_durable_store_import_artifact_printer(durable_artifact_id) == nullptr) {
            return false;
        }
    }
    return true;
}

bool provider_artifact_nodes_are_complete() {
    for (const auto &artifact : kProviderArtifacts) {
        const auto node = ahfl::cli::provider_artifact_node(artifact.artifact_kind);
        if (!node.has_value()) {
            return false;
        }
        if (node->descriptor.kind != artifact.artifact_kind ||
            node->descriptor.artifact_id != artifact.artifact_id ||
            node->descriptor.visibility != artifact.visibility ||
            node->descriptor.order != artifact.order) {
            return false;
        }
        if (node->dependencies.size() != artifact.dependency_count) {
            return false;
        }
    }
    return true;
}

bool provider_artifact_dependencies_are_catalog_nodes() {
    for (const auto &artifact : kProviderArtifacts) {
        for (const auto dependency :
             ahfl::cli::provider_artifact_dependencies(artifact.artifact_kind)) {
            if (!ahfl::cli::provider_artifact_has_node(dependency)) {
                return false;
            }
        }
    }
    return true;
}

bool provider_artifact_resolution_respects_visibility() {
    for (const auto &artifact : kProviderArtifacts) {
        std::string cli_artifact_id = "provider/";
        cli_artifact_id += artifact.artifact_id;
        const auto default_resolution =
            ahfl::cli::resolve_provider_artifact(cli_artifact_id, false);
        const auto hidden_resolution = ahfl::cli::resolve_provider_artifact(cli_artifact_id, true);

        if (artifact.visibility == ahfl::cli::ProviderArtifactVisibility::Public) {
            if (default_resolution != artifact.artifact_kind ||
                hidden_resolution != artifact.artifact_kind) {
                return false;
            }
            continue;
        }
        if (default_resolution.has_value() || hidden_resolution != artifact.artifact_kind) {
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
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitOptIr),
          "emit-opt-ir is handled by the CLI opt pipeline");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitOptIrJson),
          "emit-opt-ir-json is handled by the CLI opt pipeline");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitRuntimeSession),
          "emit-runtime-session is not a core backend command");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitDurableStoreImportReview),
          "durable-store review is not a core backend command");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::RunWorkflow),
          "run is not a core backend command");

    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitExecutionPlan),
          "emit-execution-plan remains package-aware");
    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitRuntimeSession),
          "runtime session remains package-aware");
    check(!ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitSmv),
          "emit-smv is not package-aware");
    check(!ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::RunWorkflow),
          "run does not require package metadata");
    check(durable_store_emit_commands_have_artifact_printers(),
          "durable-store emit commands map to artifact printers");
    check(durable_store_artifact_printers_have_emit_commands(),
          "durable-store artifact printers map back to emit commands");
    check(provider_artifact_nodes_have_descriptor_metadata(),
          "provider artifact nodes carry descriptor metadata");
    check(provider_artifact_nodes_have_printers(),
          "provider artifact nodes map to artifact printers");
    check(provider_artifact_nodes_are_complete(), "provider artifact nodes are complete");
    check(provider_artifact_dependencies_are_catalog_nodes(),
          "provider artifact dependencies resolve to catalog nodes");
    check(provider_artifact_resolution_respects_visibility(),
          "provider artifact resolution respects visibility");

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
    check(!ahfl::cli::action_group_from_token("run").has_value(),
          "action_group_from_token: run returns nullopt");
    check(!ahfl::cli::action_group_from_token("emit-ir").has_value(),
          "action_group_from_token: flat token returns nullopt");

    // Standalone command parse
    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "run",
            "--workflow",
            "MainWorkflow",
            "--input",
            "{\"message\":\"hello\"}",
            "--llm-config",
            "llm.json",
            "--llm-observability",
            "llm-observability.json",
            "--capability-bindings",
            "bindings.json",
            "app.ahfl",
        };
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: run has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::RunWorkflow,
              "parse_options: run command selected");
        check(options.workflow_name.has_value() && *options.workflow_name == "MainWorkflow",
              "parse_options: run workflow captured");
        check(options.runtime_input_json.has_value() &&
                  *options.runtime_input_json == "{\"message\":\"hello\"}",
              "parse_options: run input captured");
        check(options.llm_config_descriptor.has_value() &&
                  *options.llm_config_descriptor == "llm.json",
              "parse_options: run llm config captured");
        check(options.llm_observability_path.has_value() &&
                  *options.llm_observability_path == "llm-observability.json",
              "parse_options: run llm observability output captured");
        check(options.capability_bindings_descriptor.has_value() &&
                  *options.capability_bindings_descriptor == "bindings.json",
              "parse_options: run capability bindings captured");
        check(options.positional.size() == 1 && options.positional.front() == "app.ahfl",
              "parse_options: run input file captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {"emit", "summary", "-O", "--time-passes", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: time-passes has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::EmitSummary,
              "parse_options: time-passes preserves selected command");
        check(options.optimize_requested, "parse_options: -O captured");
        check(options.time_passes_requested, "parse_options: --time-passes captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {"emit", "smv", "--smv-size-report", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: smv-size-report has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::EmitSmv,
              "parse_options: smv-size-report preserves selected command");
        check(options.smv_size_report_requested, "parse_options: --smv-size-report captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "emit",
            "summary",
            "--trace-export",
            "trace.jsonl",
            "--metrics-export",
            "metrics.jsonl",
            "--structured-log",
            "log.jsonl",
            "--memory-report",
            "memory.json",
            "app.ahfl",
        };
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: observability has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::EmitSummary,
              "parse_options: observability preserves selected command");
        check(options.trace_export_path.has_value() && *options.trace_export_path == "trace.jsonl",
              "parse_options: --trace-export captured");
        check(options.metrics_export_path.has_value() &&
                  *options.metrics_export_path == "metrics.jsonl",
              "parse_options: --metrics-export captured");
        check(options.structured_log_path.has_value() &&
                  *options.structured_log_path == "log.jsonl",
              "parse_options: --structured-log captured");
        check(options.memory_report_path.has_value() &&
                  *options.memory_report_path == "memory.json",
              "parse_options: --memory-report captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "verify", "--checker-timeout-seconds", "2", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: checker timeout has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::VerifyFormal,
              "parse_options: checker timeout preserves verify command");
        check(options.checker_timeout_seconds.has_value() &&
                  *options.checker_timeout_seconds == "2",
              "parse_options: --checker-timeout-seconds captured");
    }

    // resolve_subcommand — core emit artifacts
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "ir") ==
              ahfl::cli::CommandKind::EmitIr,
          "resolve: emit ir");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "ir-json") ==
              ahfl::cli::CommandKind::EmitIrJson,
          "resolve: emit ir-json");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "opt-ir") ==
              ahfl::cli::CommandKind::EmitOptIr,
          "resolve: emit opt-ir");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "opt-ir-json") ==
              ahfl::cli::CommandKind::EmitOptIrJson,
          "resolve: emit opt-ir-json");
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

    // provider artifact resolution — provider artifacts are not CommandKind values.
    check(ahfl::cli::resolve_provider_artifact("provider/write-attempt", false) ==
              ahfl::cli::ProviderArtifactKind::WriteAttempt,
          "resolve_provider_artifact: provider/write-attempt");
    check(ahfl::cli::resolve_provider_artifact("provider/driver-binding", false) ==
              ahfl::cli::ProviderArtifactKind::DriverBinding,
          "resolve_provider_artifact: provider/driver-binding");
    check(ahfl::cli::resolve_provider_artifact("provider/production-readiness-report", false) ==
              ahfl::cli::ProviderArtifactKind::ProductionReadinessReport,
          "resolve_provider_artifact: provider/production-readiness-report");
    check(!ahfl::cli::resolve_provider_artifact("provider/driver-readiness", false).has_value(),
          "resolve_provider_artifact: internal blocked by default");
    check(ahfl::cli::resolve_provider_artifact("provider/driver-readiness", true) ==
              ahfl::cli::ProviderArtifactKind::DriverReadiness,
          "resolve_provider_artifact: internal allowed when hidden is included");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "provider/write-attempt")
               .has_value(),
          "resolve_subcommand: provider artifact is not a command");

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {"emit", "provider/write-attempt", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(parse_result.has_value() && parse_result->exit_code == 2,
              "parse_options: emit provider artifact is rejected");
        check(!options.selected_provider_artifact.has_value(),
              "parse_options: rejected emit provider artifact is not selected");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "emit-provider-artifact", "provider/write-attempt", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(),
              "parse_options: internal provider artifact has no immediate exit");
        check(options.selected_provider_artifact == ahfl::cli::ProviderArtifactKind::WriteAttempt,
              "parse_options: internal provider artifact selected");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "emit-provider-artifact", "provider/driver-readiness", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(parse_result.has_value() && parse_result->exit_code == 2,
              "parse_options: internal hidden provider artifact requires show-hidden");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "emit-provider-artifact", "provider/driver-readiness", "--show-hidden", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(),
              "parse_options: hidden provider artifact allowed with show-hidden");
        check(options.selected_provider_artifact ==
                  ahfl::cli::ProviderArtifactKind::DriverReadiness,
              "parse_options: hidden provider artifact selected");
    }

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
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "lockfile") ==
              ahfl::cli::CommandKind::DumpLockfile,
          "resolve: dump lockfile");
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
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitOptIr) == "opt-ir",
          "short_name: EmitOptIr -> opt-ir");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitOptIrJson) == "opt-ir-json",
          "short_name: EmitOptIrJson -> opt-ir-json");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitDurableStoreImportRequest) ==
              "store/request",
          "short_name: EmitDurableStoreImportRequest -> store/request");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitStoreImportDescriptor) ==
              "store-import-descriptor",
          "short_name: EmitStoreImportDescriptor -> store-import-descriptor");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::DumpAst) == "ast",
          "short_name: DumpAst -> ast");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::DumpLockfile) == "lockfile",
          "short_name: DumpLockfile -> lockfile");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::VerifyFormal) == "formal",
          "short_name: VerifyFormal -> formal");

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
