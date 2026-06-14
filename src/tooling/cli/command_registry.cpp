#include "tooling/cli/command_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ahfl::cli {
namespace {

constexpr int kNotListed = -1;

struct CommandSpec {
    CommandKind kind;
    std::string_view token;
    std::optional<ActionGroup> action_group;
    std::string_view action_target;
    std::string_view emitted_artifact_id;
    int usage_project_aware_order{kNotListed};
    int action_order{kNotListed};
    int inference_order{kNotListed};
    int package_supported_order{kNotListed};
    int capability_input_supported_order{kNotListed};
};

[[nodiscard]] constexpr CommandSpec command(CommandKind kind,
                                            std::string_view token,
                                            int usage_project_aware_order = kNotListed,
                                            int action_order = kNotListed,
                                            int inference_order = kNotListed,
                                            int package_supported_order = kNotListed,
                                            int capability_input_supported_order = kNotListed) {
    return CommandSpec{
        kind,
        token,
        std::nullopt,
        {},
        {},
        usage_project_aware_order,
        action_order,
        inference_order,
        package_supported_order,
        capability_input_supported_order,
    };
}

[[nodiscard]] constexpr CommandSpec
routed_command(CommandKind kind,
               std::string_view token,
               ActionGroup action_group,
               std::string_view action_target,
               int usage_project_aware_order = kNotListed,
               int action_order = kNotListed,
               int inference_order = kNotListed,
               int package_supported_order = kNotListed,
               int capability_input_supported_order = kNotListed) {
    return CommandSpec{
        kind,
        token,
        action_group,
        action_target,
        {},
        usage_project_aware_order,
        action_order,
        inference_order,
        package_supported_order,
        capability_input_supported_order,
    };
}

[[nodiscard]] constexpr CommandSpec
emit_command(CommandKind kind,
             std::string_view token,
             std::string_view action_target,
             std::string_view emitted_artifact_id,
             int usage_project_aware_order = kNotListed,
             int action_order = kNotListed,
             int inference_order = kNotListed,
             int package_supported_order = kNotListed,
             int capability_input_supported_order = kNotListed) {
    return CommandSpec{
        kind,
        token,
        ActionGroup::Emit,
        action_target,
        emitted_artifact_id,
        usage_project_aware_order,
        action_order,
        inference_order,
        package_supported_order,
        capability_input_supported_order,
    };
}

constexpr CommandSpec kCommandSpecs[] = {
    command(CommandKind::Check, "check", 0),
    command(CommandKind::RunWorkflow, "run", 109, 106),
    routed_command(CommandKind::DumpAst, "dump-ast", ActionGroup::Dump, "ast", 1, 0, 73),
    routed_command(CommandKind::DumpTypes, "dump-types", ActionGroup::Dump, "types", 3, 1, 72),
    routed_command(CommandKind::DumpProject, "dump-project", ActionGroup::Dump, "project", 2, 2, 0),
    emit_command(CommandKind::EmitIr, "emit-ir", "ir", "ir", 4, 3, 1),
    emit_command(CommandKind::EmitIrJson, "emit-ir-json", "ir-json", "ir-json", 5, 4, 2),
    emit_command(CommandKind::EmitOptIr, "emit-opt-ir", "opt-ir", "opt-ir", 112, 107, 112),
    emit_command(CommandKind::EmitOptIrJson,
                 "emit-opt-ir-json",
                 "opt-ir-json",
                 "opt-ir-json",
                 113,
                 108,
                 113),
    emit_command(
        CommandKind::EmitNativeJson, "emit-native-json", "native-json", "native-json", 6, 5, 3, 0),
    emit_command(CommandKind::EmitExecutionPlan,
                 "emit-execution-plan",
                 "execution-plan",
                 "execution-plan",
                 7,
                 6,
                 4,
                 1),
    emit_command(CommandKind::EmitExecutionJournal,
                 "emit-execution-journal",
                 "execution-journal",
                 "execution-journal",
                 8,
                 7,
                 5,
                 2,
                 0),
    emit_command(CommandKind::EmitReplayView,
                 "emit-replay-view",
                 "replay-view",
                 "replay-view",
                 9,
                 8,
                 6,
                 3,
                 1),
    emit_command(CommandKind::EmitAuditReport,
                 "emit-audit-report",
                 "audit-report",
                 "audit-report",
                 10,
                 9,
                 7,
                 4,
                 2),
    emit_command(CommandKind::EmitSchedulerSnapshot,
                 "emit-scheduler-snapshot",
                 "scheduler-snapshot",
                 "scheduler-snapshot",
                 11,
                 10,
                 8,
                 5,
                 3),
    emit_command(CommandKind::EmitCheckpointRecord,
                 "emit-checkpoint-record",
                 "checkpoint-record",
                 "checkpoint-record",
                 12,
                 11,
                 9,
                 6,
                 4),
    emit_command(CommandKind::EmitCheckpointReview,
                 "emit-checkpoint-review",
                 "checkpoint-review",
                 "checkpoint-review",
                 13,
                 12,
                 10,
                 7,
                 5),
    emit_command(CommandKind::EmitPersistenceDescriptor,
                 "emit-persistence-descriptor",
                 "persistence-descriptor",
                 "persistence-descriptor",
                 14,
                 13,
                 11,
                 26,
                 6),
    emit_command(CommandKind::EmitPersistenceReview,
                 "emit-persistence-review",
                 "persistence-review",
                 "persistence-review",
                 15,
                 14,
                 12,
                 27,
                 7),
    emit_command(CommandKind::EmitExportManifest,
                 "emit-export-manifest",
                 "export-manifest",
                 "export-manifest",
                 16,
                 15,
                 13,
                 28,
                 8),
    emit_command(CommandKind::EmitExportReview,
                 "emit-export-review",
                 "export-review",
                 "export-review",
                 17,
                 16,
                 14,
                 29,
                 9),
    emit_command(CommandKind::EmitStoreImportDescriptor,
                 "emit-store-import-descriptor",
                 "store-import-descriptor",
                 "store-import-descriptor",
                 20,
                 17,
                 15,
                 30,
                 10),
    emit_command(CommandKind::EmitStoreImportReview,
                 "emit-store-import-review",
                 "store-import-review",
                 "store-import-review",
                 21,
                 18,
                 16,
                 31,
                 11),
    emit_command(CommandKind::EmitDurableStoreImportRequest,
                 "emit-durable-store-import-request",
                 "store/request",
                 "durable-store-import-request",
                 22,
                 19,
                 17,
                 32,
                 12),
    emit_command(CommandKind::EmitDurableStoreImportReview,
                 "emit-durable-store-import-review",
                 "store/review",
                 "durable-store-import-review",
                 23,
                 20,
                 18,
                 33,
                 13),
    emit_command(CommandKind::EmitDurableStoreImportDecision,
                 "emit-durable-store-import-decision",
                 "store/decision",
                 "durable-store-import-decision",
                 24,
                 21,
                 19,
                 34,
                 14),
    emit_command(CommandKind::EmitDurableStoreImportReceipt,
                 "emit-durable-store-import-receipt",
                 "store/receipt",
                 "durable-store-import-receipt",
                 25,
                 22,
                 20,
                 35,
                 15),
    emit_command(CommandKind::EmitDurableStoreImportReceiptPersistenceRequest,
                 "emit-durable-store-import-receipt-persistence-request",
                 "store/receipt-persistence-request",
                 "durable-store-import-receipt-persistence-request",
                 26,
                 23,
                 21,
                 36,
                 16),
    emit_command(CommandKind::EmitDurableStoreImportDecisionReview,
                 "emit-durable-store-import-decision-review",
                 "store/decision-review",
                 "durable-store-import-decision-review",
                 27,
                 24,
                 22,
                 37,
                 17),
    emit_command(CommandKind::EmitDurableStoreImportReceiptReview,
                 "emit-durable-store-import-receipt-review",
                 "store/receipt-review",
                 "durable-store-import-receipt-review",
                 28,
                 25,
                 23,
                 38,
                 18),
    emit_command(CommandKind::EmitDurableStoreImportReceiptPersistenceReview,
                 "emit-durable-store-import-receipt-persistence-review",
                 "store/receipt-persistence-review",
                 "durable-store-import-receipt-persistence-review",
                 29,
                 26,
                 24,
                 39,
                 19),
    emit_command(CommandKind::EmitDurableStoreImportReceiptPersistenceResponse,
                 "emit-durable-store-import-receipt-persistence-response",
                 "store/receipt-persistence-response",
                 "durable-store-import-receipt-persistence-response",
                 30,
                 27,
                 25,
                 40,
                 20),
    emit_command(CommandKind::EmitDurableStoreImportReceiptPersistenceResponseReview,
                 "emit-durable-store-import-receipt-persistence-response-review",
                 "store/receipt-persistence-response-review",
                 "durable-store-import-receipt-persistence-response-review",
                 31,
                 28,
                 26,
                 41,
                 21),
    emit_command(CommandKind::EmitDurableStoreImportAdapterExecution,
                 "emit-durable-store-import-adapter-execution",
                 "store/adapter-execution",
                 "durable-store-import-adapter-execution",
                 32,
                 29,
                 27,
                 42,
                 22),
    emit_command(CommandKind::EmitDurableStoreImportRecoveryPreview,
                 "emit-durable-store-import-recovery-preview",
                 "store/recovery-preview",
                 "durable-store-import-recovery-preview",
                 33,
                 30,
                 28,
                 43,
                 23),
    emit_command(CommandKind::EmitSchedulerReview,
                 "emit-scheduler-review",
                 "scheduler-review",
                 "scheduler-review",
                 96,
                 93,
                 99,
                 8,
                 86),
    emit_command(CommandKind::EmitRuntimeSession,
                 "emit-runtime-session",
                 "runtime-session",
                 "runtime-session",
                 97,
                 94,
                 100,
                 9,
                 87),
    emit_command(CommandKind::EmitDryRunTrace,
                 "emit-dry-run-trace",
                 "dry-run-trace",
                 "dry-run-trace",
                 98,
                 95,
                 101,
                 10,
                 88),
    emit_command(CommandKind::EmitPackageReview,
                 "emit-package-review",
                 "package-review",
                 "package-review",
                 99,
                 96,
                 102,
                 11),
    emit_command(CommandKind::EmitSummary, "emit-summary", "summary", "summary", 100, 97, 103),
    emit_command(CommandKind::EmitSmv, "emit-smv", "smv", "smv", 101, 98, 104),
    emit_command(CommandKind::EmitAssuranceJson,
                 "emit-assurance-json",
                 "assurance-json",
                 "assurance-json",
                 102,
                 99,
                 105),
    emit_command(CommandKind::EmitK8sCrd, "emit-k8s-crd", "k8s-crd", "k8s-crd", 103, 100, 106),
    emit_command(CommandKind::EmitOpenApi, "emit-openapi", "openapi", "openapi", 104, 101, 107),
    emit_command(
        CommandKind::EmitTerraform, "emit-terraform", "terraform", "terraform", 105, 102, 108),
    emit_command(CommandKind::EmitWasm, "emit-wasm", "wasm", "wasm", 106, 103, 109),
    routed_command(CommandKind::ValidateAssurance,
                   "validate-assurance",
                   ActionGroup::Validate,
                   "assurance",
                   107,
                   104,
                   110,
                   12),
    routed_command(CommandKind::VerifyFormal,
                   "verify-formal",
                   ActionGroup::Verify,
                   "formal",
                   108,
                   105,
                   111,
                   13),
};

// Compile-time verification: array index must equal enum value for O(1) lookup.
static_assert(
    [] {
        for (std::size_t i = 0; i < std::size(kCommandSpecs); ++i) {
            if (static_cast<std::size_t>(kCommandSpecs[i].kind) != i)
                return false;
        }
        return true;
    }(),
    "kCommandSpecs must be indexed by CommandKind enum value");

[[nodiscard]] const CommandSpec *find_command_spec(CommandKind command) {
    const auto idx = static_cast<std::size_t>(command);
    if (idx >= std::size(kCommandSpecs))
        return nullptr;
    return &kCommandSpecs[idx];
}

[[nodiscard]] constexpr int command_list_order(const CommandSpec &spec, CommandListKind list_kind) {
    switch (list_kind) {
    case CommandListKind::UsageProjectAware:
        return spec.usage_project_aware_order;
    case CommandListKind::Action:
        return spec.action_order;
    case CommandListKind::Inference:
        return spec.inference_order;
    case CommandListKind::PackageSupported:
        return spec.package_supported_order;
    case CommandListKind::CapabilityInputSupported:
        return spec.capability_input_supported_order;
    }

    return kNotListed;
}

template <CommandListKind ListKind> [[nodiscard]] consteval bool command_list_has_unique_orders() {
    for (std::size_t lhs_index = 0; lhs_index < std::size(kCommandSpecs); ++lhs_index) {
        const auto lhs_order = command_list_order(kCommandSpecs[lhs_index], ListKind);
        if (lhs_order == kNotListed) {
            continue;
        }

        for (std::size_t rhs_index = lhs_index + 1; rhs_index < std::size(kCommandSpecs);
             ++rhs_index) {
            if (lhs_order == command_list_order(kCommandSpecs[rhs_index], ListKind)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(command_list_has_unique_orders<CommandListKind::UsageProjectAware>(),
              "duplicate usage order in command metadata");
static_assert(command_list_has_unique_orders<CommandListKind::Action>(),
              "duplicate action order in command metadata");
static_assert(command_list_has_unique_orders<CommandListKind::Inference>(),
              "duplicate inference order in command metadata");
static_assert(command_list_has_unique_orders<CommandListKind::PackageSupported>(),
              "duplicate package-supported order in command metadata");
static_assert(command_list_has_unique_orders<CommandListKind::CapabilityInputSupported>(),
              "duplicate capability-input-supported order in command metadata");

[[nodiscard]] std::vector<CommandKind> build_command_list(CommandListKind list_kind) {
    std::vector<std::pair<int, CommandKind>> ordered_commands;
    ordered_commands.reserve(std::size(kCommandSpecs));
    for (const auto &spec : kCommandSpecs) {
        if (const auto order = command_list_order(spec, list_kind); order != kNotListed) {
            ordered_commands.emplace_back(order, spec.kind);
        }
    }

    std::sort(ordered_commands.begin(),
              ordered_commands.end(),
              [](const auto &lhs, const auto &rhs) { return lhs.first < rhs.first; });

    std::vector<CommandKind> commands;
    commands.reserve(ordered_commands.size());
    for (const auto &[_, command] : ordered_commands) {
        commands.push_back(command);
    }
    return commands;
}

[[nodiscard]] bool command_in_list(CommandKind command, CommandListKind list_kind) {
    const auto *const spec = find_command_spec(command);
    return spec != nullptr && command_list_order(*spec, list_kind) != kNotListed;
}

} // namespace

[[nodiscard]] const std::vector<CommandKind> &command_list(CommandListKind list_kind) {
    static const auto kUsageProjectAware = build_command_list(CommandListKind::UsageProjectAware);
    static const auto kAction = build_command_list(CommandListKind::Action);
    static const auto kInference = build_command_list(CommandListKind::Inference);
    static const auto kPackageSupported = build_command_list(CommandListKind::PackageSupported);
    static const auto kCapabilityInputSupported =
        build_command_list(CommandListKind::CapabilityInputSupported);

    switch (list_kind) {
    case CommandListKind::UsageProjectAware:
        return kUsageProjectAware;
    case CommandListKind::Action:
        return kAction;
    case CommandListKind::Inference:
        return kInference;
    case CommandListKind::PackageSupported:
        return kPackageSupported;
    case CommandListKind::CapabilityInputSupported:
        return kCapabilityInputSupported;
    }

    return kAction;
}

[[nodiscard]] std::string_view command_name(CommandKind command) {
    if (const auto *const spec = find_command_spec(command); spec != nullptr) {
        return spec->token;
    }

    return "check";
}

[[nodiscard]] std::optional<std::string_view> emitted_artifact_id(CommandKind command) {
    const auto *spec = find_command_spec(command);
    if (spec == nullptr || spec->emitted_artifact_id.empty()) {
        return std::nullopt;
    }
    return spec->emitted_artifact_id;
}

[[nodiscard]] bool is_package_supported_command(CommandKind command) {
    return command_in_list(command, CommandListKind::PackageSupported);
}

[[nodiscard]] bool is_capability_input_supported_command(CommandKind command) {
    return command_in_list(command, CommandListKind::CapabilityInputSupported);
}

[[nodiscard]] bool supports_capability_inputs(std::optional<CommandKind> command) {
    return command.has_value() && is_capability_input_supported_command(*command);
}

[[nodiscard]] bool is_command_requiring_package(CommandKind command) {
    return is_capability_input_supported_command(command);
}

SelectedAction selected_action_from_options(const CommandLineOptions &options,
                                            std::optional<CommandKind> effective_command) {
    return SelectedAction{
        .command = effective_command,
        .provider_artifact = options.selected_provider_artifact,
    };
}

std::string selected_action_name(const SelectedAction &action) {
    if (action.provider_artifact.has_value()) {
        return provider_artifact_command_name(*action.provider_artifact);
    }
    if (action.command.has_value()) {
        return std::string(command_name(*action.command));
    }
    return "selected action";
}

bool selected_action_supports_package(const SelectedAction &action) {
    return action.provider_artifact.has_value() ||
           (action.command.has_value() && is_package_supported_command(*action.command));
}

bool selected_action_supports_capability_inputs(const SelectedAction &action) {
    return action.provider_artifact.has_value() ||
           (action.command.has_value() && supports_capability_inputs(action.command));
}

bool selected_action_requires_package(const SelectedAction &action) {
    return action.provider_artifact.has_value() ||
           (action.command.has_value() && is_command_requiring_package(*action.command));
}

[[nodiscard]] std::optional<ahfl::BackendKind>
core_backend_for_command(std::optional<CommandKind> command) {
    if (!command.has_value()) {
        return std::nullopt;
    }

    switch (*command) {
    case CommandKind::EmitIr:
        return ahfl::BackendKind::Ir;
    case CommandKind::EmitIrJson:
        return ahfl::BackendKind::IrJson;
    case CommandKind::EmitNativeJson:
        return ahfl::BackendKind::NativeJson;
    case CommandKind::EmitExecutionPlan:
        return ahfl::BackendKind::ExecutionPlan;
    case CommandKind::EmitPackageReview:
        return ahfl::BackendKind::PackageReview;
    case CommandKind::EmitSummary:
        return ahfl::BackendKind::Summary;
    case CommandKind::EmitSmv:
        return ahfl::BackendKind::Smv;
    case CommandKind::EmitAssuranceJson:
        return ahfl::BackendKind::AssuranceJson;
    case CommandKind::EmitK8sCrd:
        return ahfl::BackendKind::InfraK8sCrd;
    case CommandKind::EmitOpenApi:
        return ahfl::BackendKind::InfraOpenApi;
    case CommandKind::EmitTerraform:
        return ahfl::BackendKind::InfraTerraform;
    case CommandKind::EmitWasm:
        return ahfl::BackendKind::InfraWasm;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool is_core_backend_command(CommandKind command) {
    return core_backend_for_command(command).has_value();
}

void set_command_option(CommandLineOptions &options, CommandKind command) {
    options.selected_command = command;
}

void set_provider_artifact_option(CommandLineOptions &options, ProviderArtifactKind artifact) {
    options.selected_provider_artifact = artifact;
}

[[nodiscard]] bool is_action_enabled(const CommandLineOptions &options, CommandKind command) {
    if (command != CommandKind::Check && options.selected_command == command) {
        return true;
    }

    switch (command) {
    case CommandKind::DumpAst:
        return options.dump_ast_requested;
    case CommandKind::DumpTypes:
        return options.dump_types_requested;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<CommandKind>
infer_effective_command(const CommandLineOptions &options) {
    if (options.selected_command.has_value()) {
        return options.selected_command;
    }

    for (const auto command : command_list(CommandListKind::Inference)) {
        if (is_action_enabled(options, command)) {
            return command;
        }
    }

    return std::nullopt;
}

[[nodiscard]] int count_enabled_actions(const CommandLineOptions &options) {
    int count = 0;
    for (const auto command : command_list(CommandListKind::Action)) {
        count += static_cast<int>(is_action_enabled(options, command));
    }
    count += static_cast<int>(options.selected_provider_artifact.has_value());
    return count;
}

// ---------------------------------------------------------------------------
// Subcommand dispatch: ahflc emit <artifact-id>, ahflc dump <target>, etc.
// ---------------------------------------------------------------------------

std::optional<ActionGroup> action_group_from_token(std::string_view token) {
    if (token == "emit")
        return ActionGroup::Emit;
    if (token == "dump")
        return ActionGroup::Dump;
    if (token == "verify")
        return ActionGroup::Verify;
    if (token == "validate")
        return ActionGroup::Validate;
    return std::nullopt;
}

std::optional<CommandKind>
resolve_subcommand(ActionGroup group, std::string_view artifact_id, bool include_hidden) {
    static_cast<void>(include_hidden);
    for (const auto &spec : kCommandSpecs) {
        if (spec.action_group.has_value() && *spec.action_group == group &&
            spec.action_target == artifact_id) {
            return spec.kind;
        }
    }
    return std::nullopt;
}

std::optional<CommandKind> resolve_subcommand(ActionGroup group, std::string_view artifact_id) {
    return resolve_subcommand(group, artifact_id, false);
}

std::string command_short_name(CommandKind command) {
    const auto *spec = find_command_spec(command);
    if (spec == nullptr) {
        return "check";
    }
    if (spec->action_group.has_value()) {
        return std::string(spec->action_target);
    }
    return std::string(spec->token);
}

} // namespace ahfl::cli
