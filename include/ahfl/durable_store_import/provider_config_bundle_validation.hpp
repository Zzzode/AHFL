#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ahfl/durable_store_import/provider_config.hpp"
#include "ahfl/durable_store_import/provider_registry.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract
inline constexpr std::string_view kProviderConfigBundleValidationReportFormatVersion =
    "ahfl.durable-store-import-provider-config-bundle-validation-report.v1";

// 校验单项的状态
enum class ConfigValidationStatus { Valid, Invalid, Missing };

// Provider reference 校验条目
struct ProviderReferenceCheck {
    std::string provider_name;
    ConfigValidationStatus status{ConfigValidationStatus::Missing};
    std::optional<std::string> validation_error;
};

// Secret handle 校验条目（不读取 secret value）
struct SecretHandleCheck {
    std::string secret_name;
    std::string secret_scope;
    ConfigValidationStatus presence_status{ConfigValidationStatus::Missing};
    ConfigValidationStatus scope_status{ConfigValidationStatus::Missing};
    bool has_redaction_policy{false};
};

// Endpoint shape 校验条目
struct EndpointShapeCheck {
    std::string endpoint_name;
    std::string expected_shape;
    ConfigValidationStatus status{ConfigValidationStatus::Missing};
};

// Environment binding 校验条目
struct EnvironmentBindingCheck {
    std::string binding_name;
    ConfigValidationStatus status{ConfigValidationStatus::Missing};
};

// Config bundle validation 完整报告
struct ProviderConfigBundleValidationReport {
    std::string format_version{std::string(kProviderConfigBundleValidationReportFormatVersion)};
    std::string source_durable_store_import_provider_selection_plan_format_version{
        std::string(kProviderSelectionPlanFormatVersion)};
    std::string source_durable_store_import_provider_config_snapshot_placeholder_format_version{
        std::string(kProviderConfigSnapshotPlaceholderFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string config_bundle_identity;
    std::string durable_store_import_provider_selection_plan_identity;
    std::string durable_store_import_provider_config_snapshot_identity;
    // 校验结果
    std::vector<ProviderReferenceCheck> provider_references;
    std::vector<SecretHandleCheck> secret_handles;
    std::vector<EndpointShapeCheck> endpoint_shapes;
    std::vector<EnvironmentBindingCheck> environment_bindings;
    // 汇总计数
    int valid_count{0};
    int invalid_count{0};
    int missing_count{0};
    std::string validation_summary;
    std::string blocking_summary;
    // 安全约束标记
    bool reads_secret_value{false};
    bool opens_network_connection{false};
    bool generates_production_config{false};
};

// Validation result 包装
struct ProviderConfigBundleValidationReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderConfigBundleValidationReportResult {
    std::optional<ProviderConfigBundleValidationReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

// 校验已有报告结构的合法性
[[nodiscard]] ProviderConfigBundleValidationReportValidationResult
validate_provider_config_bundle_validation_report(
    const ProviderConfigBundleValidationReport &report);

// 基于 selection plan 和 config snapshot 构建 validation report
[[nodiscard]] ProviderConfigBundleValidationReportResult
build_provider_config_bundle_validation_report(
    const ProviderSelectionPlan &selection_plan,
    const ProviderConfigSnapshotPlaceholder &config_snapshot);

} // namespace ahfl::durable_store_import
