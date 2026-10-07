#pragma once

#include "ahfl/compiler/backends/driver.hpp"

#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::cli {

enum class CommandKind {
    Check,
    RunWorkflow,
    Format,
    InitSingleFile,
    PackageArchive,
    PackagePublish,
    PackageYank,
    RegistryResolve,
    DumpAst,
    DumpTypes,
    DumpPackageGraph,
    DumpLockfile,
    EmitIr,
    EmitIrJson,
    // RFC 0026 P9 / KR6.9: the layered Core-IR projection
    // (`ahfl.core.v1`, `ir::core::print_core_ir_json`).
    EmitCoreIrJson,
    EmitOptIr,
    EmitOptIrJson,
    EmitNativeJson,
    EmitExecutionPlan,
    EmitDryRunTrace,
    EmitPackageReview,
    EmitPublicApi,
    EmitPublicApiDocs,
    EmitPublicApiDiff,
    EmitSummary,
    EmitSmv,
    EmitSmt,
    EmitAssuranceJson,
    EmitK8sCrd,
    EmitOpenApi,
    EmitTerraform,
    EmitWasm,
    ValidateAssurance,
    _Count, // sentinel — must be last
};

// RFC 0028: formal verification stage in the default compilation pipeline.
// `Off`   — structural IR invariants only (pre-RFC behavior).
// `On`  — structural + subset eligibility + SMT-BMC + full SMV model checking,
//         fail-closed on missing solver, timeout, or refutation.
// `Off` — structural verification only (no formal stage).
enum class VerificationMode { Off, On };

struct CommandLineOptions {
    std::optional<CommandKind> selected_command;
    bool dump_ast_requested{false};
    bool dump_types_requested{false};
    std::optional<std::string_view> package_name;
    std::optional<std::string_view> capability_mocks_descriptor;
    std::optional<std::string_view> tool_catalog_descriptor;
    std::optional<std::string_view> capability_bindings_descriptor;
    std::optional<std::string_view> workspace_manifest_path;
    std::optional<std::string_view> manifest_path;
    std::optional<std::string_view> sysroot_path;
    std::optional<std::string_view> target_name;
    std::optional<std::string_view> workflow_name;
    std::optional<std::string_view> runtime_input_json;
    std::optional<std::string_view> runtime_input_file;
    std::optional<std::string_view> llm_config_descriptor;
    std::optional<std::string_view> run_profile;
    std::optional<std::string_view> wasm_profile;
    std::optional<std::string_view> execution_output_format;
    std::optional<std::string_view> execution_verbosity;
    std::optional<std::string_view> input_fixture;
    std::optional<std::string_view> run_id;
    // RFC 0022 durable resume: --recovery-store persists a resume record on
    // suspend and loads it on the next run; --resume-pending-result supplies the
    // awaited capability result (JSON) when resuming a suspended workflow.
    std::optional<std::string_view> recovery_store_path;
    std::optional<std::string_view> resume_pending_result_json;
    // RFC 0022: force the named capability to return PENDING (suspend) on a fresh
    // run, so a suspend->resume round-trip is drivable from the shell. Inert when
    // resuming (a loaded recovery_snapshot serves that call from the memo).
    std::optional<std::string_view> suspend_capability;
    // RFC 0022 slice 4 (exactly-once): append a JSONL write-ahead intent record
    // {idempotency_key, capability} to this path before each durable_write /
    // financial_write capability dispatch, for host-side dedup across a crash.
    std::optional<std::string_view> intent_log_path;
    std::optional<std::string_view> formal_backend;
    std::optional<std::string_view> model_checker;
    std::optional<std::string_view> checker_timeout_seconds;
    std::optional<std::string_view> formal_model_out;
    std::optional<std::string_view> package_archive_output_path;
    std::optional<std::string_view> package_registry_id;
    std::optional<std::string_view> lockfile_path;
    std::optional<std::string_view> bmc_depth;
    std::optional<std::string_view> bmc_boundary_invariants;
    bool explain_requested{false};
    bool optimize_requested{false};
    bool time_passes_requested{false};
    bool smv_size_report_requested{false};
    std::optional<std::string_view> trace_export_path;
    std::optional<std::string_view> pass_trace_export_path;
    std::optional<std::string_view> pass_trace_baseline_path;
    std::optional<std::string_view> metrics_export_path;
    std::optional<std::string_view> structured_log_path;
    std::optional<std::string_view> memory_report_path;
    std::optional<std::string_view> public_api_from_version;
    std::optional<std::string_view> public_api_to_version;
    std::optional<std::string_view> package_yank_reason;
    bool format_check_requested{false};
    bool public_api_semver_gate_requested{false};
    bool package_publish_dry_run_requested{false};
    // RFC 0028: formal verification stage. Raw CLI string ("off|light|full"),
    // parsed on-demand. nullopt = per-command default (light for check/run,
    // off for everything else).
    std::optional<std::string_view> verify_mode;
    std::vector<std::string_view> positional;
};

