#include "ahfl/cli/pipeline_runner.hpp"

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
    {CommandKind::EmitDurableStoreImportProviderWriteAttempt,
     invoke_package_command<emit_durable_store_import_provider_write_attempt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderRecoveryHandoff,
     invoke_package_command<emit_durable_store_import_provider_recovery_handoff_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderDriverBinding,
     invoke_package_command<emit_durable_store_import_provider_driver_binding_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderDriverReadiness,
     invoke_package_command<emit_durable_store_import_provider_driver_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderRuntimePreflight,
     invoke_package_command<emit_durable_store_import_provider_runtime_preflight_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderRuntimeReadiness,
     invoke_package_command<emit_durable_store_import_provider_runtime_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkEnvelope,
     invoke_package_command<emit_durable_store_import_provider_sdk_envelope_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkHandoffReadiness,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_handoff_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderHostExecution,
     invoke_package_command<emit_durable_store_import_provider_host_execution_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderHostExecutionReadiness,
     invoke_package_command<
         emit_durable_store_import_provider_host_execution_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalHostExecutionReceipt,
     invoke_package_command<
         emit_durable_store_import_provider_local_host_execution_receipt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalHostExecutionReceiptReview,
     invoke_package_command<
         emit_durable_store_import_provider_local_host_execution_receipt_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterRequest,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_adapter_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterResponsePlaceholder,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_adapter_response_placeholder_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterReadiness,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_adapter_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterInterface,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_adapter_interface_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkAdapterInterfaceReview,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_adapter_interface_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderConfigLoad,
     invoke_package_command<emit_durable_store_import_provider_config_load_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderConfigSnapshot,
     invoke_package_command<emit_durable_store_import_provider_config_snapshot_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderConfigReadiness,
     invoke_package_command<emit_durable_store_import_provider_config_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSecretResolverRequest,
     invoke_package_command<
         emit_durable_store_import_provider_secret_resolver_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSecretResolverResponse,
     invoke_package_command<
         emit_durable_store_import_provider_secret_resolver_response_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSecretPolicyReview,
     invoke_package_command<
         emit_durable_store_import_provider_secret_policy_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessRequest,
     invoke_package_command<
         emit_durable_store_import_provider_local_host_harness_request_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessRecord,
     invoke_package_command<
         emit_durable_store_import_provider_local_host_harness_record_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalHostHarnessReview,
     invoke_package_command<
         emit_durable_store_import_provider_local_host_harness_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkPayloadPlan,
     invoke_package_command<emit_durable_store_import_provider_sdk_payload_plan_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkPayloadAudit,
     invoke_package_command<emit_durable_store_import_provider_sdk_payload_audit_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterContract,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_mock_adapter_contract_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterExecution,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_mock_adapter_execution_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSdkMockAdapterReadiness,
     invoke_package_command<
         emit_durable_store_import_provider_sdk_mock_adapter_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaPlan,
     invoke_package_command<
         emit_durable_store_import_provider_local_filesystem_alpha_plan_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaResult,
     invoke_package_command<
         emit_durable_store_import_provider_local_filesystem_alpha_result_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderLocalFilesystemAlphaReadiness,
     invoke_package_command<
         emit_durable_store_import_provider_local_filesystem_alpha_readiness_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteRetryDecision,
     invoke_package_command<
         emit_durable_store_import_provider_write_retry_decision_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteCommitReceipt,
     invoke_package_command<
         emit_durable_store_import_provider_write_commit_receipt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteCommitReview,
     invoke_package_command<
         emit_durable_store_import_provider_write_commit_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryCheckpoint,
     invoke_package_command<
         emit_durable_store_import_provider_write_recovery_checkpoint_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryPlan,
     invoke_package_command<
         emit_durable_store_import_provider_write_recovery_plan_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderWriteRecoveryReview,
     invoke_package_command<
         emit_durable_store_import_provider_write_recovery_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderFailureTaxonomyReport,
     invoke_package_command<
         emit_durable_store_import_provider_failure_taxonomy_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderFailureTaxonomyReview,
     invoke_package_command<
         emit_durable_store_import_provider_failure_taxonomy_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderExecutionAuditEvent,
     invoke_package_command<
         emit_durable_store_import_provider_execution_audit_event_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderTelemetrySummary,
     invoke_package_command<emit_durable_store_import_provider_telemetry_summary_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderOperatorReviewEvent,
     invoke_package_command<
         emit_durable_store_import_provider_operator_review_event_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderCompatibilityTestManifest,
     invoke_package_command<
         emit_durable_store_import_provider_compatibility_test_manifest_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderFixtureMatrix,
     invoke_package_command<emit_durable_store_import_provider_fixture_matrix_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderCompatibilityReport,
     invoke_package_command<
         emit_durable_store_import_provider_compatibility_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderRegistry,
     invoke_package_command<emit_durable_store_import_provider_registry_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSelectionPlan,
     invoke_package_command<emit_durable_store_import_provider_selection_plan_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderCapabilityNegotiationReview,
     invoke_package_command<
         emit_durable_store_import_provider_capability_negotiation_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderProductionReadinessEvidence,
     invoke_package_command<
         emit_durable_store_import_provider_production_readiness_evidence_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderProductionReadinessReview,
     invoke_package_command<
         emit_durable_store_import_provider_production_readiness_review_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderProductionReadinessReport,
     invoke_package_command<
         emit_durable_store_import_provider_production_readiness_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderSchemaCompatibilityReport,
     invoke_package_command<
         emit_durable_store_import_provider_schema_compatibility_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderConfigBundleValidationReport,
     invoke_package_command<
         emit_durable_store_import_provider_config_bundle_validation_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderConformanceReport,
     invoke_package_command<
         emit_durable_store_import_provider_conformance_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderReleaseEvidenceArchiveManifest,
     invoke_package_command<
         emit_durable_store_import_provider_release_evidence_archive_manifest_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderApprovalReceipt,
     invoke_package_command<emit_durable_store_import_provider_approval_receipt_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderOptInDecisionReport,
     invoke_package_command<
         emit_durable_store_import_provider_opt_in_decision_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderRuntimePolicyReport,
     invoke_package_command<
         emit_durable_store_import_provider_runtime_policy_report_with_diagnostics>},
    {CommandKind::EmitDurableStoreImportProviderProductionIntegrationDryRunReport,
     invoke_package_command<
         emit_durable_store_import_provider_production_integration_dry_run_report_with_diagnostics>},
    {CommandKind::EmitSchedulerReview,
     invoke_package_command<emit_scheduler_review_with_diagnostics>},
    {CommandKind::EmitRuntimeSession,
     invoke_package_command<emit_runtime_session_with_diagnostics>},
    {CommandKind::EmitExecutionPlan, invoke_execution_plan_command},
};

[[nodiscard]] std::optional<int>
dispatch_package_command_impl(CommandKind command, const PackagePipelineContext &context) {
    for (const auto &entry : kPackageCommandDispatch) {
        if (entry.kind == command) {
            return entry.handler(context);
        }
    }

    return std::nullopt;
}

} // namespace

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
