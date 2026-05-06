#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/durable_store_import/provider_conformance.hpp"
#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"

namespace ahfl::durable_store_import {

// Release Evidence Archive Manifest 格式版本常量
inline constexpr std::string_view kProviderReleaseEvidenceArchiveManifestFormatVersion =
    "ahfl.durable-store-import-provider-release-evidence-archive-manifest.v1";

// 单个 evidence item 的状态描述
struct EvidenceItem {
    std::string
        evidence_type; // "conformance", "schema_compatibility", "config_validation", "readiness"
    std::string evidence_identity;    // 引用的 artifact identity
    std::string format_version;       // 该 evidence 的 format_version
    std::string digest;               // SHA-256 摘要或语义哈希
    std::string generation_timestamp; // UTC ISO 8601 时间戳
    bool is_present{false};           // evidence 是否存在
    bool is_valid{false};             // evidence 是否通过校验
};

// Release Evidence Archive Manifest - v0.46
struct ReleaseEvidenceArchiveManifest {
    std::string format_version{std::string(kProviderReleaseEvidenceArchiveManifestFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 上游 evidence artifact 引用
    std::string durable_store_import_provider_conformance_report_identity;
    std::string durable_store_import_provider_schema_compatibility_report_identity;
    std::string durable_store_import_provider_config_bundle_validation_report_identity;
    std::string durable_store_import_provider_production_readiness_evidence_identity;

    // Evidence 条目列表
    std::vector<EvidenceItem> evidence_items;

    // 统计计数
    int total_evidence_count{0};
    int present_evidence_count{0};
    int valid_evidence_count{0};
    int missing_evidence_count{0};
    int invalid_evidence_count{0};

    // 汇总摘要
    std::string archive_summary;
    std::string missing_evidence_summary;
    std::string stale_evidence_summary;
    std::string incompatible_evidence_summary;

    // Release 就绪判定
    bool is_release_ready{false};
};

// 验证结果
struct ReleaseEvidenceArchiveManifestValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果
struct ReleaseEvidenceArchiveManifestResult {
    std::optional<ReleaseEvidenceArchiveManifest> manifest;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证已构建的 manifest 结构是否合法
[[nodiscard]] ReleaseEvidenceArchiveManifestValidationResult
validate_release_evidence_archive_manifest(const ReleaseEvidenceArchiveManifest &manifest);

// 汇总所有上游 evidence 构建 release evidence archive manifest
[[nodiscard]] ReleaseEvidenceArchiveManifestResult build_release_evidence_archive_manifest(
    const ProviderConformanceReport &conformance_report,
    const ProviderSchemaCompatibilityReport &schema_report,
    const ProviderConfigBundleValidationReport &config_report,
    const ProviderProductionReadinessEvidence &readiness_evidence);

} // namespace ahfl::durable_store_import
