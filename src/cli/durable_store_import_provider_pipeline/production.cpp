#include "../pipeline_durable_store_import_provider.hpp"

#include "../pipeline_durable_store_import.hpp"
#include "../pipeline_durable_store_import_provider_steps.hpp"
#include "../pipeline_emit.hpp"

#include "durable_store_import/artifacts.hpp"
#include "durable_store_import/provider.hpp"

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
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto negotiation =
        build_provider_capability_negotiation_review_for_cli(
            program, metadata, mock_set, options, command_name);
    const auto compatibility = build_provider_compatibility_report_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto audit = build_provider_execution_audit_event_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto recovery = build_provider_write_recovery_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto taxonomy = build_provider_failure_taxonomy_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!negotiation.has_value() || !compatibility.has_value() || !audit.has_value() ||
        !recovery.has_value() || !taxonomy.has_value()) {
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
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto evidence = build_provider_production_readiness_evidence_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!evidence.has_value()) {
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
build_release_gate_artifacts_for_cli(const ahfl::ir::Program &program,
                                     const ahfl::handoff::PackageMetadata &metadata,
                                     const ahfl::dry_run::CapabilityMockSet &mock_set,
                                     const CommandLineOptions &options,
                                     std::string_view command_name) {
    const auto compatibility =
        build_provider_compatibility_report_for_cli(
            program, metadata, mock_set, options, command_name);
    const auto registry = build_provider_registry_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto readiness =
        build_provider_production_readiness_evidence_for_cli(
            program, metadata, mock_set, options, command_name);
    const auto selection_plan = build_provider_selection_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto snapshot = build_provider_config_snapshot_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!compatibility.has_value() || !registry.has_value() || !readiness.has_value() ||
        !selection_plan.has_value() || !snapshot.has_value()) {
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
build_provider_approval_receipt_for_cli(const ahfl::ir::Program &program,
                                        const ahfl::handoff::PackageMetadata &metadata,
                                        const ahfl::dry_run::CapabilityMockSet &mock_set,
                                        const CommandLineOptions &options,
                                        std::string_view command_name) {
    const auto gate =
        build_release_gate_artifacts_for_cli(program, metadata, mock_set, options, command_name);
    if (!gate.has_value()) {
        return std::nullopt;
    }
    return build_provider_approval_receipt_from_gate(*gate);
}

[[nodiscard]] std::optional<dsi::ProviderOptInDecisionReport>
build_provider_opt_in_decision_report_for_cli(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options,
                                             std::string_view command_name) {
    const auto gate =
        build_release_gate_artifacts_for_cli(program, metadata, mock_set, options, command_name);
    if (!gate.has_value()) {
        return std::nullopt;
    }
    const auto approval = build_provider_approval_receipt_from_gate(*gate);
    if (!approval.has_value()) {
        return std::nullopt;
    }
    return build_provider_opt_in_decision_report_from_gate(*gate, *approval);
}

[[nodiscard]] std::optional<dsi::ProviderRuntimePolicyReport>
build_provider_runtime_policy_report_for_cli(const ahfl::ir::Program &program,
                                             const ahfl::handoff::PackageMetadata &metadata,
                                             const ahfl::dry_run::CapabilityMockSet &mock_set,
                                             const CommandLineOptions &options,
                                             std::string_view command_name) {
    const auto gate =
        build_release_gate_artifacts_for_cli(program, metadata, mock_set, options, command_name);
    if (!gate.has_value()) {
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
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto gate =
        build_release_gate_artifacts_for_cli(program, metadata, mock_set, options, command_name);
    if (!gate.has_value()) {
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
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto review = build_provider_production_readiness_review_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!review.has_value()) {
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
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto gate =
        build_release_gate_artifacts_for_cli(program, metadata, mock_set, options, command_name);
    if (!gate.has_value()) {
        return std::nullopt;
    }
    return gate->release_archive;
}

} // namespace ahfl::cli
