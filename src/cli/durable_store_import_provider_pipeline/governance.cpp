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

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderExecutionAuditEvent>
build_provider_execution_audit_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_provider_failure_taxonomy_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto event = ahfl::durable_store_import::build_provider_execution_audit_event(*report);
    event.diagnostics.render(std::cerr);
    if (event.has_errors() || !event.event.has_value()) {
        return std::nullopt;
    }
    return *event.event;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderTelemetrySummary>
build_provider_telemetry_summary_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto event = build_provider_execution_audit_event_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!event.has_value()) {
        return std::nullopt;
    }

    const auto summary = ahfl::durable_store_import::build_provider_telemetry_summary(*event);
    summary.diagnostics.render(std::cerr);
    if (summary.has_errors() || !summary.summary.has_value()) {
        return std::nullopt;
    }
    return *summary.summary;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderOperatorReviewEvent>
build_provider_operator_review_event_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto summary = build_provider_telemetry_summary_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!summary.has_value()) {
        return std::nullopt;
    }

    const auto review = ahfl::durable_store_import::build_provider_operator_review_event(*summary);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityTestManifest>
build_provider_compatibility_test_manifest_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto review = build_provider_operator_review_event_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!review.has_value()) {
        return std::nullopt;
    }

    const auto manifest =
        ahfl::durable_store_import::build_provider_compatibility_test_manifest(*review);
    manifest.diagnostics.render(std::cerr);
    if (manifest.has_errors() || !manifest.manifest.has_value()) {
        return std::nullopt;
    }
    return *manifest.manifest;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderFixtureMatrix>
build_provider_fixture_matrix_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto manifest = build_provider_compatibility_test_manifest_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!manifest.has_value()) {
        return std::nullopt;
    }

    const auto matrix = ahfl::durable_store_import::build_provider_fixture_matrix(*manifest);
    matrix.diagnostics.render(std::cerr);
    if (matrix.has_errors() || !matrix.matrix.has_value()) {
        return std::nullopt;
    }
    return *matrix.matrix;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCompatibilityReport>
build_provider_compatibility_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto matrix = build_provider_fixture_matrix_for_cli(
        program, metadata, mock_set, options, command_name);
    const auto telemetry = build_provider_telemetry_summary_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!matrix.has_value() || !telemetry.has_value()) {
        return std::nullopt;
    }

    const auto report =
        ahfl::durable_store_import::build_provider_compatibility_report(*matrix, *telemetry);
    report.diagnostics.render(std::cerr);
    if (report.has_errors() || !report.report.has_value()) {
        return std::nullopt;
    }
    return *report.report;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderRegistry>
build_provider_registry_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto report = build_provider_compatibility_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!report.has_value()) {
        return std::nullopt;
    }

    const auto registry = ahfl::durable_store_import::build_provider_registry(*report);
    registry.diagnostics.render(std::cerr);
    if (registry.has_errors() || !registry.registry.has_value()) {
        return std::nullopt;
    }
    return *registry.registry;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderSelectionPlan>
build_provider_selection_plan_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto registry = build_provider_registry_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!registry.has_value()) {
        return std::nullopt;
    }

    const auto plan = ahfl::durable_store_import::build_provider_selection_plan(*registry);
    plan.diagnostics.render(std::cerr);
    if (plan.has_errors() || !plan.plan.has_value()) {
        return std::nullopt;
    }
    return *plan.plan;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderCapabilityNegotiationReview>
build_provider_capability_negotiation_review_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto plan = build_provider_selection_plan_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!plan.has_value()) {
        return std::nullopt;
    }

    const auto review =
        ahfl::durable_store_import::build_provider_capability_negotiation_review(*plan);
    review.diagnostics.render(std::cerr);
    if (review.has_errors() || !review.review.has_value()) {
        return std::nullopt;
    }
    return *review.review;
}

[[nodiscard]] std::optional<ahfl::durable_store_import::ProviderConformanceReport>
build_provider_conformance_report_for_cli(
    const ahfl::ir::Program &program,
    const ahfl::handoff::PackageMetadata &metadata,
    const ahfl::dry_run::CapabilityMockSet &mock_set,
    const CommandLineOptions &options,
    std::string_view command_name) {
    const auto compatibility = build_provider_compatibility_report_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!compatibility.has_value()) {
        return std::nullopt;
    }
    const auto registry = build_provider_registry_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!registry.has_value()) {
        return std::nullopt;
    }
    const auto evidence = build_provider_production_readiness_evidence_for_cli(
        program, metadata, mock_set, options, command_name);
    if (!evidence.has_value()) {
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

    std::vector<ahfl::durable_store_import::ArtifactVersionCheck> version_checks;
    version_checks.push_back({
        "provider-production-readiness-evidence",
        evidence->durable_store_import_provider_production_readiness_evidence_identity,
        evidence->format_version,
        ahfl::durable_store_import::SchemaCompatibilityStatus::Compatible,
        std::string(ahfl::durable_store_import::kProviderProductionReadinessEvidenceFormatVersion),
        std::nullopt,
    });

    // 构建 source chain checks - 检查各 source 版本是否与期望一致
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
