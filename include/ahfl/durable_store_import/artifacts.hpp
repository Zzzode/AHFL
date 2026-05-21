#pragma once

#include <iosfwd>

namespace ahfl::durable_store_import {

struct AdapterExecutionReceipt;
struct ApprovalReceipt;
struct Decision;
struct DecisionReviewSummary;
struct PersistenceRequest;
struct PersistenceResponse;
struct PersistenceResponseReviewSummary;
struct PersistenceReviewSummary;
struct ProviderCapabilityNegotiationReview;
struct ProviderCompatibilityReport;
struct ProviderCompatibilityTestManifest;
struct ProviderConfigBundleValidationReport;
struct ProviderConfigLoadPlan;
struct ProviderConfigReadinessReview;
struct ProviderConfigSnapshotPlaceholder;
struct ProviderConformanceReport;
struct ProviderDriverBindingPlan;
struct ProviderDriverReadinessReview;
struct ProviderExecutionAuditEvent;
struct ProviderFailureTaxonomyReport;
struct ProviderFailureTaxonomyReview;
struct ProviderFixtureMatrix;
struct ProviderHostExecutionPlan;
struct ProviderHostExecutionReadinessReview;
struct ProviderLocalFilesystemAlphaPlan;
struct ProviderLocalFilesystemAlphaReadiness;
struct ProviderLocalFilesystemAlphaResult;
struct ProviderLocalHostExecutionReceipt;
struct ProviderLocalHostExecutionReceiptReview;
struct ProviderLocalHostHarnessExecutionRecord;
struct ProviderLocalHostHarnessExecutionRequest;
struct ProviderLocalHostHarnessReview;
struct ProviderOperatorReviewEvent;
struct ProviderOptInDecisionReport;
struct ProviderProductionIntegrationDryRunReport;
struct ProviderProductionReadinessEvidence;
struct ProviderProductionReadinessReport;
struct ProviderProductionReadinessReview;
struct ProviderRecoveryHandoffPreview;
struct ProviderRegistry;
struct ProviderRuntimePolicyReport;
struct ProviderRuntimePreflightPlan;
struct ProviderRuntimeReadinessReview;
struct ProviderSchemaCompatibilityReport;
struct ProviderSdkAdapterInterfacePlan;
struct ProviderSdkAdapterInterfaceReview;
struct ProviderSdkAdapterReadinessReview;
struct ProviderSdkAdapterRequestPlan;
struct ProviderSdkAdapterResponsePlaceholder;
struct ProviderSdkHandoffReadinessReview;
struct ProviderSdkMockAdapterContract;
struct ProviderSdkMockAdapterExecutionResult;
struct ProviderSdkMockAdapterReadiness;
struct ProviderSdkPayloadAuditSummary;
struct ProviderSdkPayloadMaterializationPlan;
struct ProviderSdkRequestEnvelopePlan;
struct ProviderSecretPolicyReview;
struct ProviderSecretResolverRequestPlan;
struct ProviderSecretResolverResponsePlaceholder;
struct ProviderSelectionPlan;
struct ProviderTelemetrySummary;
struct ProviderWriteAttemptPreview;
struct ProviderWriteCommitReceipt;
struct ProviderWriteCommitReview;
struct ProviderWriteRecoveryCheckpoint;
struct ProviderWriteRecoveryPlan;
struct ProviderWriteRecoveryReview;
struct ProviderWriteRetryDecision;
struct Receipt;
struct ReceiptReviewSummary;
struct RecoveryCommandPreview;
struct ReleaseEvidenceArchiveManifest;
struct Request;
struct ReviewSummary;

} // namespace ahfl::durable_store_import

