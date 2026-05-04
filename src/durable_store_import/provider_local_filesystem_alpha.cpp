#include "ahfl/durable_store_import/provider_local_filesystem_alpha.hpp"
#include "ahfl/validation/common.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_FILESYSTEM_ALPHA";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderLocalFilesystemAlphaStatus status) {
    return status == ProviderLocalFilesystemAlphaStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string plan_identity(const ProviderSdkMockAdapterReadiness &readiness,
                                        ProviderLocalFilesystemAlphaStatus status) {
    return "durable-store-import-provider-local-filesystem-alpha-plan::" + readiness.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string result_identity(const ProviderLocalFilesystemAlphaPlan &plan,
                                          ProviderLocalFilesystemAlphaResultKind result) {
    std::string result_slug = "blocked";
    switch (result) {
    case ProviderLocalFilesystemAlphaResultKind::Accepted:
        result_slug = "accepted";
        break;
    case ProviderLocalFilesystemAlphaResultKind::DryRunOnly:
        result_slug = "dry-run-only";
        break;
    case ProviderLocalFilesystemAlphaResultKind::WriteFailed:
        result_slug = "write-failed";
        break;
    case ProviderLocalFilesystemAlphaResultKind::Blocked:
        result_slug = "blocked";
        break;
    }
    return "durable-store-import-provider-local-filesystem-alpha-result::" + plan.session_id +
           "::" + result_slug;
}

[[nodiscard]] ProviderLocalFilesystemAlphaFailureAttribution
mock_readiness_not_ready_failure(const ProviderSdkMockAdapterReadiness &readiness) {
    std::string message = "local filesystem alpha provider cannot proceed because mock adapter "
                          "readiness is not ready";
    if (readiness.failure_attribution.has_value()) {
        message = readiness.failure_attribution->message;
    }
    return ProviderLocalFilesystemAlphaFailureAttribution{
        .kind = ProviderLocalFilesystemAlphaFailureKind::MockReadinessNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderLocalFilesystemAlphaFailureAttribution
plan_not_ready_failure(const ProviderLocalFilesystemAlphaPlan &plan) {
    std::string message =
        "local filesystem alpha provider cannot run because alpha plan is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }
    return ProviderLocalFilesystemAlphaFailureAttribution{
        .kind = ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderLocalFilesystemAlphaFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_cloud_access(bool opens_network_connection,
                              bool reads_secret_material,
                              bool invokes_cloud_provider_sdk,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (opens_network_connection || reads_secret_material || invokes_cloud_provider_sdk) {
        emit_validation_error(
            diagnostics,
            std::string(owner) + " cannot open network, read secret, or invoke cloud provider SDK");
    }
}

[[nodiscard]] ProviderLocalFilesystemAlphaNextActionKind
next_action_for_result(const ProviderLocalFilesystemAlphaResult &result) {
    if (result.result_status == ProviderLocalFilesystemAlphaStatus::Ready &&
        (result.normalized_result == ProviderLocalFilesystemAlphaResultKind::Accepted ||
         result.normalized_result == ProviderLocalFilesystemAlphaResultKind::DryRunOnly)) {
        return ProviderLocalFilesystemAlphaNextActionKind::ReadyForIdempotencyContract;
    }
    if (result.failure_attribution.has_value() &&
        result.failure_attribution->kind ==
            ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady) {
        return ProviderLocalFilesystemAlphaNextActionKind::WaitForMockAdapter;
    }
    return ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderLocalFilesystemAlphaPlanValidationResult
validate_provider_local_filesystem_alpha_plan(const ProviderLocalFilesystemAlphaPlan &plan) {
    ProviderLocalFilesystemAlphaPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderLocalFilesystemAlphaPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha plan format_version must be '" +
                                  std::string(kProviderLocalFilesystemAlphaPlanFormatVersion) +
                                  "'");
    }
    if (plan.source_durable_store_import_provider_sdk_mock_adapter_readiness_format_version !=
        kProviderSdkMockAdapterReadinessFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local filesystem alpha plan source format_version must be '" +
                std::string(kProviderSdkMockAdapterReadinessFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        plan.durable_store_import_provider_local_filesystem_alpha_plan_identity.empty() ||
        plan.provider_key.empty() || plan.target_object_name.empty() ||
        plan.planned_payload_digest.empty()) {
        emit_validation_error(
            diagnostics, "provider local filesystem alpha plan identity fields must not be empty");
    }
    if (!plan.real_provider_alpha || !plan.fake_adapter_default_path_preserved ||
        !plan.opt_in_required) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha plan must remain opt-in alpha with "
                              "fake path preserved");
    }
    validate_failure(plan.failure_attribution, diagnostics, "provider local filesystem alpha plan");
    validate_no_cloud_access(plan.opens_network_connection,
                             plan.reads_secret_material,
                             plan.invokes_cloud_provider_sdk,
                             diagnostics,
                             "provider local filesystem alpha plan");
    if (plan.plan_status == ProviderLocalFilesystemAlphaStatus::Ready) {
        if (plan.source_mock_adapter_next_action !=
                ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity ||
            plan.operation_kind !=
                ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha ||
            plan.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "provider local filesystem alpha plan ready status requires "
                                  "accepted mock adapter and no failure");
        }
    } else if (plan.operation_kind ==
                   ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha ||
               !plan.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha plan blocked status requires noop "
                              "operation and failure_attribution");
    }
    return result;
}

ProviderLocalFilesystemAlphaResultValidationResult validate_provider_local_filesystem_alpha_result(
    const ProviderLocalFilesystemAlphaResult &result_record) {
    ProviderLocalFilesystemAlphaResultValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (result_record.format_version != kProviderLocalFilesystemAlphaResultFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha result format_version must be '" +
                                  std::string(kProviderLocalFilesystemAlphaResultFormatVersion) +
                                  "'");
    }
    if (result_record
            .source_durable_store_import_provider_local_filesystem_alpha_plan_format_version !=
        kProviderLocalFilesystemAlphaPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result source format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaPlanFormatVersion) + "'");
    }
    if (result_record.workflow_canonical_name.empty() || result_record.session_id.empty() ||
        result_record.input_fixture.empty() ||
        result_record.durable_store_import_provider_local_filesystem_alpha_plan_identity.empty() ||
        result_record.durable_store_import_provider_local_filesystem_alpha_result_identity
            .empty() ||
        result_record.provider_commit_reference.empty() ||
        result_record.provider_result_digest.empty()) {
        emit_validation_error(
            diagnostics,
            "provider local filesystem alpha result identity fields must not be empty");
    }
    validate_failure(
        result_record.failure_attribution, diagnostics, "provider local filesystem alpha result");
    validate_no_cloud_access(result_record.opens_network_connection,
                             result_record.reads_secret_material,
                             result_record.invokes_cloud_provider_sdk,
                             diagnostics,
                             "provider local filesystem alpha result");
    if (result_record.result_status == ProviderLocalFilesystemAlphaStatus::Ready) {
        if (result_record.source_alpha_plan_status != ProviderLocalFilesystemAlphaStatus::Ready ||
            result_record.operation_kind !=
                ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha) {
            emit_validation_error(diagnostics,
                                  "provider local filesystem alpha result ready status requires "
                                  "ready plan and alpha run");
        }
    } else if (result_record.operation_kind ==
                   ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha ||
               !result_record.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha result blocked status requires noop "
                              "operation and failure_attribution");
    }
    return result;
}

