#include "ahfl/backends/durable_store_import_provider_release_evidence_archive_manifest.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_release_evidence_archive_manifest(
    const durable_store_import::ReleaseEvidenceArchiveManifest &manifest, std::ostream &out) {
    out << manifest.format_version << '\n';
    out << "workflow " << manifest.workflow_canonical_name << '\n';
    out << "session " << manifest.session_id << '\n';
    if (manifest.run_id.has_value()) {
        out << "run_id " << *manifest.run_id << '\n';
    }
    out << "conformance_report "
        << manifest.durable_store_import_provider_conformance_report_identity << '\n';
    out << "schema_compatibility_report "
        << manifest.durable_store_import_provider_schema_compatibility_report_identity << '\n';
    out << "config_bundle_validation_report "
        << manifest.durable_store_import_provider_config_bundle_validation_report_identity << '\n';
    out << "production_readiness_evidence "
        << manifest.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "total_evidence_count " << manifest.total_evidence_count << '\n';
    out << "present_evidence_count " << manifest.present_evidence_count << '\n';
    out << "valid_evidence_count " << manifest.valid_evidence_count << '\n';
    out << "missing_evidence_count " << manifest.missing_evidence_count << '\n';
    out << "invalid_evidence_count " << manifest.invalid_evidence_count << '\n';
    out << "is_release_ready " << (manifest.is_release_ready ? "true" : "false") << '\n';
    out << "evidence_items " << manifest.evidence_items.size() << '\n';
    for (const auto &item : manifest.evidence_items) {
        out << "evidence " << item.evidence_type << ' ' << item.evidence_identity << ' '
            << item.format_version << ' ' << (item.is_present ? "present" : "missing") << ' '
            << (item.is_valid ? "valid" : "invalid") << '\n';
    }
    out << "archive_summary " << manifest.archive_summary << '\n';
    out << "missing_evidence_summary " << manifest.missing_evidence_summary << '\n';
    out << "stale_evidence_summary " << manifest.stale_evidence_summary << '\n';
    out << "incompatible_evidence_summary " << manifest.incompatible_evidence_summary << '\n';
}

} // namespace ahfl