namespace ahfl {

void print_durable_store_import_adapter_execution_json(
    const durable_store_import::AdapterExecutionReceipt &receipt, std::ostream &out);

void print_durable_store_import_decision_json(const durable_store_import::Decision &decision,
                                              std::ostream &out);

void print_durable_store_import_decision_review(
    const durable_store_import::DecisionReviewSummary &summary, std::ostream &out);

void print_durable_store_import_provider_approval_receipt_json(
    const durable_store_import::ApprovalReceipt &receipt, std::ostream &out);

void print_durable_store_import_provider_capability_negotiation_review(
    const durable_store_import::ProviderCapabilityNegotiationReview &review, std::ostream &out);

void print_durable_store_import_provider_compatibility_report_json(
    const durable_store_import::ProviderCompatibilityReport &report, std::ostream &out);

void print_durable_store_import_provider_compatibility_test_manifest_json(
    const durable_store_import::ProviderCompatibilityTestManifest &manifest, std::ostream &out);

void print_durable_store_import_provider_config_bundle_validation_report(
    const durable_store_import::ProviderConfigBundleValidationReport &report, std::ostream &out);

void print_durable_store_import_provider_config_load_json(
    const durable_store_import::ProviderConfigLoadPlan &plan, std::ostream &out);

void print_durable_store_import_provider_config_readiness(
    const durable_store_import::ProviderConfigReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_config_snapshot_json(
    const durable_store_import::ProviderConfigSnapshotPlaceholder &placeholder, std::ostream &out);

void print_durable_store_import_provider_conformance_report(
    const durable_store_import::ProviderConformanceReport &report, std::ostream &out);

void print_durable_store_import_provider_driver_binding_json(
    const durable_store_import::ProviderDriverBindingPlan &plan, std::ostream &out);

void print_durable_store_import_provider_driver_readiness(
    const durable_store_import::ProviderDriverReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_execution_audit_event_json(
    const durable_store_import::ProviderExecutionAuditEvent &event, std::ostream &out);

void print_durable_store_import_provider_failure_taxonomy_report_json(
    const durable_store_import::ProviderFailureTaxonomyReport &report, std::ostream &out);

void print_durable_store_import_provider_failure_taxonomy_review(
    const durable_store_import::ProviderFailureTaxonomyReview &review, std::ostream &out);

void print_durable_store_import_provider_fixture_matrix_json(
    const durable_store_import::ProviderFixtureMatrix &matrix, std::ostream &out);

void print_durable_store_import_provider_host_execution_json(
    const durable_store_import::ProviderHostExecutionPlan &plan, std::ostream &out);

void print_durable_store_import_provider_host_execution_readiness(
    const durable_store_import::ProviderHostExecutionReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_local_filesystem_alpha_plan_json(
    const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan, std::ostream &out);

void print_durable_store_import_provider_local_filesystem_alpha_readiness(
    const durable_store_import::ProviderLocalFilesystemAlphaReadiness &readiness,
    std::ostream &out);

void print_durable_store_import_provider_local_filesystem_alpha_result_json(
    const durable_store_import::ProviderLocalFilesystemAlphaResult &result, std::ostream &out);

void print_durable_store_import_provider_local_host_execution_receipt_json(
    const durable_store_import::ProviderLocalHostExecutionReceipt &receipt, std::ostream &out);

void print_durable_store_import_provider_local_host_execution_receipt_review(
    const durable_store_import::ProviderLocalHostExecutionReceiptReview &review, std::ostream &out);

void print_durable_store_import_provider_local_host_harness_record_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record, std::ostream &out);

void print_durable_store_import_provider_local_host_harness_request_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request,
    std::ostream &out);

void print_durable_store_import_provider_local_host_harness_review(
    const durable_store_import::ProviderLocalHostHarnessReview &review, std::ostream &out);

void print_durable_store_import_provider_operator_review_event(
    const durable_store_import::ProviderOperatorReviewEvent &review, std::ostream &out);

void print_durable_store_import_provider_opt_in_decision_report(
    const durable_store_import::ProviderOptInDecisionReport &report, std::ostream &out);

void print_durable_store_import_provider_production_integration_dry_run_report(
    const durable_store_import::ProviderProductionIntegrationDryRunReport &report,
    std::ostream &out);

void print_durable_store_import_provider_production_readiness_evidence_json(
    const durable_store_import::ProviderProductionReadinessEvidence &evidence, std::ostream &out);

void print_durable_store_import_provider_production_readiness_report(
    const durable_store_import::ProviderProductionReadinessReport &report, std::ostream &out);

void print_durable_store_import_provider_production_readiness_review_json(
    const durable_store_import::ProviderProductionReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_recovery_handoff(
    const durable_store_import::ProviderRecoveryHandoffPreview &preview, std::ostream &out);

void print_durable_store_import_provider_registry_json(
    const durable_store_import::ProviderRegistry &registry, std::ostream &out);

void print_durable_store_import_provider_release_evidence_archive_manifest(
    const durable_store_import::ReleaseEvidenceArchiveManifest &manifest, std::ostream &out);

void print_durable_store_import_provider_runtime_policy_report(
    const durable_store_import::ProviderRuntimePolicyReport &report, std::ostream &out);

void print_durable_store_import_provider_runtime_preflight_json(
    const durable_store_import::ProviderRuntimePreflightPlan &plan, std::ostream &out);

void print_durable_store_import_provider_runtime_readiness(
    const durable_store_import::ProviderRuntimeReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_schema_compatibility_report_json(
    const durable_store_import::ProviderSchemaCompatibilityReport &report, std::ostream &out);

void print_durable_store_import_provider_sdk_adapter_interface_json(
    const durable_store_import::ProviderSdkAdapterInterfacePlan &plan, std::ostream &out);

void print_durable_store_import_provider_sdk_adapter_interface_review(
    const durable_store_import::ProviderSdkAdapterInterfaceReview &review, std::ostream &out);

void print_durable_store_import_provider_sdk_adapter_readiness(
    const durable_store_import::ProviderSdkAdapterReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_sdk_adapter_request_json(
    const durable_store_import::ProviderSdkAdapterRequestPlan &plan, std::ostream &out);

void print_durable_store_import_provider_sdk_adapter_response_placeholder_json(
    const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder,
    std::ostream &out);

void print_durable_store_import_provider_sdk_envelope_json(
    const durable_store_import::ProviderSdkRequestEnvelopePlan &plan, std::ostream &out);

void print_durable_store_import_provider_sdk_handoff_readiness(
    const durable_store_import::ProviderSdkHandoffReadinessReview &review, std::ostream &out);

void print_durable_store_import_provider_sdk_mock_adapter_contract_json(
    const durable_store_import::ProviderSdkMockAdapterContract &contract, std::ostream &out);

void print_durable_store_import_provider_sdk_mock_adapter_execution_json(
    const durable_store_import::ProviderSdkMockAdapterExecutionResult &result, std::ostream &out);

void print_durable_store_import_provider_sdk_mock_adapter_readiness(
    const durable_store_import::ProviderSdkMockAdapterReadiness &readiness, std::ostream &out);

void print_durable_store_import_provider_sdk_payload_audit(
    const durable_store_import::ProviderSdkPayloadAuditSummary &audit, std::ostream &out);

void print_durable_store_import_provider_sdk_payload_plan_json(
    const durable_store_import::ProviderSdkPayloadMaterializationPlan &plan, std::ostream &out);

void print_durable_store_import_provider_secret_policy_review(
    const durable_store_import::ProviderSecretPolicyReview &review, std::ostream &out);

void print_durable_store_import_provider_secret_resolver_request_json(
    const durable_store_import::ProviderSecretResolverRequestPlan &plan, std::ostream &out);

void print_durable_store_import_provider_secret_resolver_response_json(
    const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder,
    std::ostream &out);

void print_durable_store_import_provider_selection_plan_json(
    const durable_store_import::ProviderSelectionPlan &plan, std::ostream &out);

void print_durable_store_import_provider_telemetry_summary_json(
    const durable_store_import::ProviderTelemetrySummary &summary, std::ostream &out);

void print_durable_store_import_provider_write_attempt_json(
    const durable_store_import::ProviderWriteAttemptPreview &preview, std::ostream &out);

void print_durable_store_import_provider_write_commit_receipt_json(
    const durable_store_import::ProviderWriteCommitReceipt &receipt, std::ostream &out);

void print_durable_store_import_provider_write_commit_review(
    const durable_store_import::ProviderWriteCommitReview &review, std::ostream &out);

void print_durable_store_import_provider_write_recovery_checkpoint_json(
    const durable_store_import::ProviderWriteRecoveryCheckpoint &checkpoint, std::ostream &out);

void print_durable_store_import_provider_write_recovery_plan_json(
    const durable_store_import::ProviderWriteRecoveryPlan &plan, std::ostream &out);

void print_durable_store_import_provider_write_recovery_review(
    const durable_store_import::ProviderWriteRecoveryReview &review, std::ostream &out);

void print_durable_store_import_provider_write_retry_decision_json(
    const durable_store_import::ProviderWriteRetryDecision &decision, std::ostream &out);

void print_durable_store_import_receipt_json(const durable_store_import::Receipt &receipt,
                                             std::ostream &out);

void print_durable_store_import_receipt_persistence_request_json(
    const durable_store_import::PersistenceRequest &request, std::ostream &out);

void print_durable_store_import_receipt_persistence_response_json(
    const durable_store_import::PersistenceResponse &response, std::ostream &out);

void print_durable_store_import_receipt_persistence_response_review(
    const durable_store_import::PersistenceResponseReviewSummary &review, std::ostream &out);

void print_durable_store_import_receipt_persistence_review(
    const durable_store_import::PersistenceReviewSummary &summary, std::ostream &out);

void print_durable_store_import_receipt_review(
    const durable_store_import::ReceiptReviewSummary &summary, std::ostream &out);

void print_durable_store_import_recovery_preview(
    const durable_store_import::RecoveryCommandPreview &preview, std::ostream &out);

void print_durable_store_import_request_json(const durable_store_import::Request &request,
                                             std::ostream &out);

void print_durable_store_import_review(const durable_store_import::ReviewSummary &summary,
                                       std::ostream &out);

} // namespace ahfl
