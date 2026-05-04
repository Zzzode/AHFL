#include "ahfl/cli/command_catalog.hpp"

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
    {CommandKind::EmitDurableStoreImportProviderWriteAttempt,
     "emit-durable-store-import-provider-write-attempt",
     34,
     31,
     29,
     44,
     24},
    {CommandKind::EmitDurableStoreImportProviderRecoveryHandoff,
     "emit-durable-store-import-provider-recovery-handoff",
     35,
     32,
     30,
     45,
     25},
    {CommandKind::EmitDurableStoreImportProviderDriverBinding,
     "emit-durable-store-import-provider-driver-binding",
     36,
     33,
     31,
     46,
     26},
    {CommandKind::EmitDurableStoreImportProviderDriverReadiness,
     "emit-durable-store-import-provider-driver-readiness",
     37,
     34,
     32,
     47,
     27},
    {CommandKind::EmitDurableStoreImportProviderRuntimePreflight,
     "emit-durable-store-import-provider-runtime-preflight",
     38,
     35,
     33,
     48,
     28},
    {CommandKind::EmitDurableStoreImportProviderRuntimeReadiness,
     "emit-durable-store-import-provider-runtime-readiness",
     39,
     36,
     34,
     49,
     29},
    {CommandKind::EmitDurableStoreImportProviderSdkEnvelope,
     "emit-durable-store-import-provider-sdk-envelope",
     40,
     37,
     35,
     50,
     30},
    {CommandKind::EmitDurableStoreImportProviderSdkHandoffReadiness,
     "emit-durable-store-import-provider-sdk-handoff-readiness",
     41,
     38,
     36,
     51,
     31},
    {CommandKind::EmitDurableStoreImportProviderHostExecution,
     "emit-durable-store-import-provider-host-execution",
     42,
     39,
     37,
     52,
     32},
    {CommandKind::EmitDurableStoreImportProviderHostExecutionReadiness,
     "emit-durable-store-import-provider-host-execution-readiness",
     43,
     40,
     38,
     53,
     33},
    {CommandKind::EmitDurableStoreImportProviderLocalHostExecutionReceipt,
     "emit-durable-store-import-provider-local-host-execution-receipt",
     44,
     41,
     39,
     54,
     34},
    {CommandKind::EmitDurableStoreImportProviderLocalHostExecutionReceiptReview,
     "emit-durable-store-import-provider-local-host-execution-receipt-review",
     45,
     42,
     40,
     55,
     35},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterRequest,
     "emit-durable-store-import-provider-sdk-adapter-request",
     46,
     43,
     41,
     56,
     36},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterResponsePlaceholder,
     "emit-durable-store-import-provider-sdk-adapter-response-placeholder",
     47,
     44,
     42,
     57,
     37},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterReadiness,
     "emit-durable-store-import-provider-sdk-adapter-readiness",
     48,
     45,
     43,
     58,
     38},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterInterface,
     "emit-durable-store-import-provider-sdk-adapter-interface",
     49,
     46,
     44,
     59,
     39},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterInterfaceReview,
     "emit-durable-store-import-provider-sdk-adapter-interface-review",
     50,
     47,
     45,
     60,
     40},
    {CommandKind::EmitDurableStoreImportProviderConfigLoad,
     "emit-durable-store-import-provider-config-load",
     51,
     48,
     46,
     61,
     41},
    {CommandKind::EmitDurableStoreImportProviderConfigSnapshot,
     "emit-durable-store-import-provider-config-snapshot",
     52,
     49,
     47,
     62,
     42},
    {CommandKind::EmitDurableStoreImportProviderConfigReadiness,
     "emit-durable-store-import-provider-config-readiness",
     53,
     50,
     48,
     63,
     43},
    {CommandKind::EmitDurableStoreImportProviderSecretResolverRequest,
     "emit-durable-store-import-provider-secret-resolver-request",
     54,
     51,
     49,
     64,
     44},
    {CommandKind::EmitDurableStoreImportProviderSecretResolverResponse,
     "emit-durable-store-import-provider-secret-resolver-response",
     55,
     52,
     50,
     65,
     45},
    {CommandKind::EmitDurableStoreImportProviderSecretPolicyReview,
     "emit-durable-store-import-provider-secret-policy-review",
     56,
     53,
     51,
     66,
     46},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessRequest,
     "emit-durable-store-import-provider-local-host-harness-request",
     57,
     54,
     52,
     67,
     47},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessRecord,
     "emit-durable-store-import-provider-local-host-harness-record",
     58,
     55,
     53,
     68,
     48},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessReview,
     "emit-durable-store-import-provider-local-host-harness-review",
     59,
     56,
     54,
     69,
     49},
    {CommandKind::EmitDurableStoreImportProviderSdkPayloadPlan,
     "emit-durable-store-import-provider-sdk-payload-plan",
     60,
     57,
     55,
     70,
     50},
    {CommandKind::EmitDurableStoreImportProviderSdkPayloadAudit,
     "emit-durable-store-import-provider-sdk-payload-audit",
     61,
     58,
     56,
     71,
     51},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterContract,
     "emit-durable-store-import-provider-sdk-mock-adapter-contract",
     62,
     59,
     57,
     72,
     52},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterExecution,
     "emit-durable-store-import-provider-sdk-mock-adapter-execution",
     63,
     60,
     58,
     73,
     53},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterReadiness,
     "emit-durable-store-import-provider-sdk-mock-adapter-readiness",
     64,
     61,
     59,
     74,
     54},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaPlan,
     "emit-durable-store-import-provider-local-filesystem-alpha-plan",
     65,
     62,
     60,
     75,
     55},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaResult,
     "emit-durable-store-import-provider-local-filesystem-alpha-result",
     66,
     63,
     61,
     76,
     56},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaReadiness,
     "emit-durable-store-import-provider-local-filesystem-alpha-readiness",
     67,
     64,
     62,
     77,
     57},
    {CommandKind::EmitDurableStoreImportProviderWriteRetryDecision,
     "emit-durable-store-import-provider-write-retry-decision",
     68,
     65,
     63,
     78,
     58},
    {CommandKind::EmitDurableStoreImportProviderWriteCommitReceipt,
     "emit-durable-store-import-provider-write-commit-receipt",
     69,
     66,
     64,
     79,
     59},
    {CommandKind::EmitDurableStoreImportProviderWriteCommitReview,
     "emit-durable-store-import-provider-write-commit-review",
     70,
     67,
     65,
     80,
     60},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryCheckpoint,
     "emit-durable-store-import-provider-write-recovery-checkpoint",
     71,
     68,
     74,
     81,
     61},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryPlan,
     "emit-durable-store-import-provider-write-recovery-plan",
     72,
     69,
     75,
     82,
     62},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryReview,
     "emit-durable-store-import-provider-write-recovery-review",
     73,
     70,
     76,
     83,
     63},
    {CommandKind::EmitDurableStoreImportProviderFailureTaxonomyReport,
     "emit-durable-store-import-provider-failure-taxonomy-report",
     74,
     71,
     77,
     84,
     64},
    {CommandKind::EmitDurableStoreImportProviderFailureTaxonomyReview,
     "emit-durable-store-import-provider-failure-taxonomy-review",
     75,
     72,
     78,
     85,
     65},
    {CommandKind::EmitDurableStoreImportProviderExecutionAuditEvent,
     "emit-durable-store-import-provider-execution-audit-event",
     76,
     73,
     79,
     86,
     66},
    {CommandKind::EmitDurableStoreImportProviderTelemetrySummary,
     "emit-durable-store-import-provider-telemetry-summary",
     77,
     74,
     80,
     87,
     67},
    {CommandKind::EmitDurableStoreImportProviderOperatorReviewEvent,
     "emit-durable-store-import-provider-operator-review-event",
     78,
     75,
     81,
     88,
     68},
    {CommandKind::EmitSchedulerReview, "emit-scheduler-review", 79, 76, 82, 8, 69},
    {CommandKind::EmitRuntimeSession, "emit-runtime-session", 80, 77, 83, 9, 70},
    {CommandKind::EmitDryRunTrace, "emit-dry-run-trace", 81, 78, 84, 10, 71},
    {CommandKind::EmitPackageReview, "emit-package-review", 82, 79, 85, 11},
    {CommandKind::EmitSummary, "emit-summary", 83, 80, 86},
    {CommandKind::EmitSmv, "emit-smv", 84, 81, 87},
};

