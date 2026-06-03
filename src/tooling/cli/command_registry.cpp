#include "tooling/cli/command_catalog.hpp"
#include "tooling/cli/provider/provider_artifact_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ahfl::cli {
namespace {

constexpr int kNotListed = -1;

struct CommandSpec {
    CommandKind kind;
    std::string_view token;
    int usage_project_aware_order{kNotListed};
    int action_order{kNotListed};
    int inference_order{kNotListed};
    int package_supported_order{kNotListed};
    int capability_input_supported_order{kNotListed};
};

// Base constants for provider command order derivation.
constexpr int kProviderUsageBase = 34;
constexpr int kProviderActionBase = 31;
constexpr int kProviderInferenceBase = 112;
constexpr int kProviderPackageBase = 44;
constexpr int kProviderCapabilityBase = 24;

constexpr CommandSpec kCommandSpecs[] = {
    {CommandKind::Check, "check", 0},
    {CommandKind::DumpAst, "dump-ast", 1, 0, 73},
    {CommandKind::DumpTypes, "dump-types", 3, 1, 72},
    {CommandKind::DumpProject, "dump-project", 2, 2, 0},
    {CommandKind::EmitIr, "emit-ir", 4, 3, 1},
    {CommandKind::EmitIrJson, "emit-ir-json", 5, 4, 2},
    {CommandKind::EmitNativeJson, "emit-native-json", 6, 5, 3, 0},
    {CommandKind::EmitExecutionPlan, "emit-execution-plan", 7, 6, 4, 1},
    {CommandKind::EmitExecutionJournal, "emit-execution-journal", 8, 7, 5, 2, 0},
    {CommandKind::EmitReplayView, "emit-replay-view", 9, 8, 6, 3, 1},
    {CommandKind::EmitAuditReport, "emit-audit-report", 10, 9, 7, 4, 2},
    {CommandKind::EmitSchedulerSnapshot, "emit-scheduler-snapshot", 11, 10, 8, 5, 3},
    {CommandKind::EmitCheckpointRecord, "emit-checkpoint-record", 12, 11, 9, 6, 4},
    {CommandKind::EmitCheckpointReview, "emit-checkpoint-review", 13, 12, 10, 7, 5},
    {CommandKind::EmitPersistenceDescriptor, "emit-persistence-descriptor", 14, 13, 11, 26, 6},
    {CommandKind::EmitPersistenceReview, "emit-persistence-review", 15, 14, 12, 27, 7},
    {CommandKind::EmitExportManifest, "emit-export-manifest", 16, 15, 13, 28, 8},
    {CommandKind::EmitExportReview, "emit-export-review", 17, 16, 14, 29, 9},
    {CommandKind::EmitStoreImportDescriptor, "emit-store-import-descriptor", 20, 17, 15, 30, 10},
    {CommandKind::EmitStoreImportReview, "emit-store-import-review", 21, 18, 16, 31, 11},
    {CommandKind::EmitDurableStoreImportRequest,
     "emit-durable-store-import-request",
     22,
     19,
     17,
     32,
     12},
    {CommandKind::EmitDurableStoreImportReview,
     "emit-durable-store-import-review",
     23,
     20,
     18,
     33,
     13},
    {CommandKind::EmitDurableStoreImportDecision,
     "emit-durable-store-import-decision",
     24,
     21,
     19,
     34,
     14},
    {CommandKind::EmitDurableStoreImportReceipt,
     "emit-durable-store-import-receipt",
     25,
     22,
     20,
     35,
     15},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceRequest,
     "emit-durable-store-import-receipt-persistence-request",
     26,
     23,
     21,
     36,
     16},
    {CommandKind::EmitDurableStoreImportDecisionReview,
     "emit-durable-store-import-decision-review",
     27,
     24,
     22,
     37,
     17},
    {CommandKind::EmitDurableStoreImportReceiptReview,
     "emit-durable-store-import-receipt-review",
     28,
     25,
     23,
     38,
     18},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceReview,
     "emit-durable-store-import-receipt-persistence-review",
     29,
     26,
     24,
     39,
     19},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceResponse,
     "emit-durable-store-import-receipt-persistence-response",
     30,
     27,
     25,
     40,
     20},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceResponseReview,
     "emit-durable-store-import-receipt-persistence-response-review",
     31,
     28,
     26,
     41,
     21},
    {CommandKind::EmitDurableStoreImportAdapterExecution,
     "emit-durable-store-import-adapter-execution",
     32,
     29,
     27,
     42,
     22},
    {CommandKind::EmitDurableStoreImportRecoveryPreview,
     "emit-durable-store-import-recovery-preview",
     33,
     30,
     28,
     43,
     23},
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind, command_kind, artifact_type, builder, printer, command_token, visibility, order)         \
    {CommandKind::command_kind,                                                                    \
     command_token,                                                                                \
     kProviderUsageBase + (order),                                                                 \
     kProviderActionBase + (order),                                                                \
     kProviderInferenceBase + (order),                                                             \
     kProviderPackageBase + (order),                                                               \
     kProviderCapabilityBase + (order)},
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT
    {CommandKind::EmitSchedulerReview, "emit-scheduler-review", 96, 93, 99, 8, 86},
    {CommandKind::EmitRuntimeSession, "emit-runtime-session", 97, 94, 100, 9, 87},
    {CommandKind::EmitDryRunTrace, "emit-dry-run-trace", 98, 95, 101, 10, 88},
    {CommandKind::EmitPackageReview, "emit-package-review", 99, 96, 102, 11},
    {CommandKind::EmitSummary, "emit-summary", 100, 97, 103},
    {CommandKind::EmitSmv, "emit-smv", 101, 98, 104},
    {CommandKind::EmitAssuranceJson, "emit-assurance-json", 102, 99, 105},
    {CommandKind::EmitK8sCrd, "emit-k8s-crd", 103, 100, 106},
    {CommandKind::EmitOpenApi, "emit-openapi", 104, 101, 107},
    {CommandKind::EmitTerraform, "emit-terraform", 105, 102, 108},
    {CommandKind::EmitWasm, "emit-wasm", 106, 103, 109},
    {CommandKind::ValidateAssurance, "validate-assurance", 107, 104, 110, 12},
    {CommandKind::VerifyFormal, "verify-formal", 108, 105, 111, 13},
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

