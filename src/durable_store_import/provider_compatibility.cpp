#include "ahfl/durable_store_import/provider_compatibility.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_COMPATIBILITY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderCompatibilityStatus status) {
    switch (status) {
    case ProviderCompatibilityStatus::Passed:
        return "passed";
    case ProviderCompatibilityStatus::Failed:
        return "failed";
    case ProviderCompatibilityStatus::Blocked:
        return "blocked";
    }
    return "blocked";
}

} // namespace

ProviderCompatibilityTestManifestValidationResult
validate_provider_compatibility_test_manifest(const ProviderCompatibilityTestManifest &manifest) {
    ProviderCompatibilityTestManifestValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (manifest.format_version != kProviderCompatibilityTestManifestFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider compatibility test manifest format_version must be '" +
                                  std::string(kProviderCompatibilityTestManifestFormatVersion) +
                                  "'");
    }
    if (manifest.source_durable_store_import_provider_operator_review_event_format_version !=
        kProviderOperatorReviewEventFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider compatibility test manifest source format_version must be '" +
                std::string(kProviderOperatorReviewEventFormatVersion) + "'");
    }
    if (manifest.workflow_canonical_name.empty() || manifest.session_id.empty() ||
        manifest.input_fixture.empty() ||
        manifest.durable_store_import_provider_operator_review_event_identity.empty() ||
        manifest.durable_store_import_provider_compatibility_test_manifest_identity.empty() ||
        manifest.capability_matrix_reference.empty()) {
        emit_validation_error(
            diagnostics, "provider compatibility test manifest identity fields must not be empty");
    }
    if (!manifest.includes_mock_adapter || !manifest.includes_local_filesystem_alpha ||
        manifest.provider_count < 2 || manifest.fixture_count < 4 ||
        manifest.external_service_required) {
        emit_validation_error(diagnostics,
                              "provider compatibility test manifest must cover mock and alpha "
                              "providers without external services");
    }
    return result;
}

ProviderFixtureMatrixValidationResult
validate_provider_fixture_matrix(const ProviderFixtureMatrix &matrix) {
    ProviderFixtureMatrixValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (matrix.format_version != kProviderFixtureMatrixFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider fixture matrix format_version must be '" +
                                  std::string(kProviderFixtureMatrixFormatVersion) + "'");
    }
    if (matrix.source_durable_store_import_provider_compatibility_test_manifest_format_version !=
        kProviderCompatibilityTestManifestFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider fixture matrix source format_version must be '" +
                                  std::string(kProviderCompatibilityTestManifestFormatVersion) +
                                  "'");
    }
    if (matrix.workflow_canonical_name.empty() || matrix.session_id.empty() ||
        matrix.input_fixture.empty() ||
        matrix.durable_store_import_provider_compatibility_test_manifest_identity.empty() ||
        matrix.durable_store_import_provider_fixture_matrix_identity.empty()) {
        emit_validation_error(diagnostics,
                              "provider fixture matrix identity fields must not be empty");
    }
    if (!matrix.covers_mock_adapter || !matrix.covers_local_filesystem_alpha ||
        !matrix.covers_success_fixture || !matrix.covers_timeout_fixture ||
        !matrix.covers_conflict_fixture || !matrix.covers_schema_mismatch_fixture ||
        matrix.provider_count < 2 || matrix.fixture_count < 4 || matrix.external_service_required) {
        emit_validation_error(diagnostics,
                              "provider fixture matrix must cover mock and alpha fixture paths "
                              "without external services");
    }
    return result;
}

ProviderCompatibilityReportValidationResult
validate_provider_compatibility_report(const ProviderCompatibilityReport &report) {
    ProviderCompatibilityReportValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (report.format_version != kProviderCompatibilityReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider compatibility report format_version must be '" +
                                  std::string(kProviderCompatibilityReportFormatVersion) + "'");
    }
    if (report.source_durable_store_import_provider_fixture_matrix_format_version !=
            kProviderFixtureMatrixFormatVersion ||
        report.source_durable_store_import_provider_telemetry_summary_format_version !=
            kProviderTelemetrySummaryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider compatibility report source format_versions must match "
                              "V0.40 fixture matrix and V0.39 telemetry summary");
    }
    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() ||
        report.durable_store_import_provider_fixture_matrix_identity.empty() ||
        report.durable_store_import_provider_telemetry_summary_identity.empty() ||
        report.durable_store_import_provider_compatibility_report_identity.empty() ||
        report.compatibility_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider compatibility report identity and summary fields must not be empty");
    }
    if (report.external_service_required) {
        emit_validation_error(
            diagnostics,
            "provider compatibility report cannot require external services by default");
    }
    return result;
}

