#pragma once

#include "ahfl/compiler/backends/driver.hpp"
#include "tooling/cli/provider/provider_artifact_catalog.hpp"

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
    DumpAst,
    DumpTypes,
    DumpProject,
    EmitIr,
    EmitIrJson,
    EmitOptIr,
    EmitOptIrJson,
    EmitNativeJson,
    EmitExecutionPlan,
    EmitExecutionJournal,
    EmitReplayView,
    EmitAuditReport,
    EmitSchedulerSnapshot,
    EmitCheckpointRecord,
    EmitCheckpointReview,
    EmitPersistenceDescriptor,
    EmitPersistenceReview,
    EmitExportManifest,
    EmitExportReview,
    EmitStoreImportDescriptor,
    EmitStoreImportReview,
    EmitDurableStoreImportRequest,
    EmitDurableStoreImportReview,
    EmitDurableStoreImportDecision,
    EmitDurableStoreImportReceipt,
    EmitDurableStoreImportReceiptPersistenceRequest,
    EmitDurableStoreImportDecisionReview,
    EmitDurableStoreImportReceiptReview,
    EmitDurableStoreImportReceiptPersistenceReview,
    EmitDurableStoreImportReceiptPersistenceResponse,
    EmitDurableStoreImportReceiptPersistenceResponseReview,
    EmitDurableStoreImportAdapterExecution,
    EmitDurableStoreImportRecoveryPreview,
    EmitSchedulerReview,
    EmitRuntimeSession,
    EmitDryRunTrace,
    EmitPackageReview,
    EmitSummary,
    EmitSmv,
    EmitAssuranceJson,
    EmitK8sCrd,
    EmitOpenApi,
    EmitTerraform,
    EmitWasm,
    ValidateAssurance,
    VerifyFormal,
    _Count, // sentinel — must be last
};

struct CommandLineOptions {
    std::optional<CommandKind> selected_command;
    std::optional<ProviderArtifactKind> selected_provider_artifact;
    bool dump_ast_requested{false};
    bool dump_types_requested{false};
    std::optional<std::string_view> package_descriptor;
    std::optional<std::string_view> capability_mocks_descriptor;
    std::optional<std::string_view> tool_catalog_descriptor;
    std::optional<std::string_view> capability_bindings_descriptor;
    std::optional<std::string_view> project_descriptor;
    std::optional<std::string_view> workspace_descriptor;
    std::optional<std::string_view> project_name;
    std::optional<std::string_view> workflow_name;
    std::optional<std::string_view> runtime_input_json;
    std::optional<std::string_view> llm_config_descriptor;
    std::optional<std::string_view> llm_observability_path;
    std::optional<std::string_view> input_fixture;
    std::optional<std::string_view> run_id;
    std::optional<std::string_view> formal_backend;
    std::optional<std::string_view> model_checker;
    std::optional<std::string_view> checker_timeout_seconds;
    std::optional<std::string_view> formal_model_out;
    std::optional<std::string_view> bmc_depth;
    std::optional<std::string_view> bmc_boundary_invariants;
    bool explain_requested{false};
    bool optimize_requested{false};
    bool time_passes_requested{false};
    bool smv_size_report_requested{false};
    std::optional<std::string_view> trace_export_path;
    std::optional<std::string_view> metrics_export_path;
    std::optional<std::string_view> structured_log_path;
    std::optional<std::string_view> memory_report_path;
    bool show_internal_artifacts{false};
    bool format_check_requested{false};
    std::vector<std::string_view> search_roots;
    std::vector<std::string_view> positional;
};

struct SelectedAction {
    std::optional<CommandKind> command;
    std::optional<ProviderArtifactKind> provider_artifact;
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
    Verify,
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
void set_provider_artifact_option(CommandLineOptions &options, ProviderArtifactKind artifact);
void print_usage(std::ostream &out, bool show_internal = false);

} // namespace ahfl::cli
