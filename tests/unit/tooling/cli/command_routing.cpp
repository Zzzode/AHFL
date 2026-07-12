#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/option_table.hpp"

#include <cstdio>
#include <string_view>

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
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitOptIr),
          "emit-opt-ir is handled by the CLI opt pipeline");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::EmitOptIrJson),
          "emit-opt-ir-json is handled by the CLI opt pipeline");
    check(!ahfl::cli::is_core_backend_command(ahfl::cli::CommandKind::RunWorkflow),
          "run is not a core backend command");

    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitExecutionPlan),
          "emit-execution-plan remains package-aware");
    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitDryRunTrace),
          "emit-dry-run-trace remains package-aware");
    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::EmitSmv),
          "emit-smv is package-aware");
    check(ahfl::cli::is_package_supported_command(ahfl::cli::CommandKind::RunWorkflow),
          "run is package-aware for multi-file workflow execution");

    // --- Subcommand dispatch tests ---

    std::printf("\n=== Subcommand Dispatch Tests ===\n\n");

    // action_group_from_token
    check(ahfl::cli::action_group_from_token("emit") == ahfl::cli::ActionGroup::Emit,
          "action_group_from_token: emit");
    check(ahfl::cli::action_group_from_token("dump") == ahfl::cli::ActionGroup::Dump,
          "action_group_from_token: dump");
    check(ahfl::cli::action_group_from_token("registry") == ahfl::cli::ActionGroup::Registry,
          "action_group_from_token: registry");
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
            "--profile",
            "low-risk",
            "--output-format",
            "jsonl",
            "--verbosity",
            "trace",
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
        check(options.run_profile.has_value() && *options.run_profile == "low-risk",
              "parse_options: run profile captured");
        check(options.execution_output_format.has_value() &&
                  *options.execution_output_format == "jsonl",
              "parse_options: run output format captured");
        check(options.execution_verbosity.has_value() &&
                  *options.execution_verbosity == "trace",
              "parse_options: run verbosity captured");
        check(options.capability_bindings_descriptor.has_value() &&
                  *options.capability_bindings_descriptor == "bindings.json",
              "parse_options: run capability bindings captured");
        check(options.positional.size() == 1 && options.positional.front() == "app.ahfl",
              "parse_options: run input file captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "run",
            "--manifest",
            "app/ahfl.toml",
            "--profile",
            "low-risk",
            "--input-file",
            "inputs/override.json",
            "--output-format",
            "quiet",
            "--verbosity",
            "verbose",
        };
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: profile run has no immediate exit");
        check(options.runtime_input_file.has_value() &&
                  *options.runtime_input_file == "inputs/override.json",
              "parse_options: input file captured");
        check(options.run_profile.has_value() && *options.run_profile == "low-risk",
              "parse_options: named profile captured");
        check(options.execution_output_format.has_value() &&
                  *options.execution_output_format == "quiet",
              "parse_options: quiet output captured");
        check(options.execution_verbosity.has_value() &&
                  *options.execution_verbosity == "verbose",
              "parse_options: verbose output captured");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "run",
            "--input",
            "{}",
            "--input-file",
            "input.json",
            "app.ahfl",
        };
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(parse_result.has_value() && parse_result->exit_code == 2,
              "parse_options: input and input-file conflict is rejected");
    }

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "run",
            "--manifest",
            "app/ahfl.toml",
            "--input",
            "{\"message\":\"hello\"}",
            "--llm-config",
            "llm.json",
        };
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(),
              "parse_options: package run without explicit workflow has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::RunWorkflow,
              "parse_options: package run command selected");
        check(options.manifest_path.has_value() && *options.manifest_path == "app/ahfl.toml",
              "parse_options: package run manifest captured");
        check(!options.workflow_name.has_value(),
              "parse_options: package run leaves workflow for manifest entry inference");
        check(options.runtime_input_json.has_value() &&
                  *options.runtime_input_json == "{\"message\":\"hello\"}",
              "parse_options: package run input captured");
        check(options.llm_config_descriptor.has_value() &&
                  *options.llm_config_descriptor == "llm.json",
              "parse_options: package run llm config captured");
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

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {
            "registry", "resolve", "--manifest", "ahfl.toml", "--lockfile", "ahfl.lock"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(!parse_result.has_value(), "parse_options: registry resolve has no immediate exit");
        check(options.selected_command == ahfl::cli::CommandKind::RegistryResolve,
              "parse_options: registry resolve selected");
        check(options.manifest_path.has_value() && *options.manifest_path == "ahfl.toml",
              "parse_options: registry resolve manifest captured");
        check(options.lockfile_path.has_value() && *options.lockfile_path == "ahfl.lock",
              "parse_options: registry resolve lockfile captured");
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
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "dry-run-trace") ==
              ahfl::cli::CommandKind::EmitDryRunTrace,
          "resolve: emit dry-run-trace");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "summary") ==
              ahfl::cli::CommandKind::EmitSummary,
          "resolve: emit summary");

    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "runtime-session")
               .has_value(),
          "resolve: retired runtime-session is rejected");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "scheduler-snapshot")
               .has_value(),
          "resolve: retired scheduler-snapshot is rejected");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "store/request").has_value(),
          "resolve: retired store request is rejected");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit,
                                         "store-import-descriptor")
               .has_value(),
          "resolve: retired store-import descriptor is rejected");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Emit, "provider/write-attempt")
               .has_value(),
          "resolve: retired provider artifact is rejected");

    {
        ahfl::cli::CommandLineOptions options;
        constexpr std::string_view args[] = {"emit", "provider/write-attempt", "app.ahfl"};
        const auto parse_result = ahfl::cli::parse_options_from_table(args, options);
        check(parse_result.has_value() && parse_result->exit_code == 2,
              "parse_options: emit provider artifact is rejected");
    }

    // resolve_subcommand — dump, verify, validate
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "ast") ==
              ahfl::cli::CommandKind::DumpAst,
          "resolve: dump ast");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "types") ==
              ahfl::cli::CommandKind::DumpTypes,
          "resolve: dump types");
    check(!ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "project").has_value(),
          "resolve: dump project is removed");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Dump, "lockfile") ==
              ahfl::cli::CommandKind::DumpLockfile,
          "resolve: dump lockfile");
    check(ahfl::cli::resolve_subcommand(ahfl::cli::ActionGroup::Registry, "resolve") ==
              ahfl::cli::CommandKind::RegistryResolve,
          "resolve: registry resolve");
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
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::EmitDryRunTrace) ==
              "dry-run-trace",
          "short_name: EmitDryRunTrace -> dry-run-trace");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::DumpAst) == "ast",
          "short_name: DumpAst -> ast");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::DumpLockfile) == "lockfile",
          "short_name: DumpLockfile -> lockfile");
    check(ahfl::cli::command_short_name(ahfl::cli::CommandKind::VerifyFormal) == "formal",
          "short_name: VerifyFormal -> formal");

    std::printf("\n%d/%d tests passed\n", pass_count, test_count);
    return pass_count == test_count ? 0 : 1;
}
