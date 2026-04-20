#include "ahfl/backends/driver.hpp"
#include "ahfl/audit_report/report.hpp"
#include "ahfl/backends/checkpoint_record.hpp"
#include "ahfl/backends/checkpoint_review.hpp"
#include "ahfl/backends/durable_store_import_request.hpp"
#include "ahfl/backends/durable_store_import_decision.hpp"
#include "ahfl/backends/durable_store_import_receipt.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_request.hpp"
#include "ahfl/backends/durable_store_import_decision_review.hpp"
#include "ahfl/backends/durable_store_import_receipt_persistence_review.hpp"
#include "ahfl/backends/durable_store_import_receipt_review.hpp"
#include "ahfl/backends/durable_store_import_review.hpp"
#include "ahfl/backends/audit_report.hpp"
#include "ahfl/backends/dry_run_trace.hpp"
#include "ahfl/backends/execution_plan.hpp"
#include "ahfl/backends/execution_journal.hpp"
#include "ahfl/backends/persistence_descriptor.hpp"
#include "ahfl/backends/persistence_export_manifest.hpp"
#include "ahfl/backends/persistence_export_review.hpp"
#include "ahfl/backends/persistence_review.hpp"
#include "ahfl/backends/replay_view.hpp"
#include "ahfl/backends/scheduler_review.hpp"
#include "ahfl/backends/runtime_session.hpp"
#include "ahfl/backends/scheduler_snapshot.hpp"
#include "ahfl/backends/store_import_descriptor.hpp"
#include "ahfl/backends/store_import_review.hpp"
#include "ahfl/checkpoint_record/record.hpp"
#include "ahfl/checkpoint_record/review.hpp"
#include "ahfl/dry_run/runner.hpp"
#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/review.hpp"
#include "ahfl/execution_journal/journal.hpp"
#include "ahfl/frontend/frontend.hpp"
#include "ahfl/persistence_descriptor/descriptor.hpp"
#include "ahfl/persistence_descriptor/review.hpp"
#include "ahfl/persistence_export/manifest.hpp"
#include "ahfl/persistence_export/review.hpp"
#include "ahfl/replay_view/replay.hpp"
#include "ahfl/runtime_session/session.hpp"
#include "ahfl/scheduler_snapshot/review.hpp"
#include "ahfl/scheduler_snapshot/snapshot.hpp"
#include "ahfl/semantics/resolver.hpp"
#include "ahfl/semantics/typecheck.hpp"
#include "ahfl/semantics/validate.hpp"
#include "ahfl/store_import/descriptor.hpp"
#include "ahfl/store_import/review.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using MaybeSourceFile = std::optional<std::reference_wrapper<const ahfl::SourceFile>>;

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

[[nodiscard]] std::string_view command_name(CommandKind command);

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

enum class CommandListKind {
    UsageProjectAware,
    Action,
    Inference,
    PackageSupported,
    CapabilityInputSupported,
};

