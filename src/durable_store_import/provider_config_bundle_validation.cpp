#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_CONFIG_BUNDLE_VALIDATION";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, std::move(message));
}

// 校验 provider reference：selection plan 中的 provider 是否在 config 中引用
[[nodiscard]] ProviderReferenceCheck check_provider_reference(
    const ProviderSelectionPlan &plan, std::string_view provider_id) {
    ProviderReferenceCheck check;
    check.provider_name = std::string(provider_id);

    if (provider_id.empty()) {
        check.status = ConfigValidationStatus::Missing;
        check.validation_error = "provider id is empty";
        return check;
    }

    // 判断 provider 是否在 selection plan 中被注册
    if (plan.selected_provider_id == provider_id ||
        plan.fallback_provider_id == provider_id) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Invalid;
        check.validation_error =
            "provider '" + std::string(provider_id) + "' not found in selection plan";
    }

    return check;
}

// 基于 config snapshot 构建 secret handle check（不读取 secret value）
[[nodiscard]] SecretHandleCheck check_secret_handle(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    SecretHandleCheck check;
    check.secret_name = snapshot.provider_config_profile_identity + ".secret-handle";
    check.secret_scope = snapshot.provider_config_profile_identity;

    if (snapshot.reads_secret_material) {
        check.presence_status = ConfigValidationStatus::Valid;
    } else {
        check.presence_status = ConfigValidationStatus::Missing;
    }

    // Scope 校验：profile identity 不能为空
    if (!snapshot.provider_config_profile_identity.empty()) {
        check.scope_status = ConfigValidationStatus::Valid;
    } else {
        check.scope_status = ConfigValidationStatus::Invalid;
    }

    // Redaction policy 要求：不允许 materialize secret value
    check.has_redaction_policy = !snapshot.materializes_secret_value;

    return check;
}

// 校验 endpoint shape：config snapshot 中是否引用了 endpoint uri
[[nodiscard]] EndpointShapeCheck check_endpoint_shape(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    EndpointShapeCheck check;
    check.endpoint_name = snapshot.provider_config_profile_identity + ".endpoint";
    check.expected_shape = "uri";

    // Snapshot 本身不含 endpoint（placeholder），检查 operation kind 是否就绪
    if (snapshot.snapshot_status == ProviderConfigStatus::Ready) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Missing;
    }

    return check;
}

// 校验 environment binding：config 是否引用了运行时环境
[[nodiscard]] EnvironmentBindingCheck check_environment_binding(
    const ProviderConfigSnapshotPlaceholder &snapshot) {
    EnvironmentBindingCheck check;
    check.binding_name = "config-snapshot." + snapshot.provider_config_profile_identity;

    if (snapshot.snapshot_status == ProviderConfigStatus::Ready) {
        check.status = ConfigValidationStatus::Valid;
    } else {
        check.status = ConfigValidationStatus::Missing;
    }

    return check;
}

// 计算各项汇总
void compute_summary(ProviderConfigBundleValidationReport &report) {
    report.valid_count = 0;
    report.invalid_count = 0;
    report.missing_count = 0;

    auto tally = [&](ConfigValidationStatus s) {
        switch (s) {
        case ConfigValidationStatus::Valid:
            ++report.valid_count;
            break;
        case ConfigValidationStatus::Invalid:
            ++report.invalid_count;
            break;
        case ConfigValidationStatus::Missing:
            ++report.missing_count;
            break;
        }
    };

    for (const auto &ref : report.provider_references) {
        tally(ref.status);
    }
    for (const auto &sh : report.secret_handles) {
        tally(sh.presence_status);
        tally(sh.scope_status);
    }
    for (const auto &ep : report.endpoint_shapes) {
        tally(ep.status);
    }
    for (const auto &eb : report.environment_bindings) {
        tally(eb.status);
    }

    report.validation_summary =
        std::to_string(report.valid_count) + " valid, " +
        std::to_string(report.invalid_count) + " invalid, " +
        std::to_string(report.missing_count) + " missing";

    if (report.invalid_count > 0) {
        report.blocking_summary =
            std::to_string(report.invalid_count) + " blocking issue(s) detected";
    } else {
        report.blocking_summary = "no blocking issues";
    }
}

} // namespace

