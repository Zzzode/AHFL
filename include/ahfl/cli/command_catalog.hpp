#pragma once

#include "ahfl/backends/driver.hpp"

#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ahfl::cli {

enum class CommandKind {
    Check,
    DumpAst,
    DumpTypes,
    DumpProject,
    EmitIr,
    EmitIrJson,
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
};

struct CommandLineOptions {
    std::optional<CommandKind> selected_command;
    bool dump_ast_requested{false};
    bool dump_types_requested{false};
    std::optional<std::string_view> package_descriptor;
    std::optional<std::string_view> capability_mocks_descriptor;
    std::optional<std::string_view> project_descriptor;
    std::optional<std::string_view> workspace_descriptor;
    std::optional<std::string_view> project_name;
    std::optional<std::string_view> workflow_name;
    std::optional<std::string_view> input_fixture;
    std::optional<std::string_view> run_id;
    std::vector<std::string_view> search_roots;
    std::vector<std::string_view> positional;
};

enum class CommandListKind {
    UsageProjectAware,
    Action,
    Inference,
    PackageSupported,
    CapabilityInputSupported,
};

[[nodiscard]] std::string_view command_name(CommandKind command);
[[nodiscard]] std::optional<CommandKind> command_token_to_kind(std::string_view argument);
[[nodiscard]] const std::vector<CommandKind> &command_list(CommandListKind list_kind);
[[nodiscard]] std::string format_comma_or_commands(std::span<const CommandKind> commands);

[[nodiscard]] bool is_action_enabled(const CommandLineOptions &options, CommandKind command);
[[nodiscard]] std::optional<CommandKind> infer_effective_command(const CommandLineOptions &options);
[[nodiscard]] int count_enabled_actions(const CommandLineOptions &options);

[[nodiscard]] bool is_package_supported_command(CommandKind command);
[[nodiscard]] bool is_capability_input_supported_command(CommandKind command);
[[nodiscard]] bool supports_capability_inputs(std::optional<CommandKind> command);
[[nodiscard]] bool is_command_requiring_package(CommandKind command);
[[nodiscard]] std::optional<ahfl::BackendKind>
selected_backend_for_command(std::optional<CommandKind> command);

void set_command_option(CommandLineOptions &options, CommandKind command);
void print_usage(std::ostream &out);

} // namespace ahfl::cli
