#include "../provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_execution_audit_event_for_cli,
    ProviderExecutionAuditEvent,
    FailureTaxonomyReport,
    build_provider_execution_audit_event,
    event)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_telemetry_summary_for_cli,
    ProviderTelemetrySummary,
    ExecutionAuditEvent,
    build_provider_telemetry_summary,
    summary)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_operator_review_event_for_cli,
    ProviderOperatorReviewEvent,
    TelemetrySummary,
    build_provider_operator_review_event,
    review)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_compatibility_test_manifest_for_cli,
    ProviderCompatibilityTestManifest,
    OperatorReviewEvent,
    build_provider_compatibility_test_manifest,
    manifest)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_fixture_matrix_for_cli,
    ProviderFixtureMatrix,
    CompatibilityTestManifest,
    build_provider_fixture_matrix,
    matrix)

AHFL_DEFINE_PROVIDER_STEP_2(
    build_provider_compatibility_report_for_cli,
    ProviderCompatibilityReport,
    FixtureMatrix,
    TelemetrySummary,
    build_provider_compatibility_report,
    report)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_registry_for_cli,
    ProviderRegistry,
    CompatibilityReport,
    build_provider_registry,
    registry)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_selection_plan_for_cli,
    ProviderSelectionPlan,
    Registry,
    build_provider_selection_plan,
    plan)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_capability_negotiation_review_for_cli,
    ProviderCapabilityNegotiationReview,
    SelectionPlan,
    build_provider_capability_negotiation_review,
    review)

// --- Complex builders (manual implementations) ---

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConformanceReport>
build_provider_conformance_report_for_cli(
    const ahfl::ir::Program & /*program*/,
    const ahfl::handoff::PackageMetadata & /*metadata*/,
    const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,
    const CommandLineOptions & /*options*/,
    std::string_view /*command_name*/,
    ProviderPipelineCache &cache) {
    const auto *compatibility = cache.get_CompatibilityReport();
    if (compatibility == nullptr) {
        return std::nullopt;
    }
    const auto *registry = cache.get_Registry();
    if (registry == nullptr) {
        return std::nullopt;
    }
    const auto *evidence = cache.get_ProductionReadinessEvidence();
    if (evidence == nullptr) {
        return std::nullopt;
    }

    const auto report = ahfl::durable_store_import::build_provider_conformance_report(
        *compatibility, *registry, *evidence);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSchemaCompatibilityReport>
build_provider_schema_compatibility_report_for_cli(
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

    std::vector<ahfl::durable_store_import::ArtifactVersionCheck> version_checks;
    version_checks.push_back({
        "provider-production-readiness-evidence",
        evidence->durable_store_import_provider_production_readiness_evidence_identity,
        evidence->format_version,
        ahfl::durable_store_import::SchemaCompatibilityStatus::Compatible,
        std::string(ahfl::durable_store_import::kProviderProductionReadinessEvidenceFormatVersion),
        std::nullopt,
    });

    std::vector<ahfl::durable_store_import::SourceChainCheck> source_chain_checks;
    source_chain_checks.push_back({
        "provider-capability-negotiation-review",
        evidence->durable_store_import_provider_selection_plan_identity,
        evidence->source_durable_store_import_provider_capability_negotiation_review_format_version,
        std::string(ahfl::durable_store_import::kProviderCapabilityNegotiationReviewFormatVersion),
        evidence->source_durable_store_import_provider_capability_negotiation_review_format_version ==
            ahfl::durable_store_import::kProviderCapabilityNegotiationReviewFormatVersion,
        std::nullopt,
    });
    source_chain_checks.push_back({
        "provider-compatibility-report",
        evidence->durable_store_import_provider_compatibility_report_identity,
        evidence->source_durable_store_import_provider_compatibility_report_format_version,
        std::string(ahfl::durable_store_import::kProviderCompatibilityReportFormatVersion),
        evidence->source_durable_store_import_provider_compatibility_report_format_version ==
            ahfl::durable_store_import::kProviderCompatibilityReportFormatVersion,
        std::nullopt,
    });
    source_chain_checks.push_back({
        "provider-failure-taxonomy-report",
        evidence->durable_store_import_provider_failure_taxonomy_report_identity,
        evidence->source_durable_store_import_provider_failure_taxonomy_report_format_version,
        std::string(ahfl::durable_store_import::kProviderFailureTaxonomyReportFormatVersion),
        evidence->source_durable_store_import_provider_failure_taxonomy_report_format_version ==
            ahfl::durable_store_import::kProviderFailureTaxonomyReportFormatVersion,
        std::nullopt,
    });

    std::vector<ahfl::durable_store_import::ReferenceVersionCheck> reference_checks;

    const auto report = ahfl::durable_store_import::build_provider_schema_compatibility_report(
        version_checks,
        source_chain_checks,
        reference_checks,
        evidence->workflow_canonical_name,
        evidence->session_id,
        evidence->run_id);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

} // namespace ahfl::cli