[[nodiscard]] const CommandSpec *find_command_spec(CommandKind command) {
    for (const auto &spec : kCommandSpecs) {
        if (spec.kind == command) {
            return &spec;
        }
    }

    return nullptr;
}

[[nodiscard]] const CommandSpec *find_command_spec(std::string_view token) {
    for (const auto &spec : kCommandSpecs) {
        if (spec.token == token) {
            return &spec;
        }
    }

    return nullptr;
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

[[nodiscard]] std::string format_pipe_separated_commands(std::span<const CommandKind> commands) {
    std::string result;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (index > 0) {
            result += "|";
        }
        result += command_name(commands[index]);
    }
    return result;
}

[[nodiscard]] std::string_view usage_suffix_for_command(CommandKind command) {
    if (command == CommandKind::Check) {
        return "[--search-root <dir>]... [--dump-ast] <input.ahfl>";
    }

    if (command == CommandKind::DumpProject) {
        return "[--search-root <dir>]... <entry.ahfl>";
    }

    if (is_capability_input_supported_command(command)) {
        return "--package <ahfl.package.json> --capability-mocks <mocks.json> "
               "--input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
               "[--search-root <dir>]... <input.ahfl>";
    }

    if (is_package_supported_command(command)) {
        return "[--package <ahfl.package.json>] [--search-root <dir>]... <input.ahfl>";
    }

    return "[--search-root <dir>]... <input.ahfl>";
}

