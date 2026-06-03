#include "tooling/cli/provider/provider_artifact_graph.hpp"

#include <array>

namespace ahfl::cli {

namespace {

using Kind = ProviderArtifactKind;

constexpr std::array<Kind, 0> kNoDependencies{};
constexpr std::array kRecoveryHandoffDependencies{Kind::WriteAttempt};
constexpr std::array kDriverBindingDependencies{Kind::WriteAttempt};
constexpr std::array kDriverReadinessDependencies{Kind::DriverBinding};
constexpr std::array kProductionReadinessEvidenceDependencies{
    Kind::CapabilityNegotiationReview,
    Kind::CompatibilityReport,
    Kind::ExecutionAuditEvent,
    Kind::WriteRecoveryPlan,
    Kind::FailureTaxonomyReport,
};
constexpr std::array kConformanceReportDependencies{
    Kind::CompatibilityReport,
    Kind::Registry,
    Kind::ProductionReadinessEvidence,
};
constexpr std::array kSchemaCompatibilityReportDependencies{
    Kind::ProductionReadinessEvidence,
};
constexpr std::array kReleaseEvidenceArchiveManifestDependencies{
    Kind::CompatibilityReport,
    Kind::Registry,
    Kind::ProductionReadinessEvidence,
    Kind::SelectionPlan,
    Kind::ConfigSnapshot,
};
constexpr std::array kApprovalReceiptDependencies{Kind::ReleaseEvidenceArchiveManifest};
constexpr std::array kOptInDecisionReportDependencies{Kind::ApprovalReceipt};
constexpr std::array kRuntimePolicyReportDependencies{Kind::OptInDecisionReport};
constexpr std::array kProductionIntegrationDryRunReportDependencies{Kind::RuntimePolicyReport};

} // namespace

bool provider_artifact_graph_has_entry(ProviderArtifactKind kind) {
    switch (kind) {
    case Kind::WriteAttempt:
    case Kind::RecoveryHandoff:
    case Kind::DriverBinding:
    case Kind::DriverReadiness:
    case Kind::ProductionReadinessEvidence:
    case Kind::ConformanceReport:
    case Kind::SchemaCompatibilityReport:
    case Kind::ReleaseEvidenceArchiveManifest:
    case Kind::ApprovalReceipt:
    case Kind::OptInDecisionReport:
    case Kind::RuntimePolicyReport:
    case Kind::ProductionIntegrationDryRunReport:
        return true;
#define AHFL_PROVIDER_STEP_1(kind_name, domain_builder, result_field, dep1)                        \
    case Kind::kind_name:                                                                          \
        return true;
#define AHFL_PROVIDER_STEP_2(kind_name, domain_builder, result_field, dep1, dep2)                  \
    case Kind::kind_name:                                                                          \
        return true;
#define AHFL_PROVIDER_STEP_CUSTOM(kind_name)
#include "tooling/cli/provider/provider_step_graph.def"
#undef AHFL_PROVIDER_STEP_CUSTOM
#undef AHFL_PROVIDER_STEP_2
#undef AHFL_PROVIDER_STEP_1
    }

    return false;
}

std::span<const ProviderArtifactKind> provider_artifact_dependencies(ProviderArtifactKind kind) {
    switch (kind) {
    case Kind::WriteAttempt:
        return kNoDependencies;
    case Kind::RecoveryHandoff:
        return kRecoveryHandoffDependencies;
    case Kind::DriverBinding:
        return kDriverBindingDependencies;
    case Kind::DriverReadiness:
        return kDriverReadinessDependencies;
    case Kind::ProductionReadinessEvidence:
        return kProductionReadinessEvidenceDependencies;
    case Kind::ConformanceReport:
        return kConformanceReportDependencies;
    case Kind::SchemaCompatibilityReport:
        return kSchemaCompatibilityReportDependencies;
    case Kind::ReleaseEvidenceArchiveManifest:
        return kReleaseEvidenceArchiveManifestDependencies;
    case Kind::ApprovalReceipt:
        return kApprovalReceiptDependencies;
    case Kind::OptInDecisionReport:
        return kOptInDecisionReportDependencies;
    case Kind::RuntimePolicyReport:
        return kRuntimePolicyReportDependencies;
    case Kind::ProductionIntegrationDryRunReport:
        return kProductionIntegrationDryRunReportDependencies;
#define AHFL_PROVIDER_STEP_1(kind_name, domain_builder, result_field, dep1)                        \
    case Kind::kind_name: {                                                                        \
        static constexpr Kind deps[] = {Kind::dep1};                                               \
        return deps;                                                                               \
    }
#define AHFL_PROVIDER_STEP_2(kind_name, domain_builder, result_field, dep1, dep2)                  \
    case Kind::kind_name: {                                                                        \
        static constexpr Kind deps[] = {Kind::dep1, Kind::dep2};                                   \
        return deps;                                                                               \
    }
#define AHFL_PROVIDER_STEP_CUSTOM(kind_name)
#include "tooling/cli/provider/provider_step_graph.def"
#undef AHFL_PROVIDER_STEP_CUSTOM
#undef AHFL_PROVIDER_STEP_2
#undef AHFL_PROVIDER_STEP_1
    }

    return kNoDependencies;
}

} // namespace ahfl::cli
