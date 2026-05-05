#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_RELEASE_EVIDENCE_ARCHIVE";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

// 构建单个 evidence item（从 conformance report）
[[nodiscard]] EvidenceItem make_conformance_evidence_item(
    const ProviderConformanceReport &report) {
    EvidenceItem item;
    item.evidence_type = "conformance";
    item.evidence_identity = report.durable_store_import_provider_conformance_report_identity;
    item.format_version = report.format_version;
    item.digest = "sha256:conformance:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.durable_store_import_provider_conformance_report_identity.empty();
    item.is_valid = (report.fail_count == 0);
    return item;
}

// 构建单个 evidence item（从 schema compatibility report）
[[nodiscard]] EvidenceItem make_schema_compatibility_evidence_item(
    const ProviderSchemaCompatibilityReport &report) {
    EvidenceItem item;
    item.evidence_type = "schema_compatibility";
    item.evidence_identity = "schema-compatibility-report::" + report.session_id;
    item.format_version = report.format_version;
    item.digest = "sha256:schema:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.format_version.empty();
    item.is_valid = !report.has_schema_drift;
    return item;
}

// 构建单个 evidence item（从 config bundle validation report）
[[nodiscard]] EvidenceItem make_config_validation_evidence_item(
    const ProviderConfigBundleValidationReport &report) {
    EvidenceItem item;
    item.evidence_type = "config_validation";
    item.evidence_identity = report.config_bundle_identity;
    item.format_version = report.format_version;
    item.digest = "sha256:config:" + report.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present = !report.config_bundle_identity.empty();
    item.is_valid = (report.invalid_count == 0 && !report.reads_secret_value &&
                     !report.opens_network_connection);
    return item;
}

// 构建单个 evidence item（从 production readiness evidence）
[[nodiscard]] EvidenceItem make_readiness_evidence_item(
    const ProviderProductionReadinessEvidence &evidence) {
    EvidenceItem item;
    item.evidence_type = "readiness";
    item.evidence_identity =
        evidence.durable_store_import_provider_production_readiness_evidence_identity;
    item.format_version = evidence.format_version;
    item.digest = "sha256:readiness:" + evidence.session_id;
    item.generation_timestamp = "deterministic";
    item.is_present =
        !evidence.durable_store_import_provider_production_readiness_evidence_identity.empty();
    item.is_valid = (evidence.blocking_issue_count == 0 &&
                     evidence.security_evidence_passed &&
                     evidence.recovery_evidence_passed &&
                     evidence.compatibility_evidence_passed);
    return item;
}

} // namespace

ReleaseEvidenceArchiveManifestValidationResult validate_release_evidence_archive_manifest(
    const ReleaseEvidenceArchiveManifest &manifest) {
    ReleaseEvidenceArchiveManifestValidationResult result;
    auto &diagnostics = result.diagnostics;

    // 检查 format_version
    if (manifest.format_version != kProviderReleaseEvidenceArchiveManifestFormatVersion) {
        emit_validation_error(
            diagnostics,
            "release evidence archive manifest format_version must be '" +
                std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion) + "'");
    }

    // 检查必需字段非空
    if (manifest.workflow_canonical_name.empty() || manifest.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "release evidence archive manifest workflow_canonical_name and session_id must not be empty");
    }

    // 检查上游 identity 引用非空
    if (manifest.durable_store_import_provider_conformance_report_identity.empty() ||
        manifest.durable_store_import_provider_schema_compatibility_report_identity.empty() ||
        manifest.durable_store_import_provider_config_bundle_validation_report_identity.empty() ||
        manifest.durable_store_import_provider_production_readiness_evidence_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "release evidence archive manifest upstream evidence identities must not be empty");
    }

    // 检查 archive_summary 非空
    if (manifest.archive_summary.empty()) {
        emit_validation_error(diagnostics,
                              "release evidence archive manifest archive_summary must not be empty");
    }

    // 检查计数非负
    if (manifest.total_evidence_count < 0 || manifest.present_evidence_count < 0 ||
        manifest.valid_evidence_count < 0 || manifest.missing_evidence_count < 0 ||
        manifest.invalid_evidence_count < 0) {
        emit_validation_error(diagnostics,
                              "release evidence archive manifest counts cannot be negative");
    }

    // 检查计数一致性
    if (manifest.total_evidence_count !=
        manifest.present_evidence_count + manifest.missing_evidence_count) {
        emit_validation_error(
            diagnostics,
            "release evidence archive manifest total_evidence_count must equal present + missing");
    }

    return result;
}

