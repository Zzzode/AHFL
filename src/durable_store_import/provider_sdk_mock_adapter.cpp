#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_SDK_MOCK_ADAPTER";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderSdkMockAdapterStatus status) {
    return status == ProviderSdkMockAdapterStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string scenario_slug(ProviderSdkMockAdapterScenarioKind scenario) {
    switch (scenario) {
    case ProviderSdkMockAdapterScenarioKind::Success:
        return "success";
    case ProviderSdkMockAdapterScenarioKind::Failure:
        return "failure";
    case ProviderSdkMockAdapterScenarioKind::Timeout:
        return "timeout";
    case ProviderSdkMockAdapterScenarioKind::Throttle:
        return "throttle";
    case ProviderSdkMockAdapterScenarioKind::Conflict:
        return "conflict";
    case ProviderSdkMockAdapterScenarioKind::SchemaMismatch:
        return "schema-mismatch";
    }
    return "unknown";
}

[[nodiscard]] std::string contract_identity(const ProviderSdkPayloadAuditSummary &audit,
                                            ProviderSdkMockAdapterStatus status) {
    return "durable-store-import-provider-sdk-mock-adapter-contract::" + audit.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::string result_identity(const ProviderSdkMockAdapterContract &contract) {
    return "durable-store-import-provider-sdk-mock-adapter-result::" + contract.session_id +
           "::" + scenario_slug(contract.scenario_kind);
}

[[nodiscard]] ProviderSdkMockAdapterFailureAttribution
payload_not_ready_failure(const ProviderSdkPayloadAuditSummary &audit) {
    std::string message =
        "mock adapter contract cannot proceed because SDK payload audit is not ready";
    if (audit.failure_attribution.has_value()) {
        message = audit.failure_attribution->message;
    }
    return ProviderSdkMockAdapterFailureAttribution{
        .kind = ProviderSdkMockAdapterFailureKind::PayloadNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderSdkMockAdapterFailureAttribution
contract_not_ready_failure(const ProviderSdkMockAdapterContract &contract) {
    std::string message =
        "mock adapter execution cannot proceed because mock adapter contract is not ready";
    if (contract.failure_attribution.has_value()) {
        message = contract.failure_attribution->message;
    }
    return ProviderSdkMockAdapterFailureAttribution{
        .kind = ProviderSdkMockAdapterFailureKind::ContractNotReady,
        .message = std::move(message),
    };
}

void validate_failure(const std::optional<ProviderSdkMockAdapterFailureAttribution> &failure,
                      DiagnosticBag &diagnostics,
                      std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_real_provider_access(bool opens_network_connection,
                                      bool reads_secret_material,
                                      bool materializes_credential_material,
                                      bool invokes_real_provider_sdk,
                                      DiagnosticBag &diagnostics,
                                      std::string_view owner) {
    if (opens_network_connection || reads_secret_material || materializes_credential_material ||
        invokes_real_provider_sdk) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " cannot open network, read secret, materialize credential "
                                  "material, or invoke real provider SDK");
    }
}

[[nodiscard]] ProviderSdkMockAdapterNextActionKind
next_action_for_result(const ProviderSdkMockAdapterExecutionResult &result) {
    if (result.result_status == ProviderSdkMockAdapterStatus::Ready &&
        result.normalized_result == ProviderSdkMockAdapterNormalizedResultKind::Accepted) {
        return ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity;
    }
    if (result.failure_attribution.has_value() &&
        result.failure_attribution->kind == ProviderSdkMockAdapterFailureKind::ContractNotReady) {
        return ProviderSdkMockAdapterNextActionKind::WaitForPayload;
    }
    return ProviderSdkMockAdapterNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderSdkMockAdapterContractValidationResult
validate_provider_sdk_mock_adapter_contract(const ProviderSdkMockAdapterContract &contract) {
    ProviderSdkMockAdapterContractValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (contract.format_version != kProviderSdkMockAdapterContractFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter contract format_version must be '" +
                                  std::string(kProviderSdkMockAdapterContractFormatVersion) + "'");
    }
    if (contract.source_durable_store_import_provider_sdk_payload_audit_summary_format_version !=
        kProviderSdkPayloadAuditSummaryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter contract source format_version must be '" +
                                  std::string(kProviderSdkPayloadAuditSummaryFormatVersion) + "'");
    }
    if (contract.workflow_canonical_name.empty() || contract.session_id.empty() ||
        contract.input_fixture.empty() ||
        contract.durable_store_import_provider_sdk_payload_plan_identity.empty() ||
        contract.durable_store_import_provider_sdk_mock_adapter_contract_identity.empty() ||
        contract.provider_request_payload_schema_ref.empty() || contract.payload_digest.empty()) {
        emit_validation_error(
            diagnostics, "provider SDK mock adapter contract identity fields must not be empty");
    }
    if (!contract.fake_provider_only ||
        contract.provider_request_payload_schema_ref != kProviderFakeSdkPayloadSchemaVersion) {
        emit_validation_error(
            diagnostics, "provider SDK mock adapter contract must target fake provider schema");
    }
    validate_failure(
        contract.failure_attribution, diagnostics, "provider SDK mock adapter contract");
    validate_no_real_provider_access(contract.opens_network_connection,
                                     contract.reads_secret_material,
                                     contract.materializes_credential_material,
                                     contract.invokes_real_provider_sdk,
                                     diagnostics,
                                     "provider SDK mock adapter contract");
    if (contract.contract_status == ProviderSdkMockAdapterStatus::Ready) {
        if (contract.source_payload_next_action !=
                ProviderSdkPayloadNextActionKind::ReadyForMockAdapter ||
            contract.operation_kind !=
                ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract ||
            contract.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "provider SDK mock adapter contract ready status requires ready "
                                  "payload and no failure");
        }
    } else if (contract.operation_kind ==
                   ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract ||
               !contract.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter contract blocked status requires noop "
                              "operation and failure_attribution");
    }
    return result;
}