struct SelectedAction {
    std::optional<CommandKind> command;
};

enum class CommandListKind {
    UsageProjectAware,
    Action,
    Inference,
    PackageSupported,
    CapabilityInputSupported,
};

// ---------------------------------------------------------------------------
// Subcommand dispatch (ahflc emit <artifact-id>, ahflc dump <target>, etc.)
// ---------------------------------------------------------------------------

enum class ActionGroup {
    Emit,
    Dump,
    Package,
    Registry,
    Validate
};

[[nodiscard]] std::optional<ActionGroup> action_group_from_token(std::string_view token);
[[nodiscard]] std::optional<CommandKind> resolve_subcommand(ActionGroup group,
                                                            std::string_view artifact_id);
[[nodiscard]] std::optional<CommandKind>
resolve_subcommand(ActionGroup group, std::string_view artifact_id, bool include_hidden);
[[nodiscard]] std::string command_short_name(CommandKind command);

[[nodiscard]] std::string_view command_name(CommandKind command);
[[nodiscard]] std::optional<std::string_view> emitted_artifact_id(CommandKind command);
[[nodiscard]] const std::vector<CommandKind> &command_list(CommandListKind list_kind);
[[nodiscard]] std::string format_comma_or_commands(std::span<const CommandKind> commands);

[[nodiscard]] bool is_action_enabled(const CommandLineOptions &options, CommandKind command);
[[nodiscard]] std::optional<CommandKind> infer_effective_command(const CommandLineOptions &options);
[[nodiscard]] int count_enabled_actions(const CommandLineOptions &options);

[[nodiscard]] bool is_package_supported_command(CommandKind command);
[[nodiscard]] bool is_capability_input_supported_command(CommandKind command);
[[nodiscard]] bool supports_capability_inputs(std::optional<CommandKind> command);
[[nodiscard]] bool is_command_requiring_package(CommandKind command);
[[nodiscard]] SelectedAction
selected_action_from_options(const CommandLineOptions &options,
                             std::optional<CommandKind> effective_command);
[[nodiscard]] std::string selected_action_name(const SelectedAction &action);
[[nodiscard]] bool selected_action_supports_package(const SelectedAction &action);
[[nodiscard]] bool selected_action_supports_capability_inputs(const SelectedAction &action);
[[nodiscard]] bool selected_action_requires_package(const SelectedAction &action);
[[nodiscard]] std::optional<ahfl::BackendKind>
core_backend_for_command(std::optional<CommandKind> command);
[[nodiscard]] bool is_core_backend_command(CommandKind command);

void set_command_option(CommandLineOptions &options, CommandKind command);
void print_usage(std::ostream &out);

// RFC 0028: parse the --verify=<off|on> string into the enum.
// Returns nullopt for unrecognized values.
[[nodiscard]] std::optional<VerificationMode> parse_verification_mode(std::string_view text);

// RFC 0028: resolve the per-command default verification mode.
// check/run default to on (fail-closed); everything else defaults to off.
[[nodiscard]] VerificationMode default_verification_mode(std::optional<CommandKind> command);

// RFC 0028: true when the formal verification stage is active (mode != Off).
[[nodiscard]] bool formal_stage_active(VerificationMode mode);

} // namespace ahfl::cli
