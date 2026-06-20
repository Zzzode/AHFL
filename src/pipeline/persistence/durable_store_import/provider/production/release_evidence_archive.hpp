#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "pipeline/persistence/durable_store_import/provider/configuration/config_bundle_validation.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/conformance.hpp"
#include "pipeline/persistence/durable_store_import/provider/production/production_readiness.hpp"
#include "pipeline/persistence/durable_store_import/provider/governance/schema_compatibility.hpp"

namespace ahfl::durable_store_import {

// Format version constant for the Release Evidence Archive Manifest
inline constexpr std::string_view kProviderReleaseEvidenceArchiveManifestFormatVersion =
    "ahfl.durable-store-import-provider-release-evidence-archive-manifest.v1";

// Status description of a single evidence item
struct EvidenceItem {
    std::string
        evidence_type; // "conformance", "schema_compatibility", "config_validation", "readiness"
    std::string evidence_identity;    // Referenced artifact identity
    std::string format_version;       // format_version of this evidence
    std::string digest;               // SHA-256 digest or semantic hash
    std::string generation_timestamp; // UTC ISO 8601 timestamp
    bool is_present{false};           // Whether the evidence is present
    bool is_valid{false};             // Whether the evidence passed validation
};

// Release Evidence Archive Manifest - v0.46
struct ReleaseEvidenceArchiveManifest {
    std::string format_version{std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // Upstream evidence artifact references
    std::string durable_store_import_provider_conformance_report_identity;
    std::string durable_store_import_provider_schema_compatibility_report_identity;
    std::string durable_store_import_provider_config_bundle_validation_report_identity;
    std::string durable_store_import_provider_production_readiness_evidence_identity;

    // Evidence item list
    std::vector<EvidenceItem> evidence_items;

    // Statistical counts
    int total_evidence_count{0};
    int present_evidence_count{0};
    int valid_evidence_count{0};
    int missing_evidence_count{0};
    int invalid_evidence_count{0};

    // Summary aggregation
    std::string archive_summary;
    std::string missing_evidence_summary;
    std::string stale_evidence_summary;
    std::string incompatible_evidence_summary;

    // Release readiness verdict
    bool is_release_ready{false};
};

// Validation result
struct ReleaseEvidenceArchiveManifestValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Build result
struct ReleaseEvidenceArchiveManifestResult {
    std::optional<ReleaseEvidenceArchiveManifest> manifest;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// Validate that the built manifest structure is well-formed
[[nodiscard]] ReleaseEvidenceArchiveManifestValidationResult
validate_release_evidence_archive_manifest(const ReleaseEvidenceArchiveManifest &manifest);

// Aggregate all upstream evidence to build the release evidence archive manifest
[[nodiscard]] ReleaseEvidenceArchiveManifestResult build_release_evidence_archive_manifest(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderProductionReadinessEvidence &readiness_evidence);

} // namespace ahfl::durable_store_import