ProviderSdkMockAdapterResultValidationResult validate_provider_sdk_mock_adapter_execution_result(
    const ProviderSdkMockAdapterExecutionResult &result_record) {
    ProviderSdkMockAdapterResultValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (result_record.format_version != kProviderSdkMockAdapterExecutionResultFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider SDK mock adapter execution result format_version must be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (result_record
            .source_durable_store_import_provider_sdk_mock_adapter_contract_format_version !=
        kProviderSdkMockAdapterContractFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider SDK mock adapter execution result source format_version must be '" +
                std::string(kProviderSdkMockAdapterContractFormatVersion) + "'");
    }
    if (result_record.workflow_canonical_name.empty() || result_record.session_id.empty() ||
        result_record.input_fixture.empty() ||
        result_record.durable_store_import_provider_sdk_mock_adapter_contract_identity.empty() ||
        result_record.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        result_record.normalized_message.empty()) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter execution result identity and message "
                              "fields must not be empty");
    }
    validate_failure(result_record.failure_attribution,
                     diagnostics,
                     "provider SDK mock adapter execution result");
    validate_no_real_provider_access(result_record.opens_network_connection,
                                     result_record.reads_secret_material,
                                     result_record.materializes_credential_material,
                                     result_record.invokes_real_provider_sdk,
                                     diagnostics,
                                     "provider SDK mock adapter execution result");
    if (result_record.result_status == ProviderSdkMockAdapterStatus::Ready) {
        if (result_record.source_contract_status != ProviderSdkMockAdapterStatus::Ready ||
            result_record.operation_kind != ProviderSdkMockAdapterOperationKind::RunMockAdapter) {
            emit_validation_error(diagnostics,
                                  "provider SDK mock adapter execution result ready status "
                                  "requires ready contract and mock run");
        }
    } else if (result_record.operation_kind ==
                   ProviderSdkMockAdapterOperationKind::RunMockAdapter ||
               !result_record.failure_attribution.has_value()) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter execution result blocked status requires "
                              "noop operation and failure_attribution");
    }
    return result;
}