void print_usage_line(std::ostream &out, CommandKind command) {
    out << "  ahflc " << command_name(command) << " " << usage_suffix_for_command(command) << '\n';
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

[[nodiscard]] std::string format_comma_or_commands(std::span<const CommandKind> commands) {
    std::string result;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (index > 0) {
            result += (index + 1 == commands.size()) ? ", or " : ", ";
        }
        result += command_name(commands[index]);
    }
    return result;
}

[[nodiscard]] std::optional<CommandKind> command_token_to_kind(std::string_view argument) {
    if (const auto *const spec = find_command_spec(argument); spec != nullptr) {
        return spec->kind;
    }

    return std::nullopt;
}

[[nodiscard]] std::string_view command_name(CommandKind command) {
    if (const auto *const spec = find_command_spec(command); spec != nullptr) {
        return spec->token;
    }

    return "check";
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
selected_backend_for_command(std::optional<CommandKind> command) {
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
    default:
        return std::nullopt;
    }
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

void print_usage(std::ostream &out) {
    const auto project_aware_commands =
        format_pipe_separated_commands(command_list(CommandListKind::UsageProjectAware));
    out << "Usage:\n"
        << "  ahflc <" << project_aware_commands
        << "> [--package <ahfl.package.json>] --project <ahfl.project.json>\n"
        << "  ahflc <" << project_aware_commands
        << "> [--package <ahfl.package.json>] --workspace <ahfl.workspace.json> --project-name "
           "<name>\n";

    print_usage_line(out, CommandKind::Check);
    for (const auto command : command_list(CommandListKind::Action)) {
        print_usage_line(out, command);
    }
    out << "  ahflc [--dump-ast] <input.ahfl>\n";
}

} // namespace ahfl::cli