constexpr CommandSpec kCommandSpecs[] = {
    {CommandKind::Check, "check", 0},
    {CommandKind::DumpAst, "dump-ast", 1, 0, 32},
    {CommandKind::DumpTypes, "dump-types", 3, 1, 31},
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
    {CommandKind::EmitDurableStoreImportReview, "emit-durable-store-import-review", 23, 20, 18, 33, 13},
    {CommandKind::EmitDurableStoreImportDecision,
     "emit-durable-store-import-decision",
     24,
     21,
     19,
     34,
     14},
    {CommandKind::EmitDurableStoreImportReceipt, "emit-durable-store-import-receipt", 25, 22, 20, 35, 15},
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
    {CommandKind::EmitSchedulerReview, "emit-scheduler-review", 30, 27, 25, 8, 20},
    {CommandKind::EmitRuntimeSession, "emit-runtime-session", 18, 28, 26, 9, 21},
    {CommandKind::EmitDryRunTrace, "emit-dry-run-trace", 19, 29, 27, 10, 22},
    {CommandKind::EmitPackageReview, "emit-package-review", 31, 30, 28, 11},
    {CommandKind::EmitSummary, "emit-summary", 32, 31, 29},
    {CommandKind::EmitSmv, "emit-smv", 33, 32, 30},
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

[[nodiscard]] int command_list_order(const CommandSpec &spec, CommandListKind list_kind) {
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

[[nodiscard]] std::vector<CommandKind> build_command_list(CommandListKind list_kind) {
    std::vector<std::pair<int, CommandKind>> ordered_commands;
    ordered_commands.reserve(std::size(kCommandSpecs));
    for (const auto &spec : kCommandSpecs) {
        if (const auto order = command_list_order(spec, list_kind); order != kNotListed) {
            ordered_commands.emplace_back(order, spec.kind);
        }
    }

    std::sort(ordered_commands.begin(), ordered_commands.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.first < rhs.first;
    });

    std::vector<CommandKind> commands;
    commands.reserve(ordered_commands.size());
    for (const auto &[_, command] : ordered_commands) {
        commands.push_back(command);
    }
    return commands;
}

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
           "<name>\n"
        << "  ahflc check [--search-root <dir>]... [--dump-ast] <input.ahfl>\n"
        << "  ahflc dump-ast [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-types [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc dump-project [--search-root <dir>]... <entry.ahfl>\n"
        << "  ahflc emit-ir [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-ir-json [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-native-json [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-execution-plan [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-execution-journal --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-replay-view --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-audit-report --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-scheduler-snapshot --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-checkpoint-record --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-checkpoint-review --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-persistence-descriptor --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-persistence-review --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-export-manifest --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-export-review --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-store-import-descriptor --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-store-import-review --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-request --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-review --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-decision --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-receipt --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-decision-review --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-receipt-review --package <ahfl.package.json> "
           "--capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] "
           "[--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-receipt-persistence-request --package "
           "<ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> "
           "[--workflow <canonical>] [--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-durable-store-import-receipt-persistence-review --package "
           "<ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> "
           "[--workflow <canonical>] [--run-id <id>] [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-scheduler-review --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-runtime-session --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-dry-run-trace --package <ahfl.package.json> --capability-mocks "
           "<mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] "
           "[--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-package-review [--package <ahfl.package.json>] [--search-root <dir>]... "
           "<input.ahfl>\n"
        << "  ahflc emit-summary [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc emit-smv [--search-root <dir>]... <input.ahfl>\n"
        << "  ahflc [--dump-ast] <input.ahfl>\n";
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

[[nodiscard]] bool is_command_requiring_package(CommandKind command) {
    return is_capability_input_supported_command(command);
}

void set_command_option(CommandLineOptions &options, CommandKind command) {
    options.selected_command = command;
}

[[nodiscard]] std::optional<ahfl::BackendKind>
selected_backend(std::optional<CommandKind> command) {
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

[[nodiscard]] ahfl::handoff::ExecutableKind
to_executable_kind(ahfl::PackageAuthoringTargetKind kind) {
    switch (kind) {
    case ahfl::PackageAuthoringTargetKind::Agent:
        return ahfl::handoff::ExecutableKind::Agent;
    case ahfl::PackageAuthoringTargetKind::Workflow:
        return ahfl::handoff::ExecutableKind::Workflow;
    }

    return ahfl::handoff::ExecutableKind::Workflow;
}

[[nodiscard]] ahfl::handoff::PackageMetadata
lower_package_metadata(const ahfl::PackageAuthoringDescriptor &descriptor) {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = std::string(ahfl::handoff::kFormatVersion),
        .name = descriptor.package_name,
        .version = descriptor.package_version,
    };
    metadata.entry_target = ahfl::handoff::ExecutableRef{
        .kind = to_executable_kind(descriptor.entry.kind),
        .canonical_name = descriptor.entry.name,
    };
    metadata.export_targets.reserve(descriptor.exports.size());
    for (const auto &target : descriptor.exports) {
        metadata.export_targets.push_back(ahfl::handoff::ExecutableRef{
            .kind = to_executable_kind(target.kind),
            .canonical_name = target.name,
        });
    }
    for (const auto &binding : descriptor.capability_bindings) {
        metadata.capability_binding_keys.emplace(binding.capability, binding.binding_key);
    }
    return metadata;
}

template <typename InputT>
[[nodiscard]] bool emit_selected_backend(std::optional<CommandKind> effective_command,
                                         const InputT &input,
                                         const ahfl::ResolveResult &resolve_result,
                                         const ahfl::TypeCheckResult &type_check_result,
                                         const ahfl::handoff::PackageMetadata *package_metadata,
                                         std::ostream &out) {
    const auto backend = selected_backend(effective_command);
    if (!backend.has_value()) {
        return false;
    }

    ahfl::emit_backend(
        *backend, input, resolve_result, type_check_result, out, package_metadata);
    return true;
}

[[nodiscard]] std::optional<std::string> to_owned_string(
    std::optional<std::string_view> value) {
    return value.transform([](std::string_view text) { return std::string(text); });
}

[[nodiscard]] std::optional<ahfl::handoff::ExecutionPlan>
build_execution_plan_for_cli(const ahfl::ir::Program &program,
                             const ahfl::handoff::PackageMetadata &metadata) {
    auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return std::nullopt;
    }

    return std::move(*plan_result.plan);
}

[[nodiscard]] std::optional<ahfl::dry_run::DryRunRequest>
build_dry_run_request_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const CommandLineOptions &options,
                              std::string_view command_name) {
    auto workflow_name = to_owned_string(options.workflow_name);
    if (!workflow_name.has_value()) {
        workflow_name = plan.entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: " << command_name
                  << " requires --workflow or package workflow entry\n";
        return std::nullopt;
    }

    return ahfl::dry_run::DryRunRequest{
        .workflow_canonical_name = std::move(*workflow_name),
        .input_fixture = std::string(*options.input_fixture),
        .run_id = to_owned_string(options.run_id),
    };
}