ReleaseEvidenceArchiveManifestResult build_release_evidence_archive_manifest(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderProductionReadinessEvidence &readiness_evidence) {
    ReleaseEvidenceArchiveManifestResult result;

    // 验证上游 artifact 基础可用性
    result.diagnostics.append(
        validate_provider_conformance_report(conformance_report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ReleaseEvidenceArchiveManifest manifest;
    manifest.workflow_canonical_name = conformance_report.workflow_canonical_name;
    manifest.session_id = conformance_report.session_id;
    manifest.run_id = conformance_report.run_id;

    // 设置上游 artifact identity 引用
    manifest.durable_store_import_provider_conformance_report_identity =
        conformance_report.durable_store_import_provider_conformance_report_identity;
    manifest.durable_store_import_provider_schema_compatibility_report_identity =
        "schema-compatibility-report::" + schema_report.session_id;
    manifest.durable_store_import_provider_config_bundle_validation_report_identity =
        config_report.config_bundle_identity;
    manifest.durable_store_import_provider_production_readiness_evidence_identity =
        readiness_evidence.durable_store_import_provider_production_readiness_evidence_identity;

    // 构建 evidence items
    std::vector<EvidenceItem> items;
    items.push_back(make_conformance_evidence_item(conformance_report));
    items.push_back(make_schema_compatibility_evidence_item(schema_report));
    items.push_back(make_config_validation_evidence_item(config_report));
    items.push_back(make_readiness_evidence_item(readiness_evidence));

    // 计算统计
    int present_count = 0;
    int valid_count = 0;
    int missing_count = 0;
    int invalid_count = 0;
    for (const auto &item : items) {
        if (item.is_present) {
            ++present_count;
            if (item.is_valid) {
                ++valid_count;
            } else {
                ++invalid_count;
            }
        } else {
            ++missing_count;
        }
    }

    manifest.total_evidence_count = static_cast<int>(items.size());
    manifest.present_evidence_count = present_count;
    manifest.valid_evidence_count = valid_count;
    manifest.missing_evidence_count = missing_count;
    manifest.invalid_evidence_count = invalid_count;
    manifest.evidence_items = std::move(items);

    // 生成汇总摘要
    manifest.is_release_ready =
        (missing_count == 0 && invalid_count == 0 && valid_count == manifest.total_evidence_count);

    if (manifest.is_release_ready) {
        manifest.archive_summary =
            "all evidence present and valid; release ready";
    } else {
        manifest.archive_summary =
            "release not ready: " + std::to_string(missing_count) + " missing, " +
            std::to_string(invalid_count) + " invalid of " +
            std::to_string(manifest.total_evidence_count) + " total evidence items";
    }

    // Missing evidence summary
    if (missing_count > 0) {
        std::string missing_details;
        for (const auto &item : manifest.evidence_items) {
            if (!item.is_present) {
                if (!missing_details.empty()) {
                    missing_details += ", ";
                }
                missing_details += item.evidence_type;
            }
        }
        manifest.missing_evidence_summary = "missing: " + missing_details;
    } else {
        manifest.missing_evidence_summary = "none";
    }

    // Stale evidence summary (deterministic placeholder)
    manifest.stale_evidence_summary = "none";

    // Incompatible evidence summary
    if (invalid_count > 0) {
        std::string incompatible_details;
        for (const auto &item : manifest.evidence_items) {
            if (item.is_present && !item.is_valid) {
                if (!incompatible_details.empty()) {
                    incompatible_details += ", ";
                }
                incompatible_details += item.evidence_type;
            }
        }
        manifest.incompatible_evidence_summary = "invalid: " + incompatible_details;
    } else {
        manifest.incompatible_evidence_summary = "none";
    }

    // 验证生成的 manifest
    result.diagnostics.append(validate_release_evidence_archive_manifest(manifest).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.manifest = std::move(manifest);
    return result;
}

} // namespace ahfl::durable_store_import
