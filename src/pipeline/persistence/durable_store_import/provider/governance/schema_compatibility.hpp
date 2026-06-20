#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/base/support/diagnostics.hpp"

namespace ahfl::durable_store_import {

// Schema Compatibility Report Format Version
inline constexpr std::string_view kProviderSchemaCompatibilityReportFormatVersion =
    "ahfl.durable-store-import-provider-schema-compatibility-report.v1";

// Schema Compatibility Status
enum class SchemaCompatibilityStatus {
    Compatible,   // Version exactly matches the golden schema
    Incompatible, // Version is incompatible and needs a fix
    Unknown,      // Compatibility cannot be determined (missing golden reference)
};

// Version check result for a single artifact
struct ArtifactVersionCheck {
    std::string artifact_type;                          // artifact type identifier
    std::string artifact_identity;                      // artifact instance identifier
    std::string format_version;                         // actual format_version
    SchemaCompatibilityStatus status;                   // compatibility status
    std::optional<std::string> expected_format_version; // expected golden format_version
    std::optional<std::string> incompatibility_reason;  // incompatibility reason description
};

// Source chain check result
struct SourceChainCheck {
    std::string source_artifact_type;                  // source artifact type
    std::string source_artifact_identity;              // source artifact instance identifier
    std::string source_format_version;                 // actual source format_version
    std::string expected_source_format_version;        // expected source format_version
    bool is_compatible;                                // whether the source chain is compatible
    std::optional<std::string> incompatibility_reason; // incompatibility reason
};

// Reference Version Check（compatibility ref, registry ref, readiness ref, audit ref）
struct ReferenceVersionCheck {
    std::string reference_type;     // reference type (compatibility/registry/readiness/audit)
    std::string reference_identity; // reference identifier
    std::string referenced_format_version; // format_version of the referenced artifact
    std::string expected_format_version;   // expected format_version
    bool is_compatible;                    // whether the reference version is compatible
    std::optional<std::string> incompatibility_reason;
};

// Provider Schema Compatibility Report - v0.44
struct ProviderSchemaCompatibilityReport {
    std::string format_version{std::string(kProviderSchemaCompatibilityReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Version check results
    std::vector<ArtifactVersionCheck> version_checks;
    std::vector<SourceChainCheck> source_chain_checks;
    std::vector<ReferenceVersionCheck> reference_checks;

    // Summary statistics
    int compatible_count{0};
    int incompatible_count{0};
    int unknown_count{0};
    std::string compatibility_summary;

    // Schema drift flag
    bool has_schema_drift{false};
    std::optional<std::string> drift_details;
};

// ValidationResult types
struct ProviderSchemaCompatibilityReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSchemaCompatibilityReportResult {
    std::optional<ProviderSchemaCompatibilityReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build function - build a schema compatibility report from existing artifacts
// Checks the format_version and source chain of all provider production artifacts
[[nodiscard]] ProviderSchemaCompatibilityReportResult build_provider_schema_compatibility_report(
    const std::vector<ArtifactVersionCheck> &version_checks,
    const std::vector<SourceChainCheck> &source_chain_checks,
    const std::vector<ReferenceVersionCheck> &reference_checks,
    const std::string &workflow_canonical_name,
    const std::string &session_id,
    const std::optional<std::string> &run_id = std::nullopt);

// Validation function
[[nodiscard]] ProviderSchemaCompatibilityReportValidationResult
validate_provider_schema_compatibility_report(const ProviderSchemaCompatibilityReport &report);

} // namespace ahfl::durable_store_import