[[nodiscard]] std::optional<ahfl::runtime_session::RuntimeSession>
build_runtime_session_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                              const ahfl::dry_run::DryRunRequest &request,
                              const ahfl::dry_run::CapabilityMockSet &mock_set) {
    auto session_result = ahfl::runtime_session::build_runtime_session(plan, request, mock_set);
    session_result.diagnostics.render(std::cerr);
    if (session_result.has_errors() || !session_result.session.has_value()) {
        return std::nullopt;
    }

    return std::move(*session_result.session);
}

[[nodiscard]] std::optional<ahfl::execution_journal::ExecutionJournal>
build_execution_journal_for_cli(const ahfl::runtime_session::RuntimeSession &session) {
    auto journal_result = ahfl::execution_journal::build_execution_journal(session);
    journal_result.diagnostics.render(std::cerr);
    if (journal_result.has_errors() || !journal_result.journal.has_value()) {
        return std::nullopt;
    }

    return std::move(*journal_result.journal);
}

[[nodiscard]] std::optional<ahfl::replay_view::ReplayView> build_replay_view_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal) {
    auto replay_result = ahfl::replay_view::build_replay_view(plan, session, journal);
    replay_result.diagnostics.render(std::cerr);
    if (replay_result.has_errors() || !replay_result.replay.has_value()) {
        return std::nullopt;
    }

    return std::move(*replay_result.replay);
}

[[nodiscard]] std::optional<ahfl::scheduler_snapshot::SchedulerSnapshot>
build_scheduler_snapshot_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                 const ahfl::runtime_session::RuntimeSession &session,
                                 const ahfl::execution_journal::ExecutionJournal &journal,
                                 const ahfl::replay_view::ReplayView &replay) {
    auto snapshot_result =
        ahfl::scheduler_snapshot::build_scheduler_snapshot(plan, session, journal, replay);
    snapshot_result.diagnostics.render(std::cerr);
    if (snapshot_result.has_errors() || !snapshot_result.snapshot.has_value()) {
        return std::nullopt;
    }

    return std::move(*snapshot_result.snapshot);
}

[[nodiscard]] std::optional<ahfl::checkpoint_record::CheckpointRecord>
build_checkpoint_record_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                const ahfl::runtime_session::RuntimeSession &session,
                                const ahfl::execution_journal::ExecutionJournal &journal,
                                const ahfl::replay_view::ReplayView &replay,
                                const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot) {
    auto record_result =
        ahfl::checkpoint_record::build_checkpoint_record(plan, session, journal, replay, snapshot);
    record_result.diagnostics.render(std::cerr);
    if (record_result.has_errors() || !record_result.record.has_value()) {
        return std::nullopt;
    }

    return std::move(*record_result.record);
}

[[nodiscard]] std::optional<ahfl::persistence_descriptor::CheckpointPersistenceDescriptor>
build_persistence_descriptor_for_cli(const ahfl::handoff::ExecutionPlan &plan,
                                     const ahfl::runtime_session::RuntimeSession &session,
                                     const ahfl::execution_journal::ExecutionJournal &journal,
                                     const ahfl::replay_view::ReplayView &replay,
                                     const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
                                     const ahfl::checkpoint_record::CheckpointRecord &record) {
    auto descriptor_result = ahfl::persistence_descriptor::build_persistence_descriptor(
        plan, session, journal, replay, snapshot, record);
    descriptor_result.diagnostics.render(std::cerr);
    if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
        return std::nullopt;
    }

    return std::move(*descriptor_result.descriptor);
}

[[nodiscard]] std::optional<ahfl::persistence_export::PersistenceExportManifest>
build_export_manifest_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal,
    const ahfl::replay_view::ReplayView &replay,
    const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
    const ahfl::checkpoint_record::CheckpointRecord &record,
    const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor &descriptor) {
    auto manifest_result = ahfl::persistence_export::build_persistence_export_manifest(
        plan, session, journal, replay, snapshot, record, descriptor);
    manifest_result.diagnostics.render(std::cerr);
    if (manifest_result.has_errors() || !manifest_result.manifest.has_value()) {
        return std::nullopt;
    }

    return std::move(*manifest_result.manifest);
}

[[nodiscard]] std::optional<ahfl::store_import::StoreImportDescriptor>
build_store_import_descriptor_for_cli(
    const ahfl::handoff::ExecutionPlan &plan,
    const ahfl::runtime_session::RuntimeSession &session,
    const ahfl::execution_journal::ExecutionJournal &journal,
    const ahfl::replay_view::ReplayView &replay,
    const ahfl::scheduler_snapshot::SchedulerSnapshot &snapshot,
    const ahfl::checkpoint_record::CheckpointRecord &record,
    const ahfl::persistence_descriptor::CheckpointPersistenceDescriptor &descriptor,
    const ahfl::persistence_export::PersistenceExportManifest &manifest) {
    auto store_import_result = ahfl::store_import::build_store_import_descriptor(
        plan, session, journal, replay, snapshot, record, descriptor, manifest);
    store_import_result.diagnostics.render(std::cerr);
    if (store_import_result.has_errors() || !store_import_result.descriptor.has_value()) {
        return std::nullopt;
    }

    return std::move(*store_import_result.descriptor);
}

