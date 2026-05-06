#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/durable_store_import/provider_audit.hpp"
#include "ahfl/durable_store_import/provider_compatibility.hpp"
#include "ahfl/durable_store_import/provider_production_readiness.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"

namespace ahfl::durable_store_import {

// Provider Conformance Report 格式版本常量
inline constexpr std::string_view kProviderConformanceReportFormatVersion =
    "ahfl.durable-store-import-provider-conformance-report.v1";

// 契约检查结果枚举
enum class ConformanceCheckResult {
    Pass,
    Fail,
    Skipped
};

// 单个契约检查项
struct ConformanceCheckItem {
    std::string check_name;
    ConformanceCheckResult result;
    std::string artifact_reference;
    std::optional<std::string> failure_reason;
};

// Provider Conformance Report 结构
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
    // 上游 artifact 引用
    std::string durable_store_import_provider_compatibility_report_identity;
    std::string durable_store_import_provider_registry_identity;
    std::string durable_store_import_provider_production_readiness_evidence_identity;
    std::string durable_store_import_provider_conformance_report_identity;
    // 检查结果统计
    int pass_count{0};
    int fail_count{0};
    int skipped_count{0};
    std::vector<ConformanceCheckItem> checks;
    std::string conformance_summary;
};

// 验证结果结构
struct ProviderConformanceReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 构建结果结构
struct ProviderConformanceReportResult {
    std::optional<ProviderConformanceReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 验证函数
[[nodiscard]] ProviderConformanceReportValidationResult
validate_provider_conformance_report(const ProviderConformanceReport &report);

// 构建函数 - 消费 v0.40-v0.42 的 artifact
[[nodiscard]] ProviderConformanceReportResult
build_provider_conformance_report(const ProviderCompatibilityReport &compatibility_report,
                                  const ProviderRegistry &registry,
                                  const ProviderProductionReadinessEvidence &readiness_evidence);

} // namespace ahfl::durable_store_import
