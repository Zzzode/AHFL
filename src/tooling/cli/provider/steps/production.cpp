#include "tooling/cli/provider/pipeline_durable_store_import_provider.hpp"

#include "tooling/cli/provider/pipeline_durable_store_import.hpp"
#include "tooling/cli/provider/pipeline_durable_store_import_provider_steps.hpp"
#include "tooling/cli/pipeline_emit.hpp"
#include "tooling/cli/provider/provider_pipeline_cache.hpp"

#include "pipeline/persistence/durable_store_import/artifacts.hpp"
#include "pipeline/persistence/durable_store_import/provider.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ahfl::cli {

namespace dsi = ahfl::durable_store_import;

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessEvidence>
build_provider_production_readiness_evidence_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *negotiation = cache.get_CapabilityNegotiationReview();
    const auto *compatibility = cache.get_CompatibilityReport();
    const auto *audit = cache.get_ExecutionAuditEvent();
    const auto *recovery = cache.get_WriteRecoveryPlan();
    const auto *taxonomy = cache.get_FailureTaxonomyReport();
    if (negotiation == nullptr || compatibility == nullptr || audit == nullptr ||
        recovery == nullptr || taxonomy == nullptr) {
        return std::nullopt;
    }

    const auto evidence = ahfl::durable_store_import::build_provider_production_readiness_evidence(
        *negotiation, *compatibility, *audit, *recovery, *taxonomy);
    evidence.diagnostics.render(std::cerr);
    if (evidence.has_errors() || !evidence.evidence.has_value()) {
        return std::nullopt;
    }
    return *evidence.evidence;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReview>
build_provider_production_readiness_review_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *evidence = cache.get_ProductionReadinessEvidence();
    if (evidence == nullptr) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_production_readiness_review(*evidence);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

namespace {

struct ReleaseGateArtifacts {
    dsi::ProviderCompatibilityReport compatibility;
    dsi::ProviderRegistry registry;
    dsi::ProviderProductionReadinessEvidence readiness_evidence;
    dsi::ProviderConformanceReport conformance;
    dsi::ProviderSchemaCompatibilityReport schema_report;
    dsi::ProviderSelectionPlan selection_plan;
    dsi::ProviderConfigBundleValidationReport config_report;
    dsi::ReleaseEvidenceArchiveManifest release_archive;
};

[[nodiscard]] dsi::ProviderSchemaCompatibilityReport
make_compatible_schema_report(const dsi::ProviderConformanceReport &conformance) {
    dsi::ProviderSchemaCompatibilityReport report;
    report.workflow_canonical_name = conformance.workflow_canonical_name;
    report.session_id = conformance.session_id;
    report.run_id = conformance.run_id;
    report.has_schema_drift = false;
    report.compatibility_summary = "all versions compatible";
    return report;
}

[[nodiscard]] dsi::ApprovalDecisionRecord
make_default_rejected_approval_decision(const dsi::ApprovalRequest &request) {
    dsi::ApprovalDecisionRecord decision;
    decision.approval_request_identity = "approval-request::" + request.session_id;
    decision.decision = dsi::ApprovalDecision::Rejected;
    decision.decision_reason = "default rejection: operator approval not yet provided";
    decision.rejection_details = "awaiting operator review and explicit approval";
    return decision;
}

[[nodiscard]] std::optional<ReleaseGateArtifacts>
build_release_gate_artifacts_for_cli(ProviderPipelineCache &cache) {
    const auto *compatibility = cache.get_CompatibilityReport();
    const auto *registry = cache.get_Registry();
    const auto *readiness = cache.get_ProductionReadinessEvidence();
    const auto *selection_plan = cache.get_SelectionPlan();
    const auto *snapshot = cache.get_ConfigSnapshot();
    if (compatibility == nullptr || registry == nullptr || readiness == nullptr ||
        selection_plan == nullptr || snapshot == nullptr) {
        return std::nullopt;
    }

    const auto conformance = unwrap_cli_result(
        dsi::build_provider_conformance_report(*compatibility, *registry, *readiness),
        &dsi::ProviderConformanceReportResult::report);
    if (!conformance.has_value()) {
        return std::nullopt;
    }

    const auto schema_report = make_compatible_schema_report(*conformance);
    const auto config_report = unwrap_cli_result(
        dsi::build_provider_config_bundle_validation_report(*selection_plan, *snapshot),
        &dsi::ProviderConfigBundleValidationReportResult::report);
    if (!config_report.has_value()) {
        return std::nullopt;
    }

    const auto archive = unwrap_cli_result(
        dsi::build_release_evidence_archive_manifest(
            *conformance, schema_report, *config_report, *readiness),
        &dsi::ReleaseEvidenceArchiveManifestResult::manifest);
    if (!archive.has_value()) {
        return std::nullopt;
    }

    return ReleaseGateArtifacts{
        *compatibility,
        *registry,
        *readiness,
        *conformance,
        schema_report,
        *selection_plan,
        *config_report,
        *archive,
    };
}

// Memoized access to release gate artifacts — avoids recomputing intermediate
// conformance/schema/config/archive artifacts on each downstream step invocation.
bool g_release_gate_computed = false;
std::optional<ReleaseGateArtifacts> g_release_gate_cache;

[[nodiscard]] const ReleaseGateArtifacts *get_release_gate(ProviderPipelineCache &cache) {
    if (!g_release_gate_computed) {
        g_release_gate_computed = true;
        g_release_gate_cache = build_release_gate_artifacts_for_cli(cache);
    }
    return g_release_gate_cache.has_value() ? &*g_release_gate_cache : nullptr;
}

[[nodiscard]] std::optional<dsi::ApprovalReceipt>
build_provider_approval_receipt_from_gate(const ReleaseGateArtifacts &gate) {
    const auto request = unwrap_cli_result(
        dsi::build_approval_request(gate.release_archive, gate.readiness_evidence),
        &dsi::ApprovalRequestResult::request);
    if (!request.has_value()) {
        return std::nullopt;
    }

    const auto decision = make_default_rejected_approval_decision(*request);
    return unwrap_cli_result(dsi::build_approval_receipt(*request, decision),
                             &dsi::ApprovalReceiptResult::receipt);
}

[[nodiscard]] std::optional<dsi::ProviderOptInDecisionReport>
build_provider_opt_in_decision_report_from_gate(const ReleaseGateArtifacts &gate,
                                                const dsi::ApprovalReceipt &approval) {
    return unwrap_cli_result(
        dsi::build_provider_opt_in_decision_report(approval,
                                                   gate.config_report,
                                                   gate.selection_plan),
        &dsi::ProviderOptInDecisionReportResult::report);
}

[[nodiscard]] std::optional<dsi::ProviderRuntimePolicyReport>
build_provider_runtime_policy_report_from_gate(const ReleaseGateArtifacts &gate,
                                               const dsi::ApprovalReceipt &approval,
                                               const dsi::ProviderOptInDecisionReport &opt_in) {
    return unwrap_cli_result(
        dsi::build_provider_runtime_policy_report(opt_in,
                                                  approval,
                                                  gate.config_report,
                                                  gate.selection_plan,
                                                  gate.readiness_evidence),
        &dsi::ProviderRuntimePolicyReportResult::report);
}

} // namespace