[[nodiscard]] int emit_execution_plan_with_diagnostics(const ahfl::ir::Program &program,
                                                       const ahfl::handoff::PackageMetadata &metadata) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    ahfl::print_execution_plan_json(*plan, std::cout);
    return 0;
}

[[nodiscard]] bool read_text_file(const std::filesystem::path &path,
                                  std::string &content,
                                  std::ostream &diagnostics) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics << "error: failed to open capability mock descriptor: "
                    << ahfl::display_path(path) << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    content = buffer.str();
    return true;
}

[[nodiscard]] int emit_dry_run_trace_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-dry-run-trace");
    if (!request.has_value()) {
        return 1;
    }

    const auto dry_run = ahfl::dry_run::run_local_dry_run(*plan, *request, mock_set);
    dry_run.diagnostics.render(std::cerr);
    if (dry_run.has_errors() || !dry_run.trace.has_value()) {
        return 1;
    }

    ahfl::print_dry_run_trace_json(*dry_run.trace, std::cout);
    return 0;
}

[[nodiscard]] int emit_runtime_session_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-runtime-session");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    ahfl::print_runtime_session_json(*session, std::cout);
    return 0;
}

[[nodiscard]] int emit_execution_journal_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-execution-journal");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    ahfl::print_execution_journal_json(*journal, std::cout);
    return 0;
}

[[nodiscard]] int emit_replay_view_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-replay-view");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    ahfl::print_replay_view_json(*replay, std::cout);
    return 0;
}

[[nodiscard]] int emit_audit_report_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-audit-report");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto trace = ahfl::dry_run::run_local_dry_run(*plan, *request, mock_set);
    trace.diagnostics.render(std::cerr);
    if (trace.has_errors() || !trace.trace.has_value()) {
        return 1;
    }

    const auto report = ahfl::audit_report::build_audit_report(
        *plan, *session, *journal, *trace.trace);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return 1;
    }

    ahfl::print_audit_report_json(*report.report, std::cout);
    return 0;
}

[[nodiscard]] int emit_scheduler_snapshot_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-scheduler-snapshot");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    ahfl::print_scheduler_snapshot_json(*snapshot, std::cout);
    return 0;
}

[[nodiscard]] int emit_scheduler_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-scheduler-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto summary =
        ahfl::scheduler_snapshot::build_scheduler_decision_summary(*snapshot);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_scheduler_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_checkpoint_record_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-checkpoint-record");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    ahfl::print_checkpoint_record_json(*record, std::cout);
    return 0;
}

[[nodiscard]] int emit_checkpoint_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-checkpoint-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto summary = ahfl::checkpoint_record::build_checkpoint_review_summary(*record);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_checkpoint_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_persistence_descriptor_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-persistence-descriptor");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    ahfl::print_persistence_descriptor_json(*descriptor, std::cout);
    return 0;
}

[[nodiscard]] int emit_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-persistence-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    const auto summary =
        ahfl::persistence_descriptor::build_persistence_review_summary(*descriptor);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return 1;
    }

    ahfl::print_persistence_review(*summary.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_export_manifest_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-export-manifest");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    const auto manifest = build_export_manifest_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor);
    if (!manifest.has_value()) {
        return 1;
    }

    ahfl::print_persistence_export_manifest_json(*manifest, std::cout);
    return 0;
}

[[nodiscard]] int emit_export_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-export-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    const auto manifest = build_export_manifest_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor);
    if (!manifest.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::persistence_export::build_persistence_export_review_summary(*manifest);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_persistence_export_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_store_import_descriptor_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-store-import-descriptor");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    const auto manifest = build_export_manifest_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor);
    if (!manifest.has_value()) {
        return 1;
    }

    const auto store_import_descriptor = build_store_import_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor, *manifest);
    if (!store_import_descriptor.has_value()) {
        return 1;
    }

    ahfl::print_store_import_descriptor_json(*store_import_descriptor, std::cout);
    return 0;
}