ProviderConfigBundleValidationReportValidationResult
validate_provider_config_bundle_validation_report(
    const ProviderConfigBundleValidationReport &report) {
    ProviderConfigBundleValidationReportValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (report.format_version != kProviderConfigBundleValidationReportFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation report format_version must be '" +
                std::string(kProviderConfigBundleValidationReportFormatVersion) + "'");
    }

    if (report.source_durable_store_import_provider_selection_plan_format_version !=
        kProviderSelectionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation report source selection plan format_version "
            "must be '" +
                std::string(kProviderSelectionPlanFormatVersion) + "'");
    }

    if (report.source_durable_store_import_provider_config_snapshot_placeholder_format_version !=
        kProviderConfigSnapshotPlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation report source config snapshot format_version "
            "must be '" +
                std::string(kProviderConfigSnapshotPlaceholderFormatVersion) + "'");
    }

    if (report.workflow_canonical_name.empty() || report.session_id.empty() ||
        report.input_fixture.empty() || report.config_bundle_identity.empty() ||
        report.durable_store_import_provider_selection_plan_identity.empty() ||
        report.durable_store_import_provider_config_snapshot_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation report identity fields must not be empty");
    }

    if (report.validation_summary.empty() || report.blocking_summary.empty()) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation report summary fields must not be empty");
    }

    // 安全约束断言
    if (report.reads_secret_value) {
        emit_validation_error(diagnostics,
                              "provider config bundle validation must not read secret values");
    }
    if (report.opens_network_connection) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation must not open network connections");
    }
    if (report.generates_production_config) {
        emit_validation_error(
            diagnostics,
            "provider config bundle validation must not generate production config");
    }

    return result;
}

ProviderConfigBundleValidationReportResult
build_provider_config_bundle_validation_report(
    const ProviderSelectionPlan &selection_plan,
    const ProviderConfigSnapshotPlaceholder &config_snapshot) {
    ProviderConfigBundleValidationReportResult result;
    auto &diagnostics = result.diagnostics;

    // 前置校验：selection plan 和 snapshot 状态
    if (selection_plan.workflow_canonical_name.empty() ||
        selection_plan.session_id.empty()) {
        emit_validation_error(diagnostics,
                              "selection plan workflow_canonical_name and session_id required");
        return result;
    }

    ProviderConfigBundleValidationReport report;
    report.workflow_canonical_name = selection_plan.workflow_canonical_name;
    report.session_id = selection_plan.session_id;
    report.run_id = selection_plan.run_id;
    report.input_fixture = selection_plan.input_fixture;
    report.durable_store_import_provider_selection_plan_identity =
        selection_plan.durable_store_import_provider_selection_plan_identity;
    report.durable_store_import_provider_config_snapshot_identity =
        config_snapshot.durable_store_import_provider_config_snapshot_identity;
    report.config_bundle_identity =
        "config-bundle-validation::" + selection_plan.workflow_canonical_name +
        "::" + selection_plan.session_id;

    // 1. 校验 provider references
    report.provider_references.push_back(
        check_provider_reference(selection_plan, selection_plan.selected_provider_id));
    report.provider_references.push_back(
        check_provider_reference(selection_plan, selection_plan.fallback_provider_id));

    // 2. 校验 secret handles（基于 config snapshot，不读取 value）
    report.secret_handles.push_back(check_secret_handle(config_snapshot));

    // 3. 校验 endpoint shapes
    report.endpoint_shapes.push_back(check_endpoint_shape(config_snapshot));

    // 4. 校验 environment bindings
    report.environment_bindings.push_back(check_environment_binding(config_snapshot));

    // 汇总计数
    compute_summary(report);

    // 安全约束：确保不读取 secret value、不连接网络、不生成配置
    report.reads_secret_value = false;
    report.opens_network_connection = false;
    report.generates_production_config = false;

    result.report = std::move(report);
    return result;
}

} // namespace ahfl::durable_store_import
