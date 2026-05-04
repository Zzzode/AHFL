#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_audit.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderCompatibilityTestManifestFormatVersion =
    "ahfl.durable-store-import-provider-compatibility-test-manifest.v1";

inline constexpr std::string_view kProviderFixtureMatrixFormatVersion =
    "ahfl.durable-store-import-provider-fixture-matrix.v1";

inline constexpr std::string_view kProviderCompatibilityReportFormatVersion =
    "ahfl.durable-store-import-provider-compatibility-report.v1";

enum class ProviderCompatibilityStatus {
    Passed,
    Failed,
    Blocked,
};

struct ProviderCompatibilityTestManifest {
    std::string format_version{std::string(kProviderCompatibilityTestManifestFormatVersion)};
    std::string source_durable_store_import_provider_operator_review_event_format_version{
        std::string(kProviderOperatorReviewEventFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_operator_review_event_identity;
    std::string durable_store_import_provider_compatibility_test_manifest_identity;
    bool includes_mock_adapter{true};
    bool includes_local_filesystem_alpha{true};
    int provider_count{2};
    int fixture_count{6};
    std::string capability_matrix_reference;
    bool external_service_required{false};
};

struct ProviderFixtureMatrix {
    std::string format_version{std::string(kProviderFixtureMatrixFormatVersion)};
    std::string source_durable_store_import_provider_compatibility_test_manifest_format_version{
        std::string(kProviderCompatibilityTestManifestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_compatibility_test_manifest_identity;
    std::string durable_store_import_provider_fixture_matrix_identity;
    bool covers_mock_adapter{true};
    bool covers_local_filesystem_alpha{true};
    bool covers_success_fixture{true};
    bool covers_timeout_fixture{true};
    bool covers_conflict_fixture{true};
    bool covers_schema_mismatch_fixture{true};
    int provider_count{2};
    int fixture_count{6};
    bool external_service_required{false};
};

struct ProviderCompatibilityReport {
    std::string format_version{std::string(kProviderCompatibilityReportFormatVersion)};
    std::string source_durable_store_import_provider_fixture_matrix_format_version{
        std::string(kProviderFixtureMatrixFormatVersion)};
    std::string source_durable_store_import_provider_telemetry_summary_format_version{
        std::string(kProviderTelemetrySummaryFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_fixture_matrix_identity;
    std::string durable_store_import_provider_telemetry_summary_identity;
    std::string durable_store_import_provider_compatibility_report_identity;
    ProviderCompatibilityStatus status{ProviderCompatibilityStatus::Blocked};
    bool mock_adapter_compatible{false};
    bool local_filesystem_alpha_compatible{false};
    bool capability_matrix_complete{false};
    bool external_service_required{false};
    std::string compatibility_summary;
};

struct ProviderCompatibilityTestManifestValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderFixtureMatrixValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCompatibilityReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCompatibilityTestManifestResult {
    std::optional<ProviderCompatibilityTestManifest> manifest;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderFixtureMatrixResult {
    std::optional<ProviderFixtureMatrix> matrix;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCompatibilityReportResult {
    std::optional<ProviderCompatibilityReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderCompatibilityTestManifestValidationResult
validate_provider_compatibility_test_manifest(const ProviderCompatibilityTestManifest &manifest);

[[nodiscard]] ProviderFixtureMatrixValidationResult
validate_provider_fixture_matrix(const ProviderFixtureMatrix &matrix);

[[nodiscard]] ProviderCompatibilityReportValidationResult
validate_provider_compatibility_report(const ProviderCompatibilityReport &report);

[[nodiscard]] ProviderCompatibilityTestManifestResult
build_provider_compatibility_test_manifest(const ProviderOperatorReviewEvent &review);

[[nodiscard]] ProviderFixtureMatrixResult
build_provider_fixture_matrix(const ProviderCompatibilityTestManifest &manifest);

[[nodiscard]] ProviderCompatibilityReportResult
build_provider_compatibility_report(const ProviderFixtureMatrix &matrix,
                                    const ProviderTelemetrySummary &telemetry);

} // namespace ahfl::durable_store_import
