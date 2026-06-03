#include "tooling/cli/pipeline_runner.hpp"

#include "pipeline_context.hpp"
#include "pipeline_core_commands.hpp"
#include "provider/pipeline_durable_store_import.hpp"
#include "provider/pipeline_durable_store_import_provider.hpp"

#include <array>
#include <iostream>
#include <optional>

namespace ahfl::cli {
namespace {

[[nodiscard]] int invoke_execution_plan_command(const PackagePipelineContext &context) {
    return emit_execution_plan_with_diagnostics(context.program, context.metadata);
}

template <ProviderArtifactKind Artifact>
[[nodiscard]] int invoke_provider_artifact_command(const PackagePipelineContext &context) {
    if (context.mock_set == nullptr) {
        context.io.err << "error: internal command dispatch failed: missing capability mocks\n";
        return 1;
    }

    return emit_provider_artifact_with_diagnostics(Artifact,
                                                   context.program,
                                                   context.metadata,
                                                   *context.mock_set,
                                                   context.options,
                                                   context.io);
}

// O(1) dispatch table indexed by CommandKind enum value.
constexpr auto kPackageCommandHandlers = [] {
    std::array<PackageCommandHandler, static_cast<std::size_t>(CommandKind::_Count)> table{};

    table[static_cast<std::size_t>(CommandKind::EmitDryRunTrace)] =
        invoke_package_command<emit_dry_run_trace_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitExecutionJournal)] =
        invoke_package_command<emit_execution_journal_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitReplayView)] =
        invoke_package_command<emit_replay_view_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitAuditReport)] =
        invoke_package_command<emit_audit_report_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitSchedulerSnapshot)] =
        invoke_package_command<emit_scheduler_snapshot_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitCheckpointRecord)] =
        invoke_package_command<emit_checkpoint_record_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitCheckpointReview)] =
        invoke_package_command<emit_checkpoint_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitPersistenceDescriptor)] =
        invoke_package_command<emit_persistence_descriptor_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitPersistenceReview)] =
        invoke_package_command<emit_persistence_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitExportManifest)] =
        invoke_package_command<emit_export_manifest_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitExportReview)] =
        invoke_package_command<emit_export_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitStoreImportDescriptor)] =
        invoke_package_command<emit_store_import_descriptor_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitStoreImportReview)] =
        invoke_package_command<emit_store_import_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportRequest)] =
        invoke_package_command<emit_durable_store_import_request_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReview)] =
        invoke_package_command<emit_durable_store_import_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportDecision)] =
        invoke_package_command<emit_durable_store_import_decision_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceipt)] =
        invoke_package_command<emit_durable_store_import_receipt_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceRequest)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_request_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportDecisionReview)] =
        invoke_package_command<emit_durable_store_import_decision_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptReview)] =
        invoke_package_command<emit_durable_store_import_receipt_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceReview)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportReceiptPersistenceResponse)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_response_with_diagnostics>;
    table[static_cast<std::size_t>(
        CommandKind::EmitDurableStoreImportReceiptPersistenceResponseReview)] =
        invoke_package_command<
            emit_durable_store_import_receipt_persistence_response_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportAdapterExecution)] =
        invoke_package_command<emit_durable_store_import_adapter_execution_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitDurableStoreImportRecoveryPreview)] =
        invoke_package_command<emit_durable_store_import_recovery_preview_with_diagnostics>;

#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT(                                           \
    kind, command_kind, artifact_type, builder, printer, command_token, visibility, order)         \
    table[static_cast<std::size_t>(CommandKind::command_kind)] =                                   \
        invoke_provider_artifact_command<ProviderArtifactKind::kind>;
#include "tooling/cli/provider/pipeline_durable_store_import_provider_artifacts.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_ARTIFACT

    table[static_cast<std::size_t>(CommandKind::EmitSchedulerReview)] =
        invoke_package_command<emit_scheduler_review_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitRuntimeSession)] =
        invoke_package_command<emit_runtime_session_with_diagnostics>;
    table[static_cast<std::size_t>(CommandKind::EmitExecutionPlan)] = invoke_execution_plan_command;

    return table;
}();

[[nodiscard]] std::optional<int>
dispatch_package_command_impl(CommandKind command, const PackagePipelineContext &context) {
    const auto idx = static_cast<std::size_t>(command);
    if (idx >= kPackageCommandHandlers.size()) {
        return std::nullopt;
    }
    const auto handler = kPackageCommandHandlers[idx];
    if (handler == nullptr) {
        return std::nullopt;
    }
    return handler(context);
}

} // namespace

bool handles_package_command(CommandKind command) {
    const auto idx = static_cast<std::size_t>(command);
    return idx < kPackageCommandHandlers.size() && kPackageCommandHandlers[idx] != nullptr;
}

std::optional<int> dispatch_package_command(CommandKind command,
                                            const ahfl::ir::Program &program,
                                            const ahfl::handoff::PackageMetadata &metadata,
                                            const ahfl::dry_run::CapabilityMockSet *mock_set,
                                            const CommandLineOptions &options) {
    const PackagePipelineContext context{
        program,
        metadata,
        mock_set,
        options,
        {},
    };
    return dispatch_package_command_impl(command, context);
}

} // namespace ahfl::cli