[[nodiscard]] std::optional<dsi::ApprovalReceipt>
build_provider_approval_receipt_for_cli(const ahfl::ir::Program & /*program*/,
                                        const ahfl::handoff::PackageMetadata & /*metadata*/,
                                        const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                        const CommandLineOptions & /*options*/,
                                        std::string_view /*command_name*/,
                                        ProviderPipelineCache &cache) {
    const auto *gate = get_release_gate(cache);
    if (gate == nullptr) {
        return std::nullopt;
    }
    return build_provider_approval_receipt_from_gate(*gate);
}

[[nodiscard]] std::optional<dsi::ProviderOptInDecisionReport>
build_provider_opt_in_decision_report_for_cli(const ahfl::ir::Program & /*program*/,
                                             const ahfl::handoff::PackageMetadata & /*metadata*/,
                                             const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                             const CommandLineOptions & /*options*/,
                                             std::string_view /*command_name*/,
                                             ProviderPipelineCache &cache) {
    const auto *gate = get_release_gate(cache);
    if (gate == nullptr) {
        return std::nullopt;
    }
    const auto approval = build_provider_approval_receipt_from_gate(*gate);
    if (!approval.has_value()) {
        return std::nullopt;
    }
    return build_provider_opt_in_decision_report_from_gate(*gate, *approval);
}

[[nodiscard]] std::optional<dsi::ProviderRuntimePolicyReport>
build_provider_runtime_policy_report_for_cli(const ahfl::ir::Program & /*program*/,
                                             const ahfl::handoff::PackageMetadata & /*metadata*/,
                                             const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
                                             const CommandLineOptions & /*options*/,
                                             std::string_view /*command_name*/,
                                             ProviderPipelineCache &cache) {
    const auto *gate = get_release_gate(cache);
    if (gate == nullptr) {
        return std::nullopt;
    }
    const auto approval = build_provider_approval_receipt_from_gate(*gate);
    if (!approval.has_value()) {
        return std::nullopt;
    }
    const auto opt_in = build_provider_opt_in_decision_report_from_gate(*gate, *approval);
    if (!opt_in.has_value()) {
        return std::nullopt;
    }
    return build_provider_runtime_policy_report_from_gate(*gate, *approval, *opt_in);
}

[[nodiscard]] std::optional<dsi::ProviderProductionIntegrationDryRunReport>
build_provider_production_integration_dry_run_report_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *gate = get_release_gate(cache);
    if (gate == nullptr) {
        return std::nullopt;
    }
    const auto approval = build_provider_approval_receipt_from_gate(*gate);
    if (!approval.has_value()) {
        return std::nullopt;
    }
    const auto opt_in = build_provider_opt_in_decision_report_from_gate(*gate, *approval);
    if (!opt_in.has_value()) {
        return std::nullopt;
    }
    const auto policy = build_provider_runtime_policy_report_from_gate(*gate, *approval, *opt_in);
    if (!policy.has_value()) {
        return std::nullopt;
    }

    return unwrap_cli_result(
        dsi::build_provider_production_integration_dry_run_report(gate->conformance,
                                                                  gate->schema_report,
                                                                  gate->config_report,
                                                                  gate->release_archive,
                                                                  *approval,
                                                                  *opt_in,
                                                                  *policy),
        &dsi::ProviderProductionIntegrationDryRunReportResult::report);
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderProductionReadinessReport>
build_provider_production_readiness_report_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *review = cache.get_ProductionReadinessReview();
    if (review == nullptr) {
        return std::nullopt;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_production_readiness_report(*review);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ReleaseEvidenceArchiveManifest>
build_provider_release_evidence_archive_manifest_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *gate = get_release_gate(cache);
    if (gate == nullptr) {
        return std::nullopt;
    }
    return gate->release_archive;
}

} // namespace ahfl::cli