ProviderLocalFilesystemAlphaReadinessValidationResult
validate_provider_local_filesystem_alpha_readiness(
    const ProviderLocalFilesystemAlphaReadiness &readiness) {
    ProviderLocalFilesystemAlphaReadinessValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (readiness.format_version != kProviderLocalFilesystemAlphaReadinessFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha readiness format_version must be '" +
                                  std::string(kProviderLocalFilesystemAlphaReadinessFormatVersion) +
                                  "'");
    }
    if (readiness
            .source_durable_store_import_provider_local_filesystem_alpha_result_format_version !=
        kProviderLocalFilesystemAlphaResultFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider local filesystem alpha readiness source format_version must be '" +
                std::string(kProviderLocalFilesystemAlphaResultFormatVersion) + "'");
    }
    if (readiness.workflow_canonical_name.empty() || readiness.session_id.empty() ||
        readiness.input_fixture.empty() ||
        readiness.durable_store_import_provider_local_filesystem_alpha_result_identity.empty() ||
        readiness.readiness_summary.empty() || readiness.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "provider local filesystem alpha readiness fields must not be empty");
    }
    validate_failure(
        readiness.failure_attribution, diagnostics, "provider local filesystem alpha readiness");
    return result;
}

ProviderLocalFilesystemAlphaPlanResult
build_provider_local_filesystem_alpha_plan(const ProviderSdkMockAdapterReadiness &readiness) {
    ProviderLocalFilesystemAlphaPlanResult result;
    result.diagnostics.append(validate_provider_sdk_mock_adapter_readiness(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready =
        readiness.next_action == ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity;
    const auto status = ready ? ProviderLocalFilesystemAlphaStatus::Ready
                              : ProviderLocalFilesystemAlphaStatus::Blocked;
    ProviderLocalFilesystemAlphaPlan plan;
    plan.workflow_canonical_name = readiness.workflow_canonical_name;
    plan.session_id = readiness.session_id;
    plan.run_id = readiness.run_id;
    plan.input_fixture = readiness.input_fixture;
    plan.durable_store_import_provider_sdk_mock_adapter_result_identity =
        readiness.durable_store_import_provider_sdk_mock_adapter_result_identity;
    plan.source_mock_adapter_next_action = readiness.next_action;
    plan.durable_store_import_provider_local_filesystem_alpha_plan_identity =
        plan_identity(readiness, status);
    plan.plan_status = status;
    plan.target_object_name = readiness.session_id + ".ahfl-provider-alpha";
    plan.planned_payload_digest = "sha256:local-filesystem-alpha:" + readiness.session_id;
    if (ready) {
        plan.operation_kind = ProviderLocalFilesystemAlphaOperationKind::PlanLocalFilesystemAlpha;
    } else {
        plan.operation_kind = ProviderLocalFilesystemAlphaOperationKind::NoopMockReadinessNotReady;
        plan.failure_attribution = mock_readiness_not_ready_failure(readiness);
    }
    result.diagnostics.append(validate_provider_local_filesystem_alpha_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderLocalFilesystemAlphaExecutionResult
run_provider_local_filesystem_alpha(const ProviderLocalFilesystemAlphaPlan &plan) {
    ProviderLocalFilesystemAlphaExecutionResult result;
    result.diagnostics.append(validate_provider_local_filesystem_alpha_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalFilesystemAlphaResult record;
    record.workflow_canonical_name = plan.workflow_canonical_name;
    record.session_id = plan.session_id;
    record.run_id = plan.run_id;
    record.input_fixture = plan.input_fixture;
    record.durable_store_import_provider_local_filesystem_alpha_plan_identity =
        plan.durable_store_import_provider_local_filesystem_alpha_plan_identity;
    record.source_alpha_plan_status = plan.plan_status;
    record.provider_commit_reference = "local-filesystem-alpha://" + plan.target_object_name;
    record.provider_result_digest = plan.planned_payload_digest;
    if (plan.plan_status != ProviderLocalFilesystemAlphaStatus::Ready) {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::NoopAlphaPlanNotReady;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Blocked;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::Blocked;
        record.failure_attribution = plan_not_ready_failure(plan);
    } else if (!plan.opt_in_enabled) {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Ready;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::DryRunOnly;
        record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
            .kind = ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
            .message = "local filesystem alpha write is available but not enabled for default path",
        };
    } else {
        record.operation_kind = ProviderLocalFilesystemAlphaOperationKind::RunLocalFilesystemAlpha;
        record.result_status = ProviderLocalFilesystemAlphaStatus::Ready;
        record.normalized_result = ProviderLocalFilesystemAlphaResultKind::Accepted;
        record.opt_in_used = true;
        try {
            const std::filesystem::path target_dir =
                plan.target_directory.value_or(".ahfl-provider-alpha");
            std::filesystem::create_directories(target_dir);
            const auto target_path = target_dir / plan.target_object_name;
            std::ofstream out(target_path);
            out << "provider_commit_reference=" << record.provider_commit_reference << '\n';
            out << "provider_result_digest=" << record.provider_result_digest << '\n';
            record.wrote_local_file = static_cast<bool>(out);
            if (!record.wrote_local_file) {
                record.normalized_result = ProviderLocalFilesystemAlphaResultKind::WriteFailed;
                record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
                    .kind = ProviderLocalFilesystemAlphaFailureKind::FilesystemWriteFailed,
                    .message = "local filesystem alpha write failed",
                };
            }
        } catch (const std::filesystem::filesystem_error &error) {
            record.normalized_result = ProviderLocalFilesystemAlphaResultKind::WriteFailed;
            record.failure_attribution = ProviderLocalFilesystemAlphaFailureAttribution{
                .kind = ProviderLocalFilesystemAlphaFailureKind::FilesystemWriteFailed,
                .message = error.what(),
            };
        }
    }
    record.durable_store_import_provider_local_filesystem_alpha_result_identity =
        result_identity(plan, record.normalized_result);
    result.diagnostics.append(validate_provider_local_filesystem_alpha_result(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.result = std::move(record);
    return result;
}

ProviderLocalFilesystemAlphaReadinessResult build_provider_local_filesystem_alpha_readiness(
    const ProviderLocalFilesystemAlphaResult &alpha_result) {
    ProviderLocalFilesystemAlphaReadinessResult result;
    result.diagnostics.append(
        validate_provider_local_filesystem_alpha_result(alpha_result).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderLocalFilesystemAlphaReadiness readiness;
    readiness.workflow_canonical_name = alpha_result.workflow_canonical_name;
    readiness.session_id = alpha_result.session_id;
    readiness.run_id = alpha_result.run_id;
    readiness.input_fixture = alpha_result.input_fixture;
    readiness.durable_store_import_provider_local_filesystem_alpha_result_identity =
        alpha_result.durable_store_import_provider_local_filesystem_alpha_result_identity;
    readiness.result_status = alpha_result.result_status;
    readiness.normalized_result = alpha_result.normalized_result;
    readiness.readiness_summary =
        alpha_result.normalized_result == ProviderLocalFilesystemAlphaResultKind::Accepted
            ? "local filesystem alpha provider wrote an opt-in commit marker"
            : "local filesystem alpha provider stayed on safe default path";
    readiness.next_action = next_action_for_result(alpha_result);
    readiness.next_step_recommendation =
        readiness.next_action ==
                ProviderLocalFilesystemAlphaNextActionKind::ReadyForIdempotencyContract
            ? "ready for idempotency and retry contract"
            : "manual review required before idempotency contract";
    readiness.failure_attribution = alpha_result.failure_attribution;
    result.diagnostics.append(
        validate_provider_local_filesystem_alpha_readiness(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.readiness = std::move(readiness);
    return result;
}

} // namespace ahfl::durable_store_import
