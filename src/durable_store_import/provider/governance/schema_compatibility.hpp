#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/support/diagnostics.hpp"

namespace ahfl::durable_store_import {

// Schema Compatibility Report Format Version
inline constexpr std::string_view kProviderSchemaCompatibilityReportFormatVersion =
    "ahfl.durable-store-import-provider-schema-compatibility-report.v1";

// Schema Compatibility Status
enum class SchemaCompatibilityStatus {
    Compatible,   // 版本完全匹配 golden schema
    Incompatible, // 版本不兼容，需要修复
    Unknown,      // 无法确定兼容性（缺少 golden reference）
};

// 单个 artifact 的版本检查结果
struct ArtifactVersionCheck {
    std::string artifact_type;                          // artifact 类型标识
    std::string artifact_identity;                      // artifact 实例标识
    std::string format_version;                         // 实际 format_version
    SchemaCompatibilityStatus status;                   // 兼容性状态
    std::optional<std::string> expected_format_version; // 期望的 golden format_version
    std::optional<std::string> incompatibility_reason;  // 不兼容原因说明
};

// Source Chain 检查结果
struct SourceChainCheck {
    std::string source_artifact_type;                  // 源 artifact 类型
    std::string source_artifact_identity;              // 源 artifact 实例标识
    std::string source_format_version;                 // 源实际 format_version
    std::string expected_source_format_version;        // 期望的源 format_version
    bool is_compatible;                                // source chain 是否兼容
    std::optional<std::string> incompatibility_reason; // 不兼容原因
};

// Reference Version Check（compatibility ref, registry ref, readiness ref, audit ref）
struct ReferenceVersionCheck {
    std::string reference_type;     // 引用类型（compatibility/registry/readiness/audit）
    std::string reference_identity; // 引用标识
    std::string referenced_format_version; // 被引用 artifact 的 format_version
    std::string expected_format_version;   // 期望的 format_version
    bool is_compatible;                    // 引用版本是否兼容
    std::optional<std::string> incompatibility_reason;
};

// Provider Schema Compatibility Report - v0.44
struct ProviderSchemaCompatibilityReport {
    std::string format_version{std::string(kProviderSchemaCompatibilityReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;

    // 版本检查结果
    std::vector<ArtifactVersionCheck> version_checks;
    std::vector<SourceChainCheck> source_chain_checks;
    std::vector<ReferenceVersionCheck> reference_checks;

    // 汇总统计
    int compatible_count{0};
    int incompatible_count{0};
    int unknown_count{0};
    std::string compatibility_summary;

    // Schema drift 标识
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

// 构建函数 - 从现有 artifact 构建 schema compatibility report
// 检查所有 provider production artifact 的 format_version 和 source chain
[[nodiscard]] ProviderSchemaCompatibilityReportResult build_provider_schema_compatibility_report(
    const std::vector<ArtifactVersionCheck> &version_checks,
    const std::vector<SourceChainCheck> &source_chain_checks,
    const std::vector<ReferenceVersionCheck> &reference_checks,
    const std::string &workflow_canonical_name,
    const std::string &session_id,
    const std::optional<std::string> &run_id = std::nullopt);

// 验证函数
[[nodiscard]] ProviderSchemaCompatibilityReportValidationResult
validate_provider_schema_compatibility_report(const ProviderSchemaCompatibilityReport &report);

} // namespace ahfl::durable_store_import
