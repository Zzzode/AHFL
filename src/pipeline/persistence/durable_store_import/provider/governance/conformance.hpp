#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/persistence/durable_store_import/provider/governance/audit.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/compatibility.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/registry.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/production_readiness.hpp"

namespace ahfl::durable_store_import {

// Provider Conformance Report format version constant
inline constexpr std::string_view kProviderConformanceReportFormatVersion =
    "ahfl.durable-store-import-provider-conformance-report.v1";

// Contract check result enum
enum class ConformanceCheckResult {
    Pass,
    Fail,
    Skipped
};

// Single contract check item
struct ConformanceCheckItem {
    std::string check_name;
    ConformanceCheckResult result;
    std::string artifact_reference;
    std::optional<std::string> failure_reason;
};

// Provider Conformance Report structure
struct ProviderConformanceReport {
    std::string format_version{std::string(kProviderConformanceReportFormatVersion)};
    std::string source_durable_store_import_provider_compatibility_report_format_version{
        std::string(kProviderCompatibilityReportFormatVersion)};
    std::string source_durable_store_import_provider_registry_format_version{
        std::string(kProviderRegistryFormatVersion)};
    std::string source_durable_store_import_provider_production_readiness_evidence_format_version{
        std::string(kProviderProductionReadinessEvidenceFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    // Upstream artifact references
    std::string durable_store_import_provider_compatibility_report_identity;
    std::string durable_store_import_provider_registry_identity;
    std::string durable_store_import_provider_production_readiness_evidence_identity;
    std::string durable_store_import_provider_conformance_report_identity;
    // Check result statistics
    int pass_count{0};
    int fail_count{0};
    int skipped_count{0};
    std::vector<ConformanceCheckItem> checks;
    std::string conformance_summary;
};

// Validation result structure
struct ProviderConformanceReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build result structure
struct ProviderConformanceReportResult {
    std::optional<ProviderConformanceReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Validation function
[[nodiscard]] ProviderConformanceReportValidationResult
validate_provider_conformance_report(const ProviderConformanceReport &report);

// Build function - consumes v0.40-v0.42 artifacts
[[nodiscard]] ProviderConformanceReportResult
build_provider_conformance_report(const ProviderCompatibilityReport &compatibility_report,
                                  const ProviderRegistry &registry,
                                  const ProviderProductionReadinessEvidence &readiness_evidence);

} // namespace ahfl::durable_store_import
