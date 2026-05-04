#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_SDK_ADAPTER";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string adapter_status_slug(ProviderSdkAdapterStatus status) {
    switch (status) {
    case ProviderSdkAdapterStatus::Ready:
        return "ready";
    case ProviderSdkAdapterStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string request_plan_identity(const ProviderLocalHostExecutionReceipt &receipt,
                                                ProviderSdkAdapterStatus status) {
    return "durable-store-import-provider-sdk-adapter-request::" + receipt.session_id +
           "::" + adapter_status_slug(status);
}

[[nodiscard]] std::string
response_placeholder_artifact_identity(const ProviderSdkAdapterRequestPlan &plan,
                                       ProviderSdkAdapterStatus status) {
    return "durable-store-import-provider-sdk-adapter-response-placeholder::" + plan.session_id +
           "::" + adapter_status_slug(status);
}

[[nodiscard]] std::optional<std::string>
provider_sdk_adapter_request_identity(const ProviderLocalHostExecutionReceipt &receipt) {
    if (!receipt.provider_local_host_execution_receipt_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-adapter-request::" +
           *receipt.provider_local_host_execution_receipt_identity;
}

[[nodiscard]] std::optional<std::string>
provider_sdk_adapter_response_placeholder_identity(const std::optional<std::string> &request_id) {
    if (!request_id.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-adapter-response-placeholder::" + *request_id;
}

[[nodiscard]] ProviderSdkAdapterFailureAttribution
local_host_execution_not_ready_failure(const ProviderLocalHostExecutionReceipt &receipt) {
    std::string message =
        "provider SDK adapter request cannot proceed because local host execution receipt is not "
        "ready";
    if (receipt.failure_attribution.has_value()) {
        message = receipt.failure_attribution->message;
    }

    return ProviderSdkAdapterFailureAttribution{
        .kind = ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady,
        .message = std::move(message),
    };
}

[[nodiscard]] ProviderSdkAdapterFailureAttribution
sdk_adapter_request_not_ready_failure(const ProviderSdkAdapterRequestPlan &plan) {
    std::string message =
        "provider SDK adapter response placeholder cannot proceed because SDK adapter request is "
        "not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderSdkAdapterFailureAttribution{
        .kind = ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady,
        .message = std::move(message),
    };
}

void validate_failure_attribution(
    const std::optional<ProviderSdkAdapterFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_side_effects(bool materializes_sdk_request_payload,
                              bool invokes_provider_sdk,
                              bool opens_network_connection,
                              bool reads_secret_material,
                              bool reads_host_environment,
                              bool writes_host_filesystem,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (materializes_sdk_request_payload) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_secret_material) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (reads_host_environment) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

void validate_forbidden_material(const ProviderSdkAdapterRequestPlan &plan,
                                 DiagnosticBag &diagnostics) {
    if (plan.provider_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter request plan cannot "
                              "contain provider_endpoint_uri");
    }
    if (plan.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter request plan cannot "
                              "contain object_path");
    }
    if (plan.database_table.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter request plan cannot "
                              "contain database_table");
    }
    if (plan.sdk_request_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter request plan cannot "
                              "contain sdk_request_payload");
    }
    if (plan.sdk_response_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter request plan cannot "
                              "contain sdk_response_payload");
    }
}

[[nodiscard]] ProviderSdkAdapterReadinessNextActionKind
next_action_for_placeholder(const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    if (placeholder.response_status == ProviderSdkAdapterStatus::Ready) {
        return ProviderSdkAdapterReadinessNextActionKind::ReadyForProviderSdkAdapterInterface;
    }

    if (placeholder.failure_attribution.has_value()) {
        switch (placeholder.failure_attribution->kind) {
        case ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady:
        case ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady:
            return ProviderSdkAdapterReadinessNextActionKind::WaitForLocalHostExecutionReceipt;
        }
    }

    return ProviderSdkAdapterReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string
boundary_summary(const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    if (placeholder.response_status == ProviderSdkAdapterStatus::Ready) {
        return "provider SDK adapter response placeholder was planned without materializing SDK "
               "payload, invoking provider SDK, opening network, reading secret material, reading "
               "host environment, or writing host filesystem";
    }

    return "provider SDK adapter response placeholder was not planned because SDK adapter request "
           "is not ready";
}

[[nodiscard]] std::string
next_step_summary(const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    switch (next_action_for_placeholder(placeholder)) {
    case ProviderSdkAdapterReadinessNextActionKind::ReadyForProviderSdkAdapterInterface:
        return "ready for future provider SDK adapter interface; no SDK payload, SDK call, "
               "network, secret, host env, or filesystem write was used";
    case ProviderSdkAdapterReadinessNextActionKind::WaitForLocalHostExecutionReceipt:
        return "wait for ready local host execution receipt and SDK adapter request before "
               "adapter interface implementation";
    case ProviderSdkAdapterReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider SDK adapter can proceed";
    }

    return "manual review required before provider SDK adapter can proceed";
}

} // namespace