/// Derive the canonical short token for a given old token.
[[nodiscard]] std::string derive_short_form(std::string_view old_token) {
    constexpr std::string_view kProviderPrefix = "emit-durable-store-import-provider-";
    constexpr std::string_view kDurableStorePrefix = "emit-durable-store-import-";
    constexpr std::string_view kEmitPrefix = "emit-";
    constexpr std::string_view kDumpPrefix = "dump-";
    constexpr std::string_view kVerifyPrefix = "verify-";
    constexpr std::string_view kValidatePrefix = "validate-";

    if (old_token.starts_with(kProviderPrefix)) {
        std::string result = "provider/";
        result += old_token.substr(kProviderPrefix.size());
        return result;
    }
    if (old_token.starts_with(kDurableStorePrefix)) {
        std::string result = "store/";
        result += old_token.substr(kDurableStorePrefix.size());
        return result;
    }
    // emit-store-import-X stays as "store-import-X" (plain top-level artifact)
    // to avoid collision with durable-store-import-X → store/X.
    if (old_token.starts_with(kEmitPrefix)) {
        return std::string(old_token.substr(kEmitPrefix.size()));
    }
    if (old_token.starts_with(kDumpPrefix)) {
        return std::string(old_token.substr(kDumpPrefix.size()));
    }
    if (old_token.starts_with(kVerifyPrefix)) {
        return std::string(old_token.substr(kVerifyPrefix.size()));
    }
    if (old_token.starts_with(kValidatePrefix)) {
        return std::string(old_token.substr(kValidatePrefix.size()));
    }
    return std::string(old_token);
}

/// Check whether `old_token` belongs to `group` and its short form equals `artifact_id`.
[[nodiscard]] bool
matches_artifact_id(std::string_view old_token, ActionGroup group, std::string_view artifact_id) {
    switch (group) {
    case ActionGroup::Emit: {
        constexpr std::string_view kProviderPrefix = "emit-durable-store-import-provider-";
        constexpr std::string_view kDurableStorePrefix = "emit-durable-store-import-";
        constexpr std::string_view kEmitPrefix = "emit-";

        if (artifact_id.starts_with("provider/")) {
            auto suffix = artifact_id.substr(9);
            return old_token.starts_with(kProviderPrefix) &&
                   old_token.substr(kProviderPrefix.size()) == suffix;
        }
        if (artifact_id.starts_with("store/")) {
            auto suffix = artifact_id.substr(6);
            // Only emit-durable-store-import-X maps to store/X.
            // emit-store-import-X stays as plain "store-import-X" (no domain prefix).
            return old_token.starts_with(kDurableStorePrefix) &&
                   old_token.substr(kDurableStorePrefix.size()) == suffix;
        }
        // Plain artifact-id (no domain prefix) — match "emit-{id}"
        return old_token.starts_with(kEmitPrefix) &&
               old_token.substr(kEmitPrefix.size()) == artifact_id;
    }
    case ActionGroup::Dump: {
        constexpr std::string_view kPrefix = "dump-";
        return old_token.starts_with(kPrefix) && old_token.substr(kPrefix.size()) == artifact_id;
    }
    case ActionGroup::Verify: {
        constexpr std::string_view kPrefix = "verify-";
        return old_token.starts_with(kPrefix) && old_token.substr(kPrefix.size()) == artifact_id;
    }
    case ActionGroup::Validate: {
        constexpr std::string_view kPrefix = "validate-";
        return old_token.starts_with(kPrefix) && old_token.substr(kPrefix.size()) == artifact_id;
    }
    }
    return false;
}

[[nodiscard]] bool is_hidden_provider_command(CommandKind command) {
    const auto visibility = provider_artifact_visibility_for_command(command);
    return visibility.has_value() && *visibility == ProviderArtifactVisibility::Internal;
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
    constexpr std::string_view kEmitPrefix = "emit-";
    auto token = command_name(command);
    if (!token.starts_with(kEmitPrefix)) {
        return std::nullopt;
    }

    token.remove_prefix(kEmitPrefix.size());
    return token;
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
    for (const auto &spec : kCommandSpecs) {
        if (matches_artifact_id(spec.token, group, artifact_id)) {
            if (!include_hidden && is_hidden_provider_command(spec.kind)) {
                continue;
            }
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
    return derive_short_form(spec->token);
}

} // namespace ahfl::cli