ProviderCompatibilityTestManifestResult
build_provider_compatibility_test_manifest(const ProviderOperatorReviewEvent &review) {
    ProviderCompatibilityTestManifestResult result;
    result.diagnostics.append(validate_provider_operator_review_event(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCompatibilityTestManifest manifest;
    manifest.workflow_canonical_name = review.workflow_canonical_name;
    manifest.session_id = review.session_id;
    manifest.run_id = review.run_id;
    manifest.input_fixture = review.input_fixture;
    manifest.durable_store_import_provider_operator_review_event_identity =
        review.durable_store_import_provider_telemetry_summary_identity;
    manifest.durable_store_import_provider_compatibility_test_manifest_identity =
        "durable-store-import-provider-compatibility-test-manifest::" + manifest.session_id;
    manifest.capability_matrix_reference =
        "provider-compatibility-matrix::mock-adapter::local-filesystem-alpha";

    result.diagnostics.append(validate_provider_compatibility_test_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.manifest = std::move(manifest);
    return result;
}

ProviderFixtureMatrixResult
build_provider_fixture_matrix(const ProviderCompatibilityTestManifest &manifest) {
    ProviderFixtureMatrixResult result;
    result.diagnostics.append(validate_provider_compatibility_test_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderFixtureMatrix matrix;
    matrix.workflow_canonical_name = manifest.workflow_canonical_name;
    matrix.session_id = manifest.session_id;
    matrix.run_id = manifest.run_id;
    matrix.input_fixture = manifest.input_fixture;
    matrix.durable_store_import_provider_compatibility_test_manifest_identity =
        manifest.durable_store_import_provider_compatibility_test_manifest_identity;
    matrix.durable_store_import_provider_fixture_matrix_identity =
        "durable-store-import-provider-fixture-matrix::" + matrix.session_id;
    matrix.provider_count = manifest.provider_count;
    matrix.fixture_count = manifest.fixture_count;

    result.diagnostics.append(validate_provider_fixture_matrix(matrix).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.matrix = std::move(matrix);
    return result;
}

ProviderCompatibilityReportResult
build_provider_compatibility_report(const ProviderFixtureMatrix &matrix,
                                    const ProviderTelemetrySummary &telemetry) {
    ProviderCompatibilityReportResult result;
    result.diagnostics.append(validate_provider_fixture_matrix(matrix).diagnostics);
    result.diagnostics.append(validate_provider_telemetry_summary(telemetry).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCompatibilityReport report;
    report.workflow_canonical_name = matrix.workflow_canonical_name;
    report.session_id = matrix.session_id;
    report.run_id = matrix.run_id;
    report.input_fixture = matrix.input_fixture;
    report.durable_store_import_provider_fixture_matrix_identity =
        matrix.durable_store_import_provider_fixture_matrix_identity;
    report.durable_store_import_provider_telemetry_summary_identity =
        telemetry.durable_store_import_provider_telemetry_summary_identity;
    report.mock_adapter_compatible = matrix.covers_mock_adapter;
    report.local_filesystem_alpha_compatible = matrix.covers_local_filesystem_alpha;
    report.capability_matrix_complete =
        matrix.covers_success_fixture && matrix.covers_timeout_fixture &&
        matrix.covers_conflict_fixture && matrix.covers_schema_mismatch_fixture;
    if (telemetry.outcome == ProviderAuditOutcome::Blocked) {
        report.status = ProviderCompatibilityStatus::Blocked;
    } else if (report.mock_adapter_compatible && report.local_filesystem_alpha_compatible &&
               report.capability_matrix_complete) {
        report.status = ProviderCompatibilityStatus::Passed;
    } else {
        report.status = ProviderCompatibilityStatus::Failed;
    }
    report.durable_store_import_provider_compatibility_report_identity =
        "durable-store-import-provider-compatibility-report::" + report.session_id +
        "::" + status_slug(report.status);
    report.compatibility_summary =
        "provider compatibility suite status is " + status_slug(report.status);

    result.diagnostics.append(validate_provider_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import