ProviderSdkAdapterRequestPlanValidationResult
validate_provider_sdk_adapter_request_plan(const ProviderSdkAdapterRequestPlan &plan) {
    ProviderSdkAdapterRequestPlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderSdkAdapterRequestPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan format_version must be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan "
            "source_durable_store_import_provider_local_host_execution_receipt_format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        plan.durable_store_import_provider_host_execution_identity.empty() ||
        plan.durable_store_import_provider_local_host_execution_receipt_identity.empty() ||
        plan.durable_store_import_provider_sdk_adapter_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan identity fields must not be "
            "empty");
    }
    validate_failure_attribution(plan.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK adapter request plan");
    validate_no_side_effects(plan.materializes_sdk_request_payload,
                             plan.invokes_provider_sdk,
                             plan.opens_network_connection,
                             plan.reads_secret_material,
                             plan.reads_host_environment,
                             plan.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider SDK adapter request plan");
    validate_forbidden_material(plan, diagnostics);

    if (plan.request_status == ProviderSdkAdapterStatus::Ready) {
        if (plan.source_local_host_execution_status !=
                ProviderLocalHostExecutionStatus::SimulatedReady ||
            !plan.source_provider_local_host_execution_receipt_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status requires "
                "simulated-ready local host execution receipt");
        }
        if (plan.operation_kind != ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest ||
            !plan.provider_sdk_adapter_request_identity.has_value() ||
            !plan.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status requires SDK "
                "adapter request and response placeholder identities");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan blocked status cannot "
                "plan SDK adapter request");
        }
        if (plan.provider_sdk_adapter_request_identity.has_value() ||
            plan.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan blocked status cannot "
                "contain SDK adapter identities");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan blocked status requires "
                "failure_attribution");
        }
    }

    return result;
}

ProviderSdkAdapterResponsePlaceholderValidationResult
validate_provider_sdk_adapter_response_placeholder(
    const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    ProviderSdkAdapterResponsePlaceholderValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (placeholder.format_version != kProviderSdkAdapterResponsePlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder format_version must "
            "be '" +
                std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion) + "'");
    }
    if (placeholder.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder "
            "source_durable_store_import_provider_sdk_adapter_request_plan_format_version must "
            "be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (placeholder
            .source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder "
            "source_durable_store_import_provider_local_host_execution_receipt_format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (placeholder.workflow_canonical_name.empty() || placeholder.session_id.empty() ||
        placeholder.input_fixture.empty() ||
        placeholder.durable_store_import_provider_sdk_adapter_request_identity.empty() ||
        placeholder.durable_store_import_provider_local_host_execution_receipt_identity.empty() ||
        placeholder.durable_store_import_provider_sdk_adapter_response_placeholder_identity
            .empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder identity fields must "
            "not be empty");
    }
    validate_failure_attribution(placeholder.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK adapter response placeholder");
    validate_no_side_effects(placeholder.materializes_sdk_request_payload,
                             placeholder.invokes_provider_sdk,
                             placeholder.opens_network_connection,
                             placeholder.reads_secret_material,
                             placeholder.reads_host_environment,
                             placeholder.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider SDK adapter response placeholder");

    if (placeholder.response_status == ProviderSdkAdapterStatus::Ready) {
        if (placeholder.source_request_status != ProviderSdkAdapterStatus::Ready ||
            !placeholder.source_provider_sdk_adapter_request_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "requires ready SDK adapter request");
        }
        if (placeholder.operation_kind !=
                ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder ||
            !placeholder.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "requires response placeholder identity");
        }
        if (placeholder.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "cannot contain failure_attribution");
        }
    } else {
        if (placeholder.operation_kind ==
            ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder blocked status "
                "cannot plan response placeholder");
        }
        if (placeholder.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder blocked status "
                "cannot contain response placeholder identity");
        }
        if (!placeholder.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder blocked status "
                "requires failure_attribution");
        }
    }

    return result;
}