[[nodiscard]] int emit_store_import_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto plan = build_execution_plan_for_cli(program, metadata);
    if (!plan.has_value()) {
        return 1;
    }

    const auto request =
        build_dry_run_request_for_cli(*plan, options, "emit-store-import-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto session = build_runtime_session_for_cli(*plan, *request, mock_set);
    if (!session.has_value()) {
        return 1;
    }

    const auto journal = build_execution_journal_for_cli(*session);
    if (!journal.has_value()) {
        return 1;
    }

    const auto replay = build_replay_view_for_cli(*plan, *session, *journal);
    if (!replay.has_value()) {
        return 1;
    }

    const auto snapshot = build_scheduler_snapshot_for_cli(*plan, *session, *journal, *replay);
    if (!snapshot.has_value()) {
        return 1;
    }

    const auto record =
        build_checkpoint_record_for_cli(*plan, *session, *journal, *replay, *snapshot);
    if (!record.has_value()) {
        return 1;
    }

    const auto descriptor = build_persistence_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record);
    if (!descriptor.has_value()) {
        return 1;
    }

    const auto manifest = build_export_manifest_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor);
    if (!manifest.has_value()) {
        return 1;
    }

    const auto store_import_descriptor = build_store_import_descriptor_for_cli(
        *plan, *session, *journal, *replay, *snapshot, *record, *descriptor, *manifest);
    if (!store_import_descriptor.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::store_import::build_store_import_review_summary(*store_import_descriptor);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_store_import_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportRequest>
build_durable_store_import_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan_result = ahfl::handoff::build_execution_plan(
        ahfl::handoff::lower_package(program, metadata));
    plan_result.diagnostics.render(std::cerr);
    if (plan_result.has_errors() || !plan_result.plan.has_value()) {
        return std::nullopt;
    }

    auto workflow_name =
        options.workflow_name.transform([](std::string_view value) { return std::string(value); });
    if (!workflow_name.has_value()) {
        workflow_name = plan_result.plan->entry_workflow_canonical_name;
    }

    if (!workflow_name.has_value()) {
        std::cerr << "error: " << command_name
                  << " requires --workflow or package workflow entry\n";
        return std::nullopt;
    }

    const auto session = ahfl::runtime_session::build_runtime_session(
        *plan_result.plan,
        ahfl::dry_run::DryRunRequest{
            .workflow_canonical_name = std::move(*workflow_name),
            .input_fixture = std::string(*options.input_fixture),
            .run_id =
                options.run_id.transform([](std::string_view value) { return std::string(value); }),
        },
        mock_set);
    session.diagnostics.render(std::cerr);
    if (session.has_errors() || !session.session.has_value()) {
        return std::nullopt;
    }

    const auto journal = ahfl::execution_journal::build_execution_journal(*session.session);
    journal.diagnostics.render(std::cerr);
    if (journal.has_errors() || !journal.journal.has_value()) {
        return std::nullopt;
    }

    const auto replay =
        ahfl::replay_view::build_replay_view(*plan_result.plan, *session.session, *journal.journal);
    replay.diagnostics.render(std::cerr);
    if (replay.has_errors() || !replay.replay.has_value()) {
        return std::nullopt;
    }

    const auto snapshot = ahfl::scheduler_snapshot::build_scheduler_snapshot(
        *plan_result.plan, *session.session, *journal.journal, *replay.replay);
    snapshot.diagnostics.render(std::cerr);
    if (snapshot.has_errors() || !snapshot.snapshot.has_value()) {
        return std::nullopt;
    }

    const auto record = ahfl::checkpoint_record::build_checkpoint_record(
        *plan_result.plan, *session.session, *journal.journal, *replay.replay, *snapshot.snapshot);
    record.diagnostics.render(std::cerr);
    if (record.has_errors() || !record.record.has_value()) {
        return std::nullopt;
    }

    const auto descriptor = ahfl::persistence_descriptor::build_persistence_descriptor(
        *plan_result.plan,
        *session.session,
        *journal.journal,
        *replay.replay,
        *snapshot.snapshot,
        *record.record);
    descriptor.diagnostics.render(std::cerr);
    if (descriptor.has_errors() || !descriptor.descriptor.has_value()) {
        return std::nullopt;
    }

    const auto manifest = ahfl::persistence_export::build_persistence_export_manifest(
        *plan_result.plan,
        *session.session,
        *journal.journal,
        *replay.replay,
        *snapshot.snapshot,
        *record.record,
        *descriptor.descriptor);
    manifest.diagnostics.render(std::cerr);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        return std::nullopt;
    }

    const auto store_import_descriptor = ahfl::store_import::build_store_import_descriptor(
        *plan_result.plan,
        *session.session,
        *journal.journal,
        *replay.replay,
        *snapshot.snapshot,
        *record.record,
        *descriptor.descriptor,
        *manifest.manifest);
    store_import_descriptor.diagnostics.render(std::cerr);
    if (store_import_descriptor.has_errors() ||
        !store_import_descriptor.descriptor.has_value()) {
        return std::nullopt;
    }

    const auto request = ahfl::durable_store_import::build_durable_store_import_request(
        *store_import_descriptor.descriptor);
    request.diagnostics.render(std::cerr);
    if (request.has_errors() || !request.request.has_value()) {
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecision>
build_durable_store_import_decision_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request =
        build_durable_store_import_request_for_cli(program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = ahfl::durable_store_import::build_durable_store_import_decision(*request);
    decision.diagnostics.render(std::cerr);
    if (decision.has_errors() || !decision.decision.has_value()) {
        return std::nullopt;
    }

    return *decision.decision;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::DurableStoreImportDecisionReceipt>
build_durable_store_import_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto decision =
        build_durable_store_import_decision_for_cli(program, metadata, mock_set, options, command_name);
    if (!decision.has_value()) {
        return std::nullopt;
    }

    const auto receipt =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt(*decision);
    receipt.diagnostics.render(std::cerr);
    if (receipt.has_errors() || !receipt.receipt.has_value()) {
        return std::nullopt;
    }

    return *receipt.receipt;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceRequest>
build_durable_store_import_receipt_persistence_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto request = ahfl::durable_store_import::
        build_durable_store_import_decision_receipt_persistence_request(*receipt);
    request.diagnostics.render(std::cerr);
    if (request.has_errors() || !request.request.has_value()) {
        return std::nullopt;
    }

    return *request.request;
}

[[nodiscard]] int emit_durable_store_import_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_request_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-request");
    if (!request.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_request_json(*request, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_request_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-review");
    if (!request.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_review(*review.summary, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_decision_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_durable_store_import_decision_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-decision");
    if (!decision.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_decision_json(*decision, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_receipt_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-receipt");
    if (!receipt.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_json(*receipt, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_request_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto request = build_durable_store_import_receipt_persistence_request_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-request");
    if (!request.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_persistence_request_json(*request, std::cout);
    return 0;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptReviewSummary>
build_durable_store_import_receipt_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto receipt = build_durable_store_import_receipt_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!receipt.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_receipt_review_summary(
            *receipt);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_receipt_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_review(*review, std::cout);
    return 0;
}

[[nodiscard]] std::optional<
    ahfl::durable_store_import::DurableStoreImportDecisionReceiptPersistenceReviewSummary>
build_durable_store_import_receipt_persistence_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto request = build_durable_store_import_receipt_persistence_request_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::
            build_durable_store_import_decision_receipt_persistence_review_summary(*request);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return std::nullopt;
    }

    return *review.summary;
}

[[nodiscard]] int emit_durable_store_import_receipt_persistence_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto review = build_durable_store_import_receipt_persistence_review_for_cli(
        program,
        metadata,
        mock_set,
        options,
        "emit-durable-store-import-receipt-persistence-review");
    if (!review.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_receipt_persistence_review(*review, std::cout);
    return 0;
}

[[nodiscard]] int emit_durable_store_import_decision_review_with_diagnostics(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options) {
    const auto decision = build_durable_store_import_decision_for_cli(
        program, metadata, mock_set, options, "emit-durable-store-import-decision-review");
    if (!decision.has_value()) {
        return 1;
    }

    const auto review =
        ahfl::durable_store_import::build_durable_store_import_decision_review_summary(*decision);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.summary.has_value()) {
        return 1;
    }

    ahfl::print_durable_store_import_decision_review(*review.summary, std::cout);
    return 0;
}

template <typename ResultT>
void render_diagnostics(const ResultT &result, MaybeSourceFile source_file, std::ostream &out) {
    if (source_file.has_value()) {
        result.diagnostics.render(out, *source_file);
        return;
    }

    result.diagnostics.render(out);
}

void dump_ast_outline(const ahfl::ast::Program &program, std::ostream &out) {
    ahfl::dump_program_outline(program, out);
}

void dump_ast_outline(const ahfl::SourceGraph &graph, std::ostream &out) {
    ahfl::dump_project_ast_outline(graph, out);
}

void print_success_summary(const ahfl::ast::Program &program,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << program.declarations.size() << " top-level declaration(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references.size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

void print_success_summary(const ahfl::SourceGraph &graph,
                           const ahfl::ResolveResult &resolve_result,
                           const ahfl::TypeCheckResult &type_check_result,
                           std::ostream &out) {
    out << "ok: checked " << graph.sources.size() << " source(s), "
        << resolve_result.symbol_table.symbols().size() << " symbol(s), "
        << resolve_result.references.size() << " reference(s), "
        << type_check_result.environment.structs().size() +
               type_check_result.environment.enums().size()
        << " named type(s)\n";
}

[[nodiscard]] int load_project_input(const CommandLineOptions &options,
                                     const ahfl::Frontend &frontend,
                                     ahfl::ProjectInput &input) {
    if (options.project_descriptor.has_value()) {
        auto descriptor_result =
            frontend.load_project_descriptor(std::string(*options.project_descriptor));
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    if (options.workspace_descriptor.has_value()) {
        auto descriptor_result = frontend.load_project_descriptor_from_workspace(
            std::string(*options.workspace_descriptor), *options.project_name);
        descriptor_result.diagnostics.render(std::cerr);
        if (descriptor_result.has_errors() || !descriptor_result.descriptor.has_value()) {
            return descriptor_result.has_errors() ? 1 : 0;
        }

        input.entry_files = descriptor_result.descriptor->entry_files;
        input.search_roots = descriptor_result.descriptor->search_roots;
        return -1;
    }

    input.entry_files.push_back(std::string(options.positional.front()));
    input.search_roots.reserve(options.search_roots.size());
    for (const auto search_root : options.search_roots) {
        input.search_roots.push_back(std::string(search_root));
    }

    return -1;
}

template <typename InputT>
[[nodiscard]] int run_analysis_pipeline(const CommandLineOptions &options,
                                        std::optional<CommandKind> effective_command,
                                        const InputT &input,
                                        MaybeSourceFile source_file,
                                        const ahfl::handoff::PackageMetadata *package_metadata,
                                        const ahfl::dry_run::CapabilityMockSet *capability_mock_set) {
    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(input);
    render_diagnostics(resolve_result, source_file, std::cerr);
    if (resolve_result.has_errors()) {
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(input, resolve_result);
    render_diagnostics(type_check_result, source_file, std::cerr);

    if (is_action_enabled(options, CommandKind::DumpTypes)) {
        ahfl::dump_type_environment(
            type_check_result.environment, resolve_result.symbol_table, std::cout);
    }

    if (type_check_result.has_errors()) {
        return 1;
    }

    const ahfl::Validator validator;
    auto validation_result = validator.validate(input, resolve_result, type_check_result);
    render_diagnostics(validation_result, source_file, std::cerr);
    if (validation_result.has_errors()) {
        return 1;
    }

    if (package_metadata != nullptr) {
        auto ir_program = ahfl::lower_program_ir(input, resolve_result, type_check_result);
        auto metadata_validation =
            ahfl::handoff::validate_package_metadata(ir_program, *package_metadata);
        render_diagnostics(metadata_validation, std::nullopt, std::cerr);
        if (metadata_validation.has_errors()) {
            return 1;
        }

        if (effective_command.has_value()) {
            switch (*effective_command) {
            case CommandKind::EmitDryRunTrace:
                return emit_dry_run_trace_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitExecutionJournal:
                return emit_execution_journal_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitReplayView:
                return emit_replay_view_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitAuditReport:
                return emit_audit_report_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitSchedulerSnapshot:
                return emit_scheduler_snapshot_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitCheckpointRecord:
                return emit_checkpoint_record_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitCheckpointReview:
                return emit_checkpoint_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitPersistenceDescriptor:
                return emit_persistence_descriptor_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitPersistenceReview:
                return emit_persistence_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitExportManifest:
                return emit_export_manifest_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitExportReview:
                return emit_export_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitStoreImportDescriptor:
                return emit_store_import_descriptor_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitStoreImportReview:
                return emit_store_import_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportRequest:
                return emit_durable_store_import_request_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportReview:
                return emit_durable_store_import_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportDecision:
                return emit_durable_store_import_decision_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportReceipt:
                return emit_durable_store_import_receipt_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportReceiptPersistenceRequest:
                return emit_durable_store_import_receipt_persistence_request_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportDecisionReview:
                return emit_durable_store_import_decision_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportReceiptReview:
                return emit_durable_store_import_receipt_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitDurableStoreImportReceiptPersistenceReview:
                return emit_durable_store_import_receipt_persistence_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitSchedulerReview:
                return emit_scheduler_review_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitRuntimeSession:
                return emit_runtime_session_with_diagnostics(
                    ir_program, metadata_validation.metadata, *capability_mock_set, options);
            case CommandKind::EmitExecutionPlan:
                return emit_execution_plan_with_diagnostics(
                    ir_program, metadata_validation.metadata);
            default:
                break;
            }
        }

        if (emit_selected_backend(effective_command,
                                  ir_program,
                                  resolve_result,
                                  type_check_result,
                                  &metadata_validation.metadata,
                                  std::cout)) {
            return 0;
        }
    }

    if (emit_selected_backend(effective_command,
                              input,
                              resolve_result,
                              type_check_result,
                              package_metadata,
                              std::cout)) {
        return 0;
    }

    if (!is_action_enabled(options, CommandKind::DumpTypes)) {
        print_success_summary(input, resolve_result, type_check_result, std::cout);
    }

    return 0;
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--project") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--package") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --package requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.package_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--capability-mocks") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --capability-mocks requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.capability_mocks_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--workspace") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workspace requires a descriptor path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workspace_descriptor = arguments[++index];
            continue;
        }

        if (argument == "--project-name") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --project-name requires a project name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.project_name = arguments[++index];
            continue;
        }

        if (argument == "--workflow") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --workflow requires a canonical workflow name\n";
                print_usage(std::cerr);
                return 2;
            }

            options.workflow_name = arguments[++index];
            continue;
        }

        if (argument == "--input-fixture") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --input-fixture requires a fixture string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.input_fixture = arguments[++index];
            continue;
        }

        if (argument == "--run-id") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --run-id requires an id string\n";
                print_usage(std::cerr);
                return 2;
            }

            options.run_id = arguments[++index];
            continue;
        }

        if (argument == "--search-root") {
            if (index + 1 >= arguments.size()) {
                std::cerr << "error: --search-root requires a directory path\n";
                print_usage(std::cerr);
                return 2;
            }

            options.search_roots.push_back(arguments[++index]);
            continue;
        }

        if (argument == "--dump-ast") {
            options.dump_ast_requested = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types_requested = true;
            continue;
        }

        const auto command = command_token_to_kind(argument);
        if (command.has_value() && !options.selected_command.has_value() &&
            options.positional.empty()) {
            set_command_option(options, *command);
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        options.positional.push_back(argument);
    }

    return -1;
}

