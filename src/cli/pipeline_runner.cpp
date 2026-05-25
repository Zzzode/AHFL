#include "cli/pipeline_runner.hpp"

#include "pipeline_context.hpp"
#include "pipeline_core_commands.hpp"
#include "pipeline_durable_store_import.hpp"
#include "pipeline_durable_store_import_provider.hpp"

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
        std::cerr << "error: internal command dispatch failed: missing capability mocks\n";
        return 1;
    }

    return emit_provider_artifact_with_diagnostics(
        Artifact, context.program, context.metadata, *context.mock_set, context.options);
}

struct PackageCommandDispatchEntry {
    CommandKind kind;
    PackageCommandHandler handler;
};

constexpr PackageCommandDispatchEntry kPackageCommandDispatch[] = {
    {CommandKind::EmitDryRunTrace, invoke_package_command<emit_dry_run_trace_with_diagnostics>},
    {CommandKind::EmitExecutionJournal,
     invoke_package_command<emit_execution_journal_with_diagnostics>},
    {CommandKind::EmitReplayView, invoke_package_command<emit_replay_view_with_diagnostics>},
    {CommandKind::EmitAuditReport, invoke_package_command<emit_audit_report_with_diagnostics>},
    {CommandKind::EmitSchedulerSnapshot,
     invoke_package_command<emit_scheduler_snapshot_with_diagnostics>},
    {CommandKind::EmitCheckpointRecord,
     invoke_package_command<emit_checkpoint_record_with_diagnostics>},
    {CommandKind::EmitCheckpointReview,
     invoke_package_command<emit_checkpoint_review_with_diagnostics>},
    {CommandKind::EmitPersistenceDescriptor,
     invoke_package_command<emit_persistence_descriptor_with_diagnostics>},
    {CommandKind::EmitPersistenceReview,
     invoke_package_command<emit_persistence_review_with_diagnostics>},
    {CommandKind::EmitExportManifest,
     invoke_package_command<emit_export_manifest_with_diagnostics>},
    {CommandKind::EmitExportReview, invoke_package_command<emit_export_review_with_diagnostics>},
    {CommandKind::EmitStoreImportDescriptor,
     invoke_package_command<emit_store_import_descriptor_with_diagnostics>},
    {CommandKind::EmitStoreImportReview,
     invoke_package_command<emit_store_import_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportRequest,
     invoke_package_command<emit_durable_store_import_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReview,
     invoke_package_command<emit_durable_store_import_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportDecision,
     invoke_package_command<emit_durable_store_import_decision_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceipt,
     invoke_package_command<emit_durable_store_import_receipt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceRequest,
     invoke_package_command<
         emit_durable_store_import_receipt_persistence_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportDecisionReview,
     invoke_package_command<emit_durable_store_import_decision_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptReview,
     invoke_package_command<emit_durable_store_import_receipt_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceReview,
     invoke_package_command<emit_durable_store_import_receipt_persistence_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceResponse,
     invoke_package_command<
         emit_durable_store_import_receipt_persistence_response_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportReceiptPersistenceResponseReview,
     invoke_package_command<
         emit_durable_store_import_receipt_persistence_response_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportAdapterExecution,
     invoke_package_command<emit_durable_store_import_adapter_execution_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportRecoveryPreview,
     invoke_package_command<emit_durable_store_import_recovery_preview_with_diagnostics>},
#define AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND(kind,                                       \
                                                       token,                                      \
                                                       usage_order,                                \
                                                       action_order,                               \
                                                       inference_order,                            \
                                                       package_order,                              \
                                                       capability_order,                           \
                                                       artifact_kind)                              \
    {CommandKind::kind, invoke_provider_artifact_command<ProviderArtifactKind::artifact_kind>},
#include "cli/durable_store_import_provider_commands.def"
#undef AHFL_CLI_DURABLE_STORE_IMPORT_PROVIDER_COMMAND
    {CommandKind::EmitSchedulerReview,
     invoke_package_command<emit_scheduler_review_with_diagnostics>},
    {CommandKind::EmitRuntimeSession,
     invoke_package_command<emit_runtime_session_with_diagnostics>},
    {CommandKind::EmitExecutionPlan, invoke_execution_plan_command},
};

[[nodiscard]] const PackageCommandDispatchEntry *find_package_command(CommandKind command) {
    for (const auto &entry : kPackageCommandDispatch) {
        if (entry.kind == command) {
            return &entry;
        }
    }

    return nullptr;
}

[[nodiscard]] std::optional<int>
dispatch_package_command_impl(CommandKind command, const PackagePipelineContext &context) {
    const auto *entry = find_package_command(command);
    if (entry == nullptr) {
        return std::nullopt;
    }

    return entry->handler(context);
}

} // namespace

bool handles_package_command(CommandKind command) {
    return find_package_command(command) != nullptr;
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
    };
    return dispatch_package_command_impl(command, context);
}

} // namespace ahfl::cli
