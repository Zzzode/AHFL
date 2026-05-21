#pragma once

#include "pipeline_durable_store_import.hpp"
#include "pipeline_durable_store_import_provider.hpp"

#include "ahfl/durable_store_import/provider.hpp"

#include <optional>
#include <string_view>

namespace ahfl::cli {

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRuntimePreflightPlan>
build_durable_store_import_provider_runtime_preflight_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRuntimeReadinessReview>
build_durable_store_import_provider_runtime_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkRequestEnvelopePlan>
build_durable_store_import_provider_sdk_envelope_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkHandoffReadinessReview>
build_durable_store_import_provider_sdk_handoff_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderHostExecutionPlan>
build_durable_store_import_provider_host_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderHostExecutionReadinessReview>
build_durable_store_import_provider_host_execution_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostExecutionReceipt>
build_durable_store_import_provider_local_host_execution_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostExecutionReceiptReview>
build_durable_store_import_provider_local_host_execution_receipt_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterRequestPlan>
build_durable_store_import_provider_sdk_adapter_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterResponsePlaceholder>
build_durable_store_import_provider_sdk_adapter_response_placeholder_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterReadinessReview>
build_durable_store_import_provider_sdk_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfacePlan>
build_durable_store_import_provider_sdk_adapter_interface_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkAdapterInterfaceReview>
build_durable_store_import_provider_sdk_adapter_interface_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigLoadPlan>
build_durable_store_import_provider_config_load_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigSnapshotPlaceholder>
build_durable_store_import_provider_config_snapshot_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConfigReadinessReview>
build_durable_store_import_provider_config_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverRequestPlan>
build_durable_store_import_provider_secret_resolver_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretResolverResponsePlaceholder>
build_durable_store_import_provider_secret_resolver_response_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSecretPolicyReview>
build_durable_store_import_provider_secret_policy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRequest>
build_durable_store_import_provider_local_host_harness_request_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessExecutionRecord>
build_durable_store_import_provider_local_host_harness_record_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalHostHarnessReview>
build_durable_store_import_provider_local_host_harness_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadMaterializationPlan>
build_durable_store_import_provider_sdk_payload_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkPayloadAuditSummary>
build_durable_store_import_provider_sdk_payload_audit_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterContract>
build_durable_store_import_provider_sdk_mock_adapter_contract_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterExecutionResult>
build_durable_store_import_provider_sdk_mock_adapter_execution_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSdkMockAdapterReadiness>
build_durable_store_import_provider_sdk_mock_adapter_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaPlan>
build_durable_store_import_provider_local_filesystem_alpha_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaResult>
build_durable_store_import_provider_local_filesystem_alpha_result_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderLocalFilesystemAlphaReadiness>
build_durable_store_import_provider_local_filesystem_alpha_readiness_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRetryDecision>
build_durable_store_import_provider_write_retry_decision_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReceipt>
build_durable_store_import_provider_write_commit_receipt_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteCommitReview>
build_durable_store_import_provider_write_commit_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryCheckpoint>
build_durable_store_import_provider_write_recovery_checkpoint_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryPlan>
build_durable_store_import_provider_write_recovery_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderWriteRecoveryReview>
build_durable_store_import_provider_write_recovery_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReport>
build_durable_store_import_provider_failure_taxonomy_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFailureTaxonomyReview>
build_durable_store_import_provider_failure_taxonomy_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderExecutionAuditEvent>
build_durable_store_import_provider_execution_audit_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderTelemetrySummary>
build_durable_store_import_provider_telemetry_summary_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderOperatorReviewEvent>
build_durable_store_import_provider_operator_review_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityTestManifest>
build_durable_store_import_provider_compatibility_test_manifest_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFixtureMatrix>
build_durable_store_import_provider_fixture_matrix_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityReport>
build_durable_store_import_provider_compatibility_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRegistry>
build_durable_store_import_provider_registry_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSelectionPlan>
build_durable_store_import_provider_selection_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCapabilityNegotiationReview>
build_durable_store_import_provider_capability_negotiation_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessEvidence>
build_durable_store_import_provider_production_readiness_evidence_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReview>
build_durable_store_import_provider_production_readiness_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name);

} // namespace ahfl::cli
