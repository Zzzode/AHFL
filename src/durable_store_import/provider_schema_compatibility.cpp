#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_SCHEMA_COMPATIBILITY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(SchemaCompatibilityStatus status) {
    switch (status) {
    case SchemaCompatibilityStatus::Compatible:
        return "compatible";
    case SchemaCompatibilityStatus::Incompatible:
        return "incompatible";
    case SchemaCompatibilityStatus::Unknown:
        return "unknown";
    }
    return "unknown";
}

} // namespace

ProviderSchemaCompatibilityReportValidationResult
validate_provider_schema_compatibility_report(const ProviderSchemaCompatibilityReport &report) {
    ProviderSchemaCompatibilityReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    // 校验 format version
    if (report.format_version != kProviderSchemaCompatibilityReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider schema compatibility report format_version must be '" +
                                  std::string(kProviderSchemaCompatibilityReportFormatVersion) +
                                  "'");
    }

    // 校验必填字段
    if (report.workflow_canonical_name.empty() || report.session_id.empty()) {
        emit_validation_error(diagnostics,
                              "provider schema compatibility report identity fields must not "
                              "be empty");
    }

    // 校验兼容性汇总不为空
    if (report.compatibility_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider schema compatibility report compatibility_summary must not be empty");
    }

    // 校验汇总计数与检查项一致
    const int total =
        report.compatible_count + report.incompatible_count + report.unknown_count;
    const int expected_total = static_cast<int>(report.version_checks.size());
    if (total != expected_total) {
        emit_validation_error(
            diagnostics,
            "provider schema compatibility report summary counts ("
                + std::to_string(total) + ") do not match version_checks count ("
                + std::to_string(expected_total) + ")");
    }

    return result;
}

ProviderSchemaCompatibilityReportResult
build_provider_schema_compatibility_report(
    const std::vector<ArtifactVersionCheck> &version_checks,
    const std::vector<SourceChainCheck> &source_chain_checks,
    const std::vector<ReferenceVersionCheck> &reference_checks,
    const std::string &workflow_canonical_name,
    const std::string &session_id,
    const std::optional<std::string> &run_id) {
    ProviderSchemaCompatibilityReportResult result;

    // 校验输入不为空
    if (workflow_canonical_name.empty() || session_id.empty()) {
        emit_validation_error(result.diagnostics,
                              "workflow_canonical_name and session_id must not be empty");
        return result;
    }

    ProviderSchemaCompatibilityReport report;
    report.workflow_canonical_name = workflow_canonical_name;
    report.session_id = session_id;
    report.run_id = run_id;
    report.version_checks = version_checks;
    report.source_chain_checks = source_chain_checks;
    report.reference_checks = reference_checks;

    // 计算汇总
    int compatible = 0;
    int incompatible = 0;
    int unknown = 0;
    for (const auto &check : version_checks) {
        switch (check.status) {
        case SchemaCompatibilityStatus::Compatible:
            ++compatible;
            break;
        case SchemaCompatibilityStatus::Incompatible:
            ++incompatible;
            break;
        case SchemaCompatibilityStatus::Unknown:
            ++unknown;
            break;
        }
    }
    report.compatible_count = compatible;
    report.incompatible_count = incompatible;
    report.unknown_count = unknown;

    // 检查 source chain 是否有不兼容项
    bool source_chain_drift = false;
    for (const auto &chain : source_chain_checks) {
        if (!chain.is_compatible) {
            source_chain_drift = true;
            break;
        }
    }

    // 检查 reference version 是否有不兼容项
    bool reference_drift = false;
    for (const auto &ref : reference_checks) {
        if (!ref.is_compatible) {
            reference_drift = true;
            break;
        }
    }

    // 判定 schema drift
    report.has_schema_drift = (incompatible > 0) || source_chain_drift || reference_drift;
    if (report.has_schema_drift) {
        report.drift_details = "schema drift detected: " + std::to_string(incompatible) +
                               " incompatible version(s)";
        if (source_chain_drift) {
            report.drift_details.value() += ", source chain incompatibility";
        }
        if (reference_drift) {
            report.drift_details.value() += ", reference version incompatibility";
        }
    }

    // 构造汇总
    report.compatibility_summary =
        "provider schema compatibility: " + std::to_string(compatible) + " compatible, " +
        std::to_string(incompatible) + " incompatible, " + std::to_string(unknown) +
        " unknown";

    // 验证构建结果
    result.diagnostics.append(validate_provider_schema_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import