ProviderSdkAdapterReadinessReviewValidationResult
validate_provider_sdk_adapter_readiness_review(const ProviderSdkAdapterReadinessReview &review) {
    ProviderSdkAdapterReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderSdkAdapterReadinessReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review format_version must be '" +
                std::string(kProviderSdkAdapterReadinessReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version !=
        kProviderSdkAdapterResponsePlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review "
            "source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version "
            "must be '" +
                std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review "
            "source_durable_store_import_provider_sdk_adapter_request_plan_format_version must "
            "be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_sdk_adapter_request_identity.empty() ||
        review.durable_store_import_provider_sdk_adapter_response_placeholder_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review identity fields must not "
            "be empty");
    }
    if (review.sdk_adapter_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review summaries must not be "
            "empty");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK adapter readiness review");
    validate_no_side_effects(review.materializes_sdk_request_payload,
                             review.invokes_provider_sdk,
                             review.opens_network_connection,
                             review.reads_secret_material,
                             review.reads_host_environment,
                             review.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider SDK adapter readiness review");

    if (review.response_status == ProviderSdkAdapterStatus::Ready) {
        if (review.operation_kind !=
                ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder ||
            !review.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review ready status requires "
                "response placeholder identity");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (review.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review blocked status "
                "cannot contain response placeholder identity");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review blocked status "
                "requires failure_attribution");
        }
    }

    return result;
}