ProviderSdkMockAdapterReadinessValidationResult
validate_provider_sdk_mock_adapter_readiness(const ProviderSdkMockAdapterReadiness &readiness) {
    ProviderSdkMockAdapterReadinessValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (readiness.format_version != kProviderSdkMockAdapterReadinessFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter readiness format_version must be '" +
                                  std::string(kProviderSdkMockAdapterReadinessFormatVersion) + "'");
    }
    if (readiness
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider SDK mock adapter readiness source format_version must be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (readiness.workflow_canonical_name.empty() || readiness.session_id.empty() ||
        readiness.input_fixture.empty() ||
        readiness.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        readiness.normalization_summary.empty() || readiness.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "provider SDK mock adapter readiness identity and summary fields must not be empty");
    }
    validate_failure(
        readiness.failure_attribution, diagnostics, "provider SDK mock adapter readiness");
    if (readiness.next_action == ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity &&
        (readiness.result_status != ProviderSdkMockAdapterStatus::Ready ||
         readiness.normalized_result != ProviderSdkMockAdapterNormalizedResultKind::Accepted ||
         readiness.failure_attribution.has_value())) {
        emit_validation_error(diagnostics,
                              "provider SDK mock adapter readiness real-adapter next action "
                              "requires accepted mock result and no failure");
    }
    return result;
}

