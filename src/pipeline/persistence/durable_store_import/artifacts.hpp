#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

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

enum class DurableStoreImportArtifactOutputFormat {
    Json,
    Text,
};

enum class DurableStoreImportArtifactDomain {
    CoreWorkflow,
    Persistence,
    ProviderConfiguration,
    ProviderGovernance,
    ProviderHostExecution,
    ProviderReliability,
    ProviderRuntime,
    ProviderSdk,
};

struct DurableStoreImportArtifactPrinterDescriptor {
    std::string_view public_name;
    std::string_view artifact_type_name;
    std::string_view detail_namespace;
    std::string_view detail_name;
    std::string_view artifact_id;
    DurableStoreImportArtifactOutputFormat output_format;
    DurableStoreImportArtifactDomain domain;
};

[[nodiscard]] std::string_view
durable_store_import_artifact_output_format_name(DurableStoreImportArtifactOutputFormat format);

[[nodiscard]] std::string_view
durable_store_import_artifact_domain_name(DurableStoreImportArtifactDomain domain);

[[nodiscard]] std::span<const DurableStoreImportArtifactPrinterDescriptor>
durable_store_import_artifact_printers();
[[nodiscard]] const DurableStoreImportArtifactPrinterDescriptor *
find_durable_store_import_artifact_printer(std::string_view artifact_id);

#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    void public_name(const durable_store_import::artifact_type &parameter_name, std::ostream &out);
#include "pipeline/persistence/durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER
} // namespace ahfl