ProviderSdkAdapterRequestPlanResult
build_provider_sdk_adapter_request_plan(const ProviderLocalHostExecutionReceipt &receipt) {
    ProviderSdkAdapterRequestPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_local_host_execution_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto can_plan =
        receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady &&
        receipt.provider_local_host_execution_receipt_identity.has_value();
    const auto status =
        can_plan ? ProviderSdkAdapterStatus::Ready : ProviderSdkAdapterStatus::Blocked;
    const auto request_id =
        can_plan ? provider_sdk_adapter_request_identity(receipt) : std::nullopt;
    const auto response_placeholder_id =
        can_plan ? provider_sdk_adapter_response_placeholder_identity(request_id) : std::nullopt;

    ProviderSdkAdapterRequestPlan plan{
        .format_version = std::string(kProviderSdkAdapterRequestPlanFormatVersion),
        .source_durable_store_import_provider_local_host_execution_receipt_format_version =
            receipt.format_version,
        .source_durable_store_import_provider_host_execution_plan_format_version =
            receipt.source_durable_store_import_provider_host_execution_plan_format_version,
        .workflow_canonical_name = receipt.workflow_canonical_name,
        .session_id = receipt.session_id,
        .run_id = receipt.run_id,
        .input_fixture = receipt.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            receipt.durable_store_import_provider_sdk_request_envelope_identity,
        .durable_store_import_provider_host_execution_identity =
            receipt.durable_store_import_provider_host_execution_identity,
        .durable_store_import_provider_local_host_execution_receipt_identity =
            receipt.durable_store_import_provider_local_host_execution_receipt_identity,
        .source_local_host_execution_status = receipt.execution_status,
        .source_provider_local_host_execution_receipt_identity =
            receipt.provider_local_host_execution_receipt_identity,
        .durable_store_import_provider_sdk_adapter_request_identity =
            request_plan_identity(receipt, status),
        .operation_kind = can_plan
                              ? ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest
                              : ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady,
        .request_status = status,
        .provider_sdk_adapter_request_identity = request_id,
        .provider_sdk_adapter_response_placeholder_identity = response_placeholder_id,
        .materializes_sdk_request_payload = false,
        .invokes_provider_sdk = false,
        .opens_network_connection = false,
        .reads_secret_material = false,
        .reads_host_environment = false,
        .writes_host_filesystem = false,
        .provider_endpoint_uri = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_request_payload = std::nullopt,
        .sdk_response_payload = std::nullopt,
        .failure_attribution = can_plan ? std::nullopt
                                        : std::optional<ProviderSdkAdapterFailureAttribution>(
                                              local_host_execution_not_ready_failure(receipt)),
    };

    result.diagnostics.append(validate_provider_sdk_adapter_request_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(plan);
    return result;
}

ProviderSdkAdapterResponsePlaceholderResult
build_provider_sdk_adapter_response_placeholder(const ProviderSdkAdapterRequestPlan &plan) {
    ProviderSdkAdapterResponsePlaceholderResult result{
        .placeholder = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_sdk_adapter_request_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto can_plan = plan.request_status == ProviderSdkAdapterStatus::Ready &&
                          plan.provider_sdk_adapter_request_identity.has_value() &&
                          plan.provider_sdk_adapter_response_placeholder_identity.has_value();
    const auto status =
        can_plan ? ProviderSdkAdapterStatus::Ready : ProviderSdkAdapterStatus::Blocked;

    ProviderSdkAdapterResponsePlaceholder placeholder{
        .format_version = std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion),
        .source_durable_store_import_provider_sdk_adapter_request_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_local_host_execution_receipt_format_version =
            plan.source_durable_store_import_provider_local_host_execution_receipt_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_sdk_adapter_request_identity =
            plan.durable_store_import_provider_sdk_adapter_request_identity,
        .durable_store_import_provider_local_host_execution_receipt_identity =
            plan.durable_store_import_provider_local_host_execution_receipt_identity,
        .source_request_status = plan.request_status,
        .source_provider_sdk_adapter_request_identity = plan.provider_sdk_adapter_request_identity,
        .durable_store_import_provider_sdk_adapter_response_placeholder_identity =
            response_placeholder_artifact_identity(plan, status),
        .operation_kind =
            can_plan ? ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder
                     : ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady,
        .response_status = status,
        .provider_sdk_adapter_response_placeholder_identity =
            can_plan ? plan.provider_sdk_adapter_response_placeholder_identity : std::nullopt,
        .materializes_sdk_request_payload = false,
        .invokes_provider_sdk = false,
        .opens_network_connection = false,
        .reads_secret_material = false,
        .reads_host_environment = false,
        .writes_host_filesystem = false,
        .failure_attribution = can_plan ? std::nullopt
                                        : std::optional<ProviderSdkAdapterFailureAttribution>(
                                              sdk_adapter_request_not_ready_failure(plan)),
    };

    result.diagnostics.append(
        validate_provider_sdk_adapter_response_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.placeholder = std::move(placeholder);
    return result;
}

ProviderSdkAdapterReadinessReviewResult build_provider_sdk_adapter_readiness_review(
    const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    ProviderSdkAdapterReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(
        validate_provider_sdk_adapter_response_placeholder(placeholder).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderSdkAdapterReadinessReview review{
        .format_version = std::string(kProviderSdkAdapterReadinessReviewFormatVersion),
        .source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version =
            placeholder.format_version,
        .source_durable_store_import_provider_sdk_adapter_request_plan_format_version =
            placeholder
                .source_durable_store_import_provider_sdk_adapter_request_plan_format_version,
        .workflow_canonical_name = placeholder.workflow_canonical_name,
        .session_id = placeholder.session_id,
        .run_id = placeholder.run_id,
        .input_fixture = placeholder.input_fixture,
        .durable_store_import_provider_sdk_adapter_request_identity =
            placeholder.durable_store_import_provider_sdk_adapter_request_identity,
        .durable_store_import_provider_sdk_adapter_response_placeholder_identity =
            placeholder.durable_store_import_provider_sdk_adapter_response_placeholder_identity,
        .response_status = placeholder.response_status,
        .operation_kind = placeholder.operation_kind,
        .provider_sdk_adapter_response_placeholder_identity =
            placeholder.provider_sdk_adapter_response_placeholder_identity,
        .materializes_sdk_request_payload = placeholder.materializes_sdk_request_payload,
        .invokes_provider_sdk = placeholder.invokes_provider_sdk,
        .opens_network_connection = placeholder.opens_network_connection,
        .reads_secret_material = placeholder.reads_secret_material,
        .reads_host_environment = placeholder.reads_host_environment,
        .writes_host_filesystem = placeholder.writes_host_filesystem,
        .failure_attribution = placeholder.failure_attribution,
        .sdk_adapter_boundary_summary = boundary_summary(placeholder),
        .next_step_recommendation = next_step_summary(placeholder),
        .next_action = next_action_for_placeholder(placeholder),
    };

    result.diagnostics.append(validate_provider_sdk_adapter_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