int run_cli(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    if (const auto parse_status = parse_command_line(arguments, options); parse_status >= 0) {
        return parse_status;
    }

    const auto effective_command = infer_effective_command(options);

    const auto action_count = count_enabled_actions(options);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of "
                  << format_comma_or_commands(command_list(CommandListKind::Action)) << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --project cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && options.project_descriptor.has_value()) {
        std::cerr << "error: --workspace cannot be combined with --project\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.search_roots.empty()) {
        std::cerr << "error: --workspace cannot be combined with --search-root\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_name.has_value() && !options.workspace_descriptor.has_value()) {
        std::cerr << "error: --project-name requires --workspace\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workspace_descriptor.has_value() && !options.project_name.has_value()) {
        std::cerr << "error: --workspace requires --project-name\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.project_descriptor.has_value() || options.workspace_descriptor.has_value()
            ? !options.positional.empty()
            : options.positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    if (options.package_descriptor.has_value() &&
        (!effective_command.has_value() ||
         !is_package_supported_command(*effective_command))) {
        std::cerr << "error: --package is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::PackageSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.capability_mocks_descriptor.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --capability-mocks is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.input_fixture.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --input-fixture is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.run_id.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --run-id is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.workflow_name.has_value() &&
        (!effective_command.has_value() ||
         !is_capability_input_supported_command(*effective_command))) {
        std::cerr << "error: --workflow is only supported with "
                  << format_comma_or_commands(command_list(CommandListKind::CapabilityInputSupported))
                  << "\n";
        print_usage(std::cerr);
        return 2;
    }

    std::optional<ahfl::dry_run::CapabilityMockSet> capability_mock_set;
    if (effective_command.has_value() &&
        is_command_requiring_package(*effective_command)) {
        const auto selected_command_name = command_name(*effective_command);
        if (!options.package_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --package\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.capability_mocks_descriptor.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --capability-mocks\n";
            print_usage(std::cerr);
            return 2;
        }
        if (!options.input_fixture.has_value()) {
            std::cerr << "error: " << selected_command_name << " requires --input-fixture\n";
            print_usage(std::cerr);
            return 2;
        }

        std::string capability_mocks_content;
        if (!read_text_file(std::string(*options.capability_mocks_descriptor),
                            capability_mocks_content,
                            std::cerr)) {
            return 1;
        }

        auto mock_parse_result =
            ahfl::dry_run::parse_capability_mock_set_json(capability_mocks_content);
        mock_parse_result.diagnostics.render(std::cerr);
        if (mock_parse_result.has_errors() || !mock_parse_result.mock_set.has_value()) {
            return 1;
        }

        capability_mock_set = std::move(*mock_parse_result.mock_set);
    }

    std::optional<ahfl::handoff::PackageMetadata> package_metadata;

    const bool project_mode = options.project_descriptor.has_value() ||
                              options.workspace_descriptor.has_value() ||
                              !options.search_roots.empty();
    const ahfl::Frontend frontend;
    if (options.package_descriptor.has_value()) {
        auto package_result =
            frontend.load_package_authoring_descriptor(std::string(*options.package_descriptor));
        package_result.diagnostics.render(std::cerr);
        if (package_result.has_errors() || !package_result.descriptor.has_value()) {
            return package_result.has_errors() ? 1 : 0;
        }

        package_metadata = lower_package_metadata(*package_result.descriptor);
    }
    if (project_mode || is_action_enabled(options, CommandKind::DumpProject)) {
        ahfl::ProjectInput input;
        if (const auto load_status = load_project_input(options, frontend, input);
            load_status >= 0) {
            return load_status;
        }

        auto project_result = frontend.parse_project(input);
        render_diagnostics(project_result, std::nullopt, std::cerr);
        if (project_result.has_errors()) {
            return 1;
        }

        if (effective_command == CommandKind::DumpProject) {
            ahfl::dump_project_outline(project_result.graph, std::cout);
            return 0;
        }

        if (effective_command == CommandKind::DumpAst) {
            dump_ast_outline(project_result.graph, std::cout);
            return 0;
        }

        return run_analysis_pipeline(options,
                                     effective_command,
                                     project_result.graph,
                                     std::nullopt,
                                     package_metadata ? &*package_metadata : nullptr,
                                     capability_mock_set ? &*capability_mock_set : nullptr);
    }

    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    render_diagnostics(parse_result, std::cref(parse_result.source), std::cerr);

    if (parse_result.program && effective_command == CommandKind::DumpAst) {
        dump_ast_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    return run_analysis_pipeline(options,
                                 effective_command,
                                 *parse_result.program,
                                 std::cref(parse_result.source),
                                 package_metadata ? &*package_metadata : nullptr,
                                 capability_mock_set ? &*capability_mock_set : nullptr);
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return run_cli(arguments);
}