ProviderSdkMockAdapterContractResult
build_provider_sdk_mock_adapter_contract(const ProviderSdkPayloadAuditSummary &audit) {
    ProviderSdkMockAdapterContractResult result;
    result.diagnostics.append(validate_provider_sdk_payload_audit_summary(audit).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = audit.next_action == ProviderSdkPayloadNextActionKind::ReadyForMockAdapter;
    const auto status =
        ready ? ProviderSdkMockAdapterStatus::Ready : ProviderSdkMockAdapterStatus::Blocked;
    ProviderSdkMockAdapterContract contract;
    contract.workflow_canonical_name = audit.workflow_canonical_name;
    contract.session_id = audit.session_id;
    contract.run_id = audit.run_id;
    contract.input_fixture = audit.input_fixture;
    contract.durable_store_import_provider_sdk_payload_plan_identity =
        audit.durable_store_import_provider_sdk_payload_plan_identity;
    contract.source_payload_next_action = audit.next_action;
    contract.durable_store_import_provider_sdk_mock_adapter_contract_identity =
        contract_identity(audit, status);
    contract.contract_status = status;
    contract.provider_request_payload_schema_ref = audit.provider_request_payload_schema_ref;
    contract.payload_digest = audit.payload_digest;
    contract.fake_provider_only = audit.fake_provider_only;
    if (ready) {
        contract.operation_kind = ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract;
    } else {
        contract.operation_kind = ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady;
        contract.failure_attribution = payload_not_ready_failure(audit);
    }
    result.diagnostics.append(validate_provider_sdk_mock_adapter_contract(contract).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.contract = std::move(contract);
    return result;
}

ProviderSdkMockAdapterExecutionResultResult
run_provider_sdk_mock_adapter(const ProviderSdkMockAdapterContract &contract) {
    ProviderSdkMockAdapterExecutionResultResult result;
    result.diagnostics.append(validate_provider_sdk_mock_adapter_contract(contract).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderSdkMockAdapterExecutionResult record;
    record.workflow_canonical_name = contract.workflow_canonical_name;
    record.session_id = contract.session_id;
    record.run_id = contract.run_id;
    record.input_fixture = contract.input_fixture;
    record.durable_store_import_provider_sdk_mock_adapter_contract_identity =
        contract.durable_store_import_provider_sdk_mock_adapter_contract_identity;
    record.source_contract_status = contract.contract_status;
    record.scenario_kind = contract.scenario_kind;
    if (contract.contract_status != ProviderSdkMockAdapterStatus::Ready) {
        record.operation_kind = ProviderSdkMockAdapterOperationKind::NoopContractNotReady;
        record.result_status = ProviderSdkMockAdapterStatus::Blocked;
        record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::Blocked;
        record.normalized_message = "mock adapter contract was blocked";
        record.failure_attribution = contract_not_ready_failure(contract);
    } else {
        record.operation_kind = ProviderSdkMockAdapterOperationKind::RunMockAdapter;
        record.result_status = ProviderSdkMockAdapterStatus::Ready;
        switch (contract.scenario_kind) {
        case ProviderSdkMockAdapterScenarioKind::Success:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::Accepted;
            record.provider_status_code = 200;
            record.normalized_message = "mock provider accepted fake payload";
            break;
        case ProviderSdkMockAdapterScenarioKind::Failure:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure;
            record.provider_status_code = 500;
            record.normalized_message = "mock provider returned failure";
            record.failure_attribution = ProviderSdkMockAdapterFailureAttribution{
                .kind = ProviderSdkMockAdapterFailureKind::ProviderFailure,
                .message = "mock provider returned failure",
            };
            break;
        case ProviderSdkMockAdapterScenarioKind::Timeout:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::Timeout;
            record.provider_status_code = 504;
            record.normalized_message = "mock provider timed out";
            record.failure_attribution = ProviderSdkMockAdapterFailureAttribution{
                .kind = ProviderSdkMockAdapterFailureKind::Timeout,
                .message = "mock provider timed out",
            };
            break;
        case ProviderSdkMockAdapterScenarioKind::Throttle:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::Throttled;
            record.provider_status_code = 429;
            record.normalized_message = "mock provider throttled request";
            record.failure_attribution = ProviderSdkMockAdapterFailureAttribution{
                .kind = ProviderSdkMockAdapterFailureKind::Throttle,
                .message = "mock provider throttled request",
            };
            break;
        case ProviderSdkMockAdapterScenarioKind::Conflict:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::Conflict;
            record.provider_status_code = 409;
            record.normalized_message = "mock provider returned conflict";
            record.failure_attribution = ProviderSdkMockAdapterFailureAttribution{
                .kind = ProviderSdkMockAdapterFailureKind::Conflict,
                .message = "mock provider returned conflict",
            };
            break;
        case ProviderSdkMockAdapterScenarioKind::SchemaMismatch:
            record.normalized_result = ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch;
            record.provider_status_code = 422;
            record.normalized_message = "mock provider reported schema mismatch";
            record.failure_attribution = ProviderSdkMockAdapterFailureAttribution{
                .kind = ProviderSdkMockAdapterFailureKind::SchemaMismatch,
                .message = "mock provider reported schema mismatch",
            };
            break;
        }
    }
    record.durable_store_import_provider_sdk_mock_adapter_result_identity =
        result_identity(contract);
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.result = std::move(record);
    return result;
}

ProviderSdkMockAdapterReadinessResult build_provider_sdk_mock_adapter_readiness(
    const ProviderSdkMockAdapterExecutionResult &result_record) {
    ProviderSdkMockAdapterReadinessResult result;
    result.diagnostics.append(
        validate_provider_sdk_mock_adapter_execution_result(result_record).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderSdkMockAdapterReadiness readiness;
    readiness.workflow_canonical_name = result_record.workflow_canonical_name;
    readiness.session_id = result_record.session_id;
    readiness.run_id = result_record.run_id;
    readiness.input_fixture = result_record.input_fixture;
    readiness.durable_store_import_provider_sdk_mock_adapter_result_identity =
        result_record.durable_store_import_provider_sdk_mock_adapter_result_identity;
    readiness.result_status = result_record.result_status;
    readiness.normalized_result = result_record.normalized_result;
    readiness.normalization_summary =
        "mock adapter normalized provider result: " + result_record.normalized_message;
    readiness.next_action = next_action_for_result(result_record);
    readiness.next_step_recommendation =
        readiness.next_action == ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity
            ? "mock adapter accepted fake payload and can anchor real adapter parity tests"
            : "review mock adapter normalized failure before real adapter work";
    readiness.failure_attribution = result_record.failure_attribution;
    result.diagnostics.append(validate_provider_sdk_mock_adapter_readiness(readiness).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.readiness = std::move(readiness);
    return result;
}

} // namespace ahfl::durable_store_import
