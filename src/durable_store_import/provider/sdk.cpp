// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_sdk_adapter.cpp ----

#include "durable_store_import/provider/sdk/adapter.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_sdk_adapter_detail_kValidationDiagnosticCode{"DSI_PROVIDER_SDK_ADAPTER"};

void provider_sdk_adapter_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                       std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_sdk_adapter_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_sdk_adapter_detail_adapter_status_slug(ProviderSdkAdapterStatus status) {
    switch (status) {
    case ProviderSdkAdapterStatus::Ready:
        return "ready";
    case ProviderSdkAdapterStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
provider_sdk_adapter_detail_request_plan_identity(const ProviderLocalHostExecutionReceipt &receipt,
                                                  ProviderSdkAdapterStatus status) {
    return "durable-store-import-provider-sdk-adapter-request::" + receipt.session_id +
           "::" + provider_sdk_adapter_detail_adapter_status_slug(status);
}

[[nodiscard]] std::string provider_sdk_adapter_detail_response_placeholder_artifact_identity(
    const ProviderSdkAdapterRequestPlan &plan, ProviderSdkAdapterStatus status) {
    return "durable-store-import-provider-sdk-adapter-response-placeholder::" + plan.session_id +
           "::" + provider_sdk_adapter_detail_adapter_status_slug(status);
}

[[nodiscard]] std::optional<std::string>
provider_sdk_adapter_detail_provider_sdk_adapter_request_identity(
    const ProviderLocalHostExecutionReceipt &receipt) {
    if (!receipt.provider_local_host_execution_receipt_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-adapter-request::" +
           *receipt.provider_local_host_execution_receipt_identity;
}

[[nodiscard]] std::optional<std::string>
provider_sdk_adapter_detail_provider_sdk_adapter_response_placeholder_identity(
    const std::optional<std::string> &request_id) {
    if (!request_id.has_value()) {
        return std::nullopt;
    }

    return "provider-sdk-adapter-response-placeholder::" + *request_id;
}

[[nodiscard]] ProviderSdkAdapterFailureAttribution
provider_sdk_adapter_detail_local_host_execution_not_ready_failure(
    const ProviderLocalHostExecutionReceipt &receipt) {
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
provider_sdk_adapter_detail_sdk_adapter_request_not_ready_failure(
    const ProviderSdkAdapterRequestPlan &plan) {
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

void provider_sdk_adapter_detail_validate_failure_attribution(
    const std::optional<ProviderSdkAdapterFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_sdk_adapter_detail_validate_no_side_effects(bool materializes_sdk_request_payload,
                                                          bool invokes_provider_sdk,
                                                          bool opens_network_connection,
                                                          bool reads_secret_material,
                                                          bool reads_host_environment,
                                                          bool writes_host_filesystem,
                                                          DiagnosticBag &diagnostics,
                                                          std::string_view owner) {
    if (materializes_sdk_request_payload) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (opens_network_connection) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_secret_material) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (reads_host_environment) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

void provider_sdk_adapter_detail_validate_forbidden_material(
    const ProviderSdkAdapterRequestPlan &plan, DiagnosticBag &diagnostics) {
    if (plan.provider_endpoint_uri.has_value()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan cannot "
            "contain provider_endpoint_uri");
    }
    if (plan.object_path.has_value()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan cannot "
            "contain object_path");
    }
    if (plan.database_table.has_value()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan cannot "
            "contain database_table");
    }
    if (plan.sdk_request_payload.has_value()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan cannot "
            "contain sdk_request_payload");
    }
    if (plan.sdk_response_payload.has_value()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan cannot "
            "contain sdk_response_payload");
    }
}

[[nodiscard]] ProviderSdkAdapterReadinessNextActionKind
provider_sdk_adapter_detail_next_action_for_placeholder(
    const ProviderSdkAdapterResponsePlaceholder &placeholder) {
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

[[nodiscard]] std::string provider_sdk_adapter_detail_boundary_summary(
    const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    if (placeholder.response_status == ProviderSdkAdapterStatus::Ready) {
        return "provider SDK adapter response placeholder was planned without materializing SDK "
               "payload, invoking provider SDK, opening network, reading secret material, reading "
               "host environment, or writing host filesystem";
    }

    return "provider SDK adapter response placeholder was not planned because SDK adapter request "
           "is not ready";
}

[[nodiscard]] std::string provider_sdk_adapter_detail_next_step_summary(
    const ProviderSdkAdapterResponsePlaceholder &placeholder) {
    switch (provider_sdk_adapter_detail_next_action_for_placeholder(placeholder)) {
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan format_version must be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan "
            "source_durable_store_import_provider_local_host_execution_receipt_format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter request plan identity fields must not be "
            "empty");
    }
    provider_sdk_adapter_detail_validate_failure_attribution(
        plan.failure_attribution,
        diagnostics,
        "durable store import provider SDK adapter request plan");
    provider_sdk_adapter_detail_validate_no_side_effects(
        plan.materializes_sdk_request_payload,
        plan.invokes_provider_sdk,
        plan.opens_network_connection,
        plan.reads_secret_material,
        plan.reads_host_environment,
        plan.writes_host_filesystem,
        diagnostics,
        "durable store import provider SDK adapter request plan");
    provider_sdk_adapter_detail_validate_forbidden_material(plan, diagnostics);

    if (plan.request_status == ProviderSdkAdapterStatus::Ready) {
        if (plan.source_local_host_execution_status !=
                ProviderLocalHostExecutionStatus::SimulatedReady ||
            !plan.source_provider_local_host_execution_receipt_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status requires "
                "simulated-ready local host execution receipt");
        }
        if (plan.operation_kind != ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest ||
            !plan.provider_sdk_adapter_request_identity.has_value() ||
            !plan.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status requires SDK "
                "adapter request and response placeholder identities");
        }
        if (plan.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan blocked status cannot "
                "plan SDK adapter request");
        }
        if (plan.provider_sdk_adapter_request_identity.has_value() ||
            plan.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter request plan blocked status cannot "
                "contain SDK adapter identities");
        }
        if (!plan.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder format_version must "
            "be '" +
                std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion) + "'");
    }
    if (placeholder.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder "
            "source_durable_store_import_provider_sdk_adapter_request_plan_format_version must "
            "be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (placeholder
            .source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter response placeholder identity fields must "
            "not be empty");
    }
    provider_sdk_adapter_detail_validate_failure_attribution(
        placeholder.failure_attribution,
        diagnostics,
        "durable store import provider SDK adapter response placeholder");
    provider_sdk_adapter_detail_validate_no_side_effects(
        placeholder.materializes_sdk_request_payload,
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
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "requires ready SDK adapter request");
        }
        if (placeholder.operation_kind !=
                ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder ||
            !placeholder.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "requires response placeholder identity");
        }
        if (placeholder.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder ready status "
                "cannot contain failure_attribution");
        }
    } else {
        if (placeholder.operation_kind ==
            ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterResponsePlaceholder) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder blocked status "
                "cannot plan response placeholder");
        }
        if (placeholder.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter response placeholder blocked status "
                "cannot contain response placeholder identity");
        }
        if (!placeholder.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review format_version must be '" +
                std::string(kProviderSdkAdapterReadinessReviewFormatVersion) + "'");
    }
    if (review
            .source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version !=
        kProviderSdkAdapterResponsePlaceholderFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review "
            "source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version "
            "must be '" +
                std::string(kProviderSdkAdapterResponsePlaceholderFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        provider_sdk_adapter_detail_emit_validation_error(
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
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review identity fields must not "
            "be empty");
    }
    if (review.sdk_adapter_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        provider_sdk_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter readiness review summaries must not be "
            "empty");
    }
    provider_sdk_adapter_detail_validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider SDK adapter readiness review");
    provider_sdk_adapter_detail_validate_no_side_effects(
        review.materializes_sdk_request_payload,
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
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review ready status requires "
                "response placeholder identity");
        }
        if (review.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (review.provider_sdk_adapter_response_placeholder_identity.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter readiness review blocked status "
                "cannot contain response placeholder identity");
        }
        if (!review.failure_attribution.has_value()) {
            provider_sdk_adapter_detail_emit_validation_error(
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
        can_plan ? provider_sdk_adapter_detail_provider_sdk_adapter_request_identity(receipt)
                 : std::nullopt;
    const auto response_placeholder_id =
        can_plan ? provider_sdk_adapter_detail_provider_sdk_adapter_response_placeholder_identity(
                       request_id)
                 : std::nullopt;

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
            provider_sdk_adapter_detail_request_plan_identity(receipt, status),
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
        .failure_attribution =
            can_plan
                ? std::nullopt
                : std::optional<ProviderSdkAdapterFailureAttribution>(
                      provider_sdk_adapter_detail_local_host_execution_not_ready_failure(receipt)),
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
            provider_sdk_adapter_detail_response_placeholder_artifact_identity(plan, status),
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
        .failure_attribution =
            can_plan ? std::nullopt
                     : std::optional<ProviderSdkAdapterFailureAttribution>(
                           provider_sdk_adapter_detail_sdk_adapter_request_not_ready_failure(plan)),
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
        .sdk_adapter_boundary_summary = provider_sdk_adapter_detail_boundary_summary(placeholder),
        .next_step_recommendation = provider_sdk_adapter_detail_next_step_summary(placeholder),
        .next_action = provider_sdk_adapter_detail_next_action_for_placeholder(placeholder),
    };

    result.diagnostics.append(validate_provider_sdk_adapter_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_sdk_interface.cpp ----

#include "durable_store_import/provider/sdk/interface.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_sdk_interface_detail_kValidationDiagnosticCode{"DSI_PROVIDER_SDK_ADAPTER_INTERFACE"};

void provider_sdk_interface_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                         std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_sdk_interface_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_sdk_interface_detail_interface_status_slug(ProviderSdkAdapterInterfaceStatus status) {
    switch (status) {
    case ProviderSdkAdapterInterfaceStatus::Ready:
        return "ready";
    case ProviderSdkAdapterInterfaceStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
provider_sdk_interface_detail_interface_plan_identity(const ProviderSdkAdapterRequestPlan &plan,
                                                      ProviderSdkAdapterInterfaceStatus status) {
    return "durable-store-import-provider-sdk-adapter-interface::" + plan.session_id +
           "::" + provider_sdk_interface_detail_interface_status_slug(status);
}

[[nodiscard]] std::string provider_sdk_interface_detail_provider_registry_identity(
    const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-registry::" + plan.session_id + "::local-placeholder";
}

[[nodiscard]] std::string provider_sdk_interface_detail_adapter_descriptor_identity(
    const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-descriptor::" + plan.session_id +
           "::local-placeholder-durable-store";
}

[[nodiscard]] std::string provider_sdk_interface_detail_capability_descriptor_identity(
    const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-capability::" + plan.session_id + "::durable-store-import-write";
}

[[nodiscard]] ProviderSdkAdapterInterfaceFailureAttribution
provider_sdk_interface_detail_request_not_ready_failure(const ProviderSdkAdapterRequestPlan &plan) {
    std::string message =
        "provider SDK adapter interface cannot proceed because SDK adapter request is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderSdkAdapterInterfaceFailureAttribution{
        .kind = ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady,
        .message = std::move(message),
    };
}

void provider_sdk_interface_detail_validate_failure_attribution(
    const std::optional<ProviderSdkAdapterInterfaceFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_sdk_interface_detail_validate_no_side_effects(bool materializes_sdk_request_payload,
                                                            bool invokes_provider_sdk,
                                                            bool opens_network_connection,
                                                            bool reads_secret_material,
                                                            bool reads_host_environment,
                                                            bool writes_host_filesystem,
                                                            DiagnosticBag &diagnostics,
                                                            std::string_view owner) {
    if (materializes_sdk_request_payload) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (opens_network_connection) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (reads_secret_material) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read secret material");
    }
    if (reads_host_environment) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (writes_host_filesystem) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

void provider_sdk_interface_detail_validate_forbidden_material(
    const ProviderSdkAdapterInterfacePlan &plan, DiagnosticBag &diagnostics) {
    if (plan.provider_endpoint_uri.has_value()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan cannot "
            "contain provider_endpoint_uri");
    }
    if (plan.credential_reference.has_value()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan cannot "
            "contain credential_reference");
    }
    if (plan.sdk_request_payload.has_value()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan cannot "
            "contain sdk_request_payload");
    }
    if (plan.sdk_response_payload.has_value()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan cannot "
            "contain sdk_response_payload");
    }
}

[[nodiscard]] ProviderSdkAdapterInterfaceReviewNextActionKind
provider_sdk_interface_detail_next_action_for_plan(const ProviderSdkAdapterInterfacePlan &plan) {
    if (plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        return ProviderSdkAdapterInterfaceReviewNextActionKind::ReadyForMockAdapterImplementation;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady:
            return ProviderSdkAdapterInterfaceReviewNextActionKind::WaitForSdkAdapterRequest;
        case ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
            return ProviderSdkAdapterInterfaceReviewNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderSdkAdapterInterfaceReviewNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string provider_sdk_interface_detail_interface_boundary_summary(
    const ProviderSdkAdapterInterfacePlan &plan) {
    if (plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        return "provider SDK adapter interface descriptor is ready with local placeholder "
               "registry, durable-store-import capability descriptor, and normalized error "
               "taxonomy; no SDK payload, SDK call, network, secret, host environment, or "
               "filesystem write was used";
    }

    return "provider SDK adapter interface descriptor is blocked because SDK adapter request is "
           "not ready";
}

[[nodiscard]] std::string
provider_sdk_interface_detail_next_step_summary(const ProviderSdkAdapterInterfacePlan &plan) {
    switch (provider_sdk_interface_detail_next_action_for_plan(plan)) {
    case ProviderSdkAdapterInterfaceReviewNextActionKind::ReadyForMockAdapterImplementation:
        return "ready for future mock provider SDK adapter implementation";
    case ProviderSdkAdapterInterfaceReviewNextActionKind::WaitForSdkAdapterRequest:
        return "wait for ready provider SDK adapter request before interface implementation";
    case ProviderSdkAdapterInterfaceReviewNextActionKind::ManualReviewRequired:
        return "manual review required before provider SDK adapter interface can proceed";
    }

    return "manual review required before provider SDK adapter interface can proceed";
}

} // namespace

ProviderSdkAdapterInterfacePlanValidationResult
validate_provider_sdk_adapter_interface_plan(const ProviderSdkAdapterInterfacePlan &plan) {
    ProviderSdkAdapterInterfacePlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderSdkAdapterInterfacePlanFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan format_version must be '" +
                std::string(kProviderSdkAdapterInterfacePlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan "
            "source_durable_store_import_provider_sdk_adapter_request_plan_format_version must "
            "be '" +
                std::string(kProviderSdkAdapterRequestPlanFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_sdk_adapter_request_identity.empty() ||
        plan.durable_store_import_provider_sdk_adapter_interface_identity.empty() ||
        plan.provider_registry_identity.empty() || plan.adapter_descriptor_identity.empty() ||
        plan.provider_key.empty() || plan.adapter_name.empty() || plan.adapter_version.empty() ||
        plan.interface_abi_version.empty() || plan.capability_descriptor_identity.empty() ||
        plan.capability_descriptor.capability_key.empty() ||
        plan.capability_descriptor.input_contract_format_version.empty() ||
        plan.capability_descriptor.output_contract_format_version.empty() ||
        plan.error_taxonomy_version.empty()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan identity and descriptor "
            "fields must not be empty");
    }
    if (plan.interface_abi_version != kProviderSdkAdapterInterfaceAbiVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan interface_abi_version must "
            "be '" +
                std::string(kProviderSdkAdapterInterfaceAbiVersion) + "'");
    }
    if (plan.error_taxonomy_version != kProviderSdkAdapterErrorTaxonomyVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan error_taxonomy_version must "
            "be '" +
                std::string(kProviderSdkAdapterErrorTaxonomyVersion) + "'");
    }
    if (plan.capability_descriptor.input_contract_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan capability input contract "
            "must be provider SDK adapter request plan");
    }
    if (plan.capability_descriptor.output_contract_format_version !=
        kProviderSdkAdapterResponsePlaceholderFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan capability output contract "
            "must be provider SDK adapter response placeholder");
    }
    provider_sdk_interface_detail_validate_failure_attribution(
        plan.failure_attribution,
        diagnostics,
        "durable store import provider SDK adapter interface plan");
    provider_sdk_interface_detail_validate_no_side_effects(
        plan.materializes_sdk_request_payload,
        plan.invokes_provider_sdk,
        plan.opens_network_connection,
        plan.reads_secret_material,
        plan.reads_host_environment,
        plan.writes_host_filesystem,
        diagnostics,
        "durable store import provider SDK adapter interface plan");
    provider_sdk_interface_detail_validate_forbidden_material(plan, diagnostics);

    if (plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        if (plan.source_sdk_adapter_request_status != ProviderSdkAdapterStatus::Ready ||
            !plan.source_provider_sdk_adapter_request_identity.has_value()) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan ready status requires "
                "ready SDK adapter request");
        }
        if (plan.operation_kind !=
                ProviderSdkAdapterInterfaceOperationKind::PlanProviderSdkAdapterInterface ||
            plan.capability_descriptor.support_status !=
                ProviderSdkAdapterCapabilitySupportStatus::Supported ||
            plan.normalized_error_kind != ProviderSdkAdapterNormalizedErrorKind::None ||
            !plan.returns_placeholder_result ||
            !plan.capability_descriptor.supports_placeholder_response) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan ready status requires "
                "supported placeholder capability and no normalized error");
        }
        if (plan.failure_attribution.has_value()) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (plan.operation_kind ==
            ProviderSdkAdapterInterfaceOperationKind::PlanProviderSdkAdapterInterface) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan blocked status cannot "
                "plan provider SDK adapter interface");
        }
        if (!plan.failure_attribution.has_value()) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan blocked status "
                "requires failure_attribution");
        }
    }

    return result;
}

ProviderSdkAdapterInterfaceReviewValidationResult
validate_provider_sdk_adapter_interface_review(const ProviderSdkAdapterInterfaceReview &review) {
    ProviderSdkAdapterInterfaceReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderSdkAdapterInterfaceReviewFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review format_version must be '" +
                std::string(kProviderSdkAdapterInterfaceReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version !=
        kProviderSdkAdapterInterfacePlanFormatVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review source format_version "
            "must be '" +
                std::string(kProviderSdkAdapterInterfacePlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_sdk_adapter_request_identity.empty() ||
        review.durable_store_import_provider_sdk_adapter_interface_identity.empty() ||
        review.provider_registry_identity.empty() || review.adapter_descriptor_identity.empty() ||
        review.capability_descriptor_identity.empty() || review.error_taxonomy_version.empty() ||
        review.interface_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review identity and summary "
            "fields must not be empty");
    }
    if (review.error_taxonomy_version != kProviderSdkAdapterErrorTaxonomyVersion) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review error_taxonomy_version "
            "must be '" +
                std::string(kProviderSdkAdapterErrorTaxonomyVersion) + "'");
    }
    provider_sdk_interface_detail_validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider SDK adapter interface review");
    provider_sdk_interface_detail_validate_no_side_effects(
        review.materializes_sdk_request_payload,
        review.invokes_provider_sdk,
        review.opens_network_connection,
        review.reads_secret_material,
        review.reads_host_environment,
        review.writes_host_filesystem,
        diagnostics,
        "durable store import provider SDK adapter interface review");

    if (review.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        if (review.operation_kind !=
                ProviderSdkAdapterInterfaceOperationKind::PlanProviderSdkAdapterInterface ||
            review.capability_support_status !=
                ProviderSdkAdapterCapabilitySupportStatus::Supported ||
            review.normalized_error_kind != ProviderSdkAdapterNormalizedErrorKind::None ||
            review.next_action != ProviderSdkAdapterInterfaceReviewNextActionKind::
                                      ReadyForMockAdapterImplementation ||
            review.failure_attribution.has_value()) {
            provider_sdk_interface_detail_emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface review ready status requires "
                "supported capability and ready next action");
        }
    } else if (!review.failure_attribution.has_value()) {
        provider_sdk_interface_detail_emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review blocked status requires "
            "failure_attribution");
    }

    return result;
}

ProviderSdkAdapterInterfacePlanResult
build_provider_sdk_adapter_interface_plan(const ProviderSdkAdapterRequestPlan &request_plan) {
    ProviderSdkAdapterInterfacePlanResult result;
    const auto source_validation = validate_provider_sdk_adapter_request_plan(request_plan);
    result.diagnostics.append(source_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto ready = request_plan.request_status == ProviderSdkAdapterStatus::Ready &&
                       request_plan.provider_sdk_adapter_request_identity.has_value();
    const auto status = ready ? ProviderSdkAdapterInterfaceStatus::Ready
                              : ProviderSdkAdapterInterfaceStatus::Blocked;

    ProviderSdkAdapterInterfacePlan plan;
    plan.workflow_canonical_name = request_plan.workflow_canonical_name;
    plan.session_id = request_plan.session_id;
    plan.run_id = request_plan.run_id;
    plan.input_fixture = request_plan.input_fixture;
    plan.durable_store_import_provider_sdk_adapter_request_identity =
        request_plan.durable_store_import_provider_sdk_adapter_request_identity;
    plan.source_sdk_adapter_request_status = request_plan.request_status;
    plan.source_provider_sdk_adapter_request_identity =
        request_plan.provider_sdk_adapter_request_identity;
    plan.durable_store_import_provider_sdk_adapter_interface_identity =
        provider_sdk_interface_detail_interface_plan_identity(request_plan, status);
    plan.interface_status = status;
    plan.provider_registry_identity =
        provider_sdk_interface_detail_provider_registry_identity(request_plan);
    plan.adapter_descriptor_identity =
        provider_sdk_interface_detail_adapter_descriptor_identity(request_plan);
    plan.capability_descriptor_identity =
        provider_sdk_interface_detail_capability_descriptor_identity(request_plan);

    if (ready) {
        plan.operation_kind =
            ProviderSdkAdapterInterfaceOperationKind::PlanProviderSdkAdapterInterface;
        plan.capability_descriptor.support_status =
            ProviderSdkAdapterCapabilitySupportStatus::Supported;
        plan.normalized_error_kind = ProviderSdkAdapterNormalizedErrorKind::None;
        plan.returns_placeholder_result = true;
    } else {
        plan.operation_kind =
            ProviderSdkAdapterInterfaceOperationKind::NoopSdkAdapterRequestNotReady;
        plan.capability_descriptor.support_status =
            ProviderSdkAdapterCapabilitySupportStatus::Unsupported;
        plan.normalized_error_kind =
            ProviderSdkAdapterNormalizedErrorKind::SdkAdapterRequestNotReady;
        plan.returns_placeholder_result = false;
        plan.capability_descriptor.supports_placeholder_response = false;
        plan.failure_attribution =
            provider_sdk_interface_detail_request_not_ready_failure(request_plan);
    }

    const auto validation = validate_provider_sdk_adapter_interface_plan(plan);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(plan);
    return result;
}

ProviderSdkAdapterInterfaceReviewResult
build_provider_sdk_adapter_interface_review(const ProviderSdkAdapterInterfacePlan &plan) {
    ProviderSdkAdapterInterfaceReviewResult result;
    const auto source_validation = validate_provider_sdk_adapter_interface_plan(plan);
    result.diagnostics.append(source_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderSdkAdapterInterfaceReview review;
    review.workflow_canonical_name = plan.workflow_canonical_name;
    review.session_id = plan.session_id;
    review.run_id = plan.run_id;
    review.input_fixture = plan.input_fixture;
    review.durable_store_import_provider_sdk_adapter_request_identity =
        plan.durable_store_import_provider_sdk_adapter_request_identity;
    review.durable_store_import_provider_sdk_adapter_interface_identity =
        plan.durable_store_import_provider_sdk_adapter_interface_identity;
    review.interface_status = plan.interface_status;
    review.operation_kind = plan.operation_kind;
    review.provider_registry_identity = plan.provider_registry_identity;
    review.adapter_descriptor_identity = plan.adapter_descriptor_identity;
    review.capability_descriptor_identity = plan.capability_descriptor_identity;
    review.capability_support_status = plan.capability_descriptor.support_status;
    review.error_taxonomy_version = plan.error_taxonomy_version;
    review.normalized_error_kind = plan.normalized_error_kind;
    review.returns_placeholder_result = plan.returns_placeholder_result;
    review.materializes_sdk_request_payload = plan.materializes_sdk_request_payload;
    review.invokes_provider_sdk = plan.invokes_provider_sdk;
    review.opens_network_connection = plan.opens_network_connection;
    review.reads_secret_material = plan.reads_secret_material;
    review.reads_host_environment = plan.reads_host_environment;
    review.writes_host_filesystem = plan.writes_host_filesystem;
    review.failure_attribution = plan.failure_attribution;
    review.interface_boundary_summary =
        provider_sdk_interface_detail_interface_boundary_summary(plan);
    review.next_step_recommendation = provider_sdk_interface_detail_next_step_summary(plan);
    review.next_action = provider_sdk_interface_detail_next_action_for_plan(plan);

    const auto validation = validate_provider_sdk_adapter_interface_review(review);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_sdk_payload.cpp ----

#include "durable_store_import/provider/sdk/payload.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_sdk_payload_detail_kValidationDiagnosticCode{"DSI_PROVIDER_SDK_PAYLOAD"};

void provider_sdk_payload_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                       std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_sdk_payload_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_sdk_payload_detail_status_slug(ProviderSdkPayloadStatus status) {
    return status == ProviderSdkPayloadStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string
provider_sdk_payload_detail_plan_identity(const ProviderLocalHostHarnessReview &review,
                                          ProviderSdkPayloadStatus status) {
    return "durable-store-import-provider-sdk-payload-plan::" + review.session_id +
           "::" + provider_sdk_payload_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_sdk_payload_detail_stable_fake_digest(const ProviderLocalHostHarnessReview &review) {
    return "sha256:fake-provider-payload:" + review.session_id + ":" + review.input_fixture;
}

[[nodiscard]] ProviderSdkPayloadFailureAttribution
provider_sdk_payload_detail_harness_not_ready_failure(
    const ProviderLocalHostHarnessReview &review) {
    std::string message =
        "SDK payload materialization cannot proceed because local host harness is not ready";
    if (review.failure_attribution.has_value()) {
        message = review.failure_attribution->message;
    }
    return ProviderSdkPayloadFailureAttribution{
        .kind = ProviderSdkPayloadFailureKind::HarnessNotReady,
        .message = std::move(message),
    };
}

void provider_sdk_payload_detail_validate_failure(
    const std::optional<ProviderSdkPayloadFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_sdk_payload_detail_validate_redaction(
    const ProviderSdkPayloadRedactionSummary &summary,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (!summary.secret_free || !summary.credential_free || !summary.token_free ||
        summary.summary.empty() || summary.redacted_field_count < 0) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            std::string(owner) +
                " redaction summary must be secret-free, credential-free, and token-free");
    }
}

void provider_sdk_payload_detail_validate_no_forbidden_material(
    const ProviderSdkPayloadMaterializationPlan &plan, DiagnosticBag &diagnostics) {
    if (plan.raw_payload.has_value()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan cannot persist raw_payload");
    }
    if (plan.secret_value.has_value()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan cannot contain secret_value");
    }
    if (plan.credential_material.has_value()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan cannot contain credential_material");
    }
    if (plan.token_value.has_value()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan cannot contain token_value");
    }
}

void provider_sdk_payload_detail_validate_materialization_boundary(
    const ProviderSdkPayloadMaterializationPlan &plan, DiagnosticBag &diagnostics) {
    if (!plan.fake_provider_only) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan must be fake-provider-only");
    }
    if (plan.provider_request_payload_schema_ref != kProviderFakeSdkPayloadSchemaVersion) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload plan schema ref must be '" +
                std::string(kProviderFakeSdkPayloadSchemaVersion) + "'");
    }
    if (plan.payload_digest.empty()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan payload_digest must not be empty");
    }
    if (plan.persists_materialized_payload) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan cannot persist materialized payload");
    }
    if (plan.opens_network_connection || plan.reads_secret_material ||
        plan.materializes_secret_value || plan.materializes_credential_material ||
        plan.materializes_token_value || plan.invokes_provider_sdk) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload plan cannot open network, read secret, "
            "materialize secret material, or invoke provider SDK");
    }
}

[[nodiscard]] ProviderSdkPayloadNextActionKind provider_sdk_payload_detail_next_action_for_plan(
    const ProviderSdkPayloadMaterializationPlan &plan) {
    if (plan.payload_status == ProviderSdkPayloadStatus::Ready) {
        return ProviderSdkPayloadNextActionKind::ReadyForMockAdapter;
    }
    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderSdkPayloadFailureKind::HarnessNotReady) {
        return ProviderSdkPayloadNextActionKind::WaitForLocalHarness;
    }
    return ProviderSdkPayloadNextActionKind::ManualReviewRequired;
}

} // namespace

ProviderSdkPayloadPlanValidationResult validate_provider_sdk_payload_materialization_plan(
    const ProviderSdkPayloadMaterializationPlan &plan) {
    ProviderSdkPayloadPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderSdkPayloadMaterializationPlanFormatVersion) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload plan format_version must be '" +
                std::string(kProviderSdkPayloadMaterializationPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_local_host_harness_review_format_version !=
        kProviderLocalHostHarnessReviewFormatVersion) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload plan source format_version must be '" +
                std::string(kProviderLocalHostHarnessReviewFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_local_host_harness_record_identity.empty() ||
        plan.durable_store_import_provider_sdk_payload_plan_identity.empty()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics, "provider SDK payload plan identity fields must not be empty");
    }
    provider_sdk_payload_detail_validate_failure(
        plan.failure_attribution, diagnostics, "provider SDK payload plan");
    provider_sdk_payload_detail_validate_redaction(
        plan.redaction_summary, diagnostics, "provider SDK payload plan");
    provider_sdk_payload_detail_validate_materialization_boundary(plan, diagnostics);
    provider_sdk_payload_detail_validate_no_forbidden_material(plan, diagnostics);
    if (plan.payload_status == ProviderSdkPayloadStatus::Ready) {
        if (plan.source_harness_next_action !=
                ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization ||
            plan.operation_kind !=
                ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization ||
            plan.failure_attribution.has_value()) {
            provider_sdk_payload_detail_emit_validation_error(
                diagnostics,
                "provider SDK payload plan ready status requires ready harness and no failure");
        }
    } else if (plan.operation_kind ==
                   ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization ||
               !plan.failure_attribution.has_value()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload plan blocked status requires noop operation "
            "and failure_attribution");
    }
    return result;
}

ProviderSdkPayloadAuditValidationResult
validate_provider_sdk_payload_audit_summary(const ProviderSdkPayloadAuditSummary &audit) {
    ProviderSdkPayloadAuditValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (audit.format_version != kProviderSdkPayloadAuditSummaryFormatVersion) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary format_version must be '" +
                std::string(kProviderSdkPayloadAuditSummaryFormatVersion) + "'");
    }
    if (audit
            .source_durable_store_import_provider_sdk_payload_materialization_plan_format_version !=
        kProviderSdkPayloadMaterializationPlanFormatVersion) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary source format_version must be '" +
                std::string(kProviderSdkPayloadMaterializationPlanFormatVersion) + "'");
    }
    if (audit.workflow_canonical_name.empty() || audit.session_id.empty() ||
        audit.input_fixture.empty() ||
        audit.durable_store_import_provider_sdk_payload_plan_identity.empty() ||
        audit.provider_request_payload_schema_ref.empty() || audit.payload_digest.empty() ||
        audit.audit_summary.empty() || audit.next_step_recommendation.empty()) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary identity and summary fields must not be empty");
    }
    if (!audit.fake_provider_only || audit.raw_payload_persisted) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary must be fake-only with no raw payload persistence");
    }
    provider_sdk_payload_detail_validate_redaction(
        audit.redaction_summary, diagnostics, "provider SDK payload audit summary");
    provider_sdk_payload_detail_validate_failure(
        audit.failure_attribution, diagnostics, "provider SDK payload audit summary");
    if (audit.next_action == ProviderSdkPayloadNextActionKind::ReadyForMockAdapter &&
        (audit.payload_status != ProviderSdkPayloadStatus::Ready ||
         audit.failure_attribution.has_value())) {
        provider_sdk_payload_detail_emit_validation_error(
            diagnostics,
            "provider SDK payload audit summary mock adapter next action "
            "requires ready payload and no failure");
    }
    return result;
}

ProviderSdkPayloadPlanResult
build_provider_sdk_payload_materialization_plan(const ProviderLocalHostHarnessReview &review) {
    ProviderSdkPayloadPlanResult result;
    result.diagnostics.append(validate_provider_local_host_harness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    const auto ready = review.next_action ==
                       ProviderLocalHostHarnessNextActionKind::ReadyForSdkPayloadMaterialization;
    const auto status = ready ? ProviderSdkPayloadStatus::Ready : ProviderSdkPayloadStatus::Blocked;
    ProviderSdkPayloadMaterializationPlan plan;
    plan.workflow_canonical_name = review.workflow_canonical_name;
    plan.session_id = review.session_id;
    plan.run_id = review.run_id;
    plan.input_fixture = review.input_fixture;
    plan.durable_store_import_provider_local_host_harness_record_identity =
        review.durable_store_import_provider_local_host_harness_record_identity;
    plan.source_harness_next_action = review.next_action;
    plan.durable_store_import_provider_sdk_payload_plan_identity =
        provider_sdk_payload_detail_plan_identity(review, status);
    plan.payload_status = status;
    plan.payload_digest =
        ready ? provider_sdk_payload_detail_stable_fake_digest(review) : "sha256:blocked";
    if (ready) {
        plan.operation_kind =
            ProviderSdkPayloadOperationKind::PlanFakeProviderPayloadMaterialization;
    } else {
        plan.operation_kind = ProviderSdkPayloadOperationKind::NoopHarnessNotReady;
        plan.failure_attribution = provider_sdk_payload_detail_harness_not_ready_failure(review);
    }
    result.diagnostics.append(validate_provider_sdk_payload_materialization_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderSdkPayloadAuditResult
build_provider_sdk_payload_audit_summary(const ProviderSdkPayloadMaterializationPlan &plan) {
    ProviderSdkPayloadAuditResult result;
    result.diagnostics.append(validate_provider_sdk_payload_materialization_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    ProviderSdkPayloadAuditSummary audit;
    audit.workflow_canonical_name = plan.workflow_canonical_name;
    audit.session_id = plan.session_id;
    audit.run_id = plan.run_id;
    audit.input_fixture = plan.input_fixture;
    audit.durable_store_import_provider_sdk_payload_plan_identity =
        plan.durable_store_import_provider_sdk_payload_plan_identity;
    audit.payload_status = plan.payload_status;
    audit.provider_request_payload_schema_ref = plan.provider_request_payload_schema_ref;
    audit.payload_digest = plan.payload_digest;
    audit.redaction_summary = plan.redaction_summary;
    audit.fake_provider_only = plan.fake_provider_only;
    audit.raw_payload_persisted = plan.persists_materialized_payload;
    audit.audit_summary = plan.payload_status == ProviderSdkPayloadStatus::Ready
                              ? "fake SDK payload digest is ready and raw payload was not persisted"
                              : "fake SDK payload materialization is blocked";
    audit.next_action = provider_sdk_payload_detail_next_action_for_plan(plan);
    audit.next_step_recommendation =
        audit.next_action == ProviderSdkPayloadNextActionKind::ReadyForMockAdapter
            ? "ready for provider SDK mock adapter contract"
            : "wait for successful local harness before mock adapter execution";
    audit.failure_attribution = plan.failure_attribution;
    result.diagnostics.append(validate_provider_sdk_payload_audit_summary(audit).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.audit = std::move(audit);
    return result;
}

} // namespace ahfl::durable_store_import

// ---- provider_sdk_mock_adapter.cpp ----

#include "durable_store_import/provider/sdk/mock_adapter.hpp"
#include "validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_sdk_mock_adapter_detail_kValidationDiagnosticCode{"DSI_PROVIDER_SDK_MOCK_ADAPTER"};

void provider_sdk_mock_adapter_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                            std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_sdk_mock_adapter_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_sdk_mock_adapter_detail_status_slug(ProviderSdkMockAdapterStatus status) {
    return status == ProviderSdkMockAdapterStatus::Ready ? "ready" : "blocked";
}

[[nodiscard]] std::string
provider_sdk_mock_adapter_detail_scenario_slug(ProviderSdkMockAdapterScenarioKind scenario) {
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

[[nodiscard]] std::string
provider_sdk_mock_adapter_detail_contract_identity(const ProviderSdkPayloadAuditSummary &audit,
                                                   ProviderSdkMockAdapterStatus status) {
    return "durable-store-import-provider-sdk-mock-adapter-contract::" + audit.session_id +
           "::" + provider_sdk_mock_adapter_detail_status_slug(status);
}

[[nodiscard]] std::string
provider_sdk_mock_adapter_detail_result_identity(const ProviderSdkMockAdapterContract &contract) {
    return "durable-store-import-provider-sdk-mock-adapter-result::" + contract.session_id +
           "::" + provider_sdk_mock_adapter_detail_scenario_slug(contract.scenario_kind);
}

[[nodiscard]] ProviderSdkMockAdapterFailureAttribution
provider_sdk_mock_adapter_detail_payload_not_ready_failure(
    const ProviderSdkPayloadAuditSummary &audit) {
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
provider_sdk_mock_adapter_detail_contract_not_ready_failure(
    const ProviderSdkMockAdapterContract &contract) {
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

void provider_sdk_mock_adapter_detail_validate_failure(
    const std::optional<ProviderSdkMockAdapterFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void provider_sdk_mock_adapter_detail_validate_no_real_provider_access(
    bool opens_network_connection,
    bool reads_secret_material,
    bool materializes_credential_material,
    bool invokes_real_provider_sdk,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (opens_network_connection || reads_secret_material || materializes_credential_material ||
        invokes_real_provider_sdk) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " cannot open network, read secret, materialize credential "
                                 "material, or invoke real provider SDK");
    }
}

[[nodiscard]] ProviderSdkMockAdapterNextActionKind
provider_sdk_mock_adapter_detail_next_action_for_result(
    const ProviderSdkMockAdapterExecutionResult &result) {
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
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter contract format_version must be '" +
                std::string(kProviderSdkMockAdapterContractFormatVersion) + "'");
    }
    if (contract.source_durable_store_import_provider_sdk_payload_audit_summary_format_version !=
        kProviderSdkPayloadAuditSummaryFormatVersion) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter contract source format_version must be '" +
                std::string(kProviderSdkPayloadAuditSummaryFormatVersion) + "'");
    }
    if (contract.workflow_canonical_name.empty() || contract.session_id.empty() ||
        contract.input_fixture.empty() ||
        contract.durable_store_import_provider_sdk_payload_plan_identity.empty() ||
        contract.durable_store_import_provider_sdk_mock_adapter_contract_identity.empty() ||
        contract.provider_request_payload_schema_ref.empty() || contract.payload_digest.empty()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics, "provider SDK mock adapter contract identity fields must not be empty");
    }
    if (!contract.fake_provider_only ||
        contract.provider_request_payload_schema_ref != kProviderFakeSdkPayloadSchemaVersion) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics, "provider SDK mock adapter contract must target fake provider schema");
    }
    provider_sdk_mock_adapter_detail_validate_failure(
        contract.failure_attribution, diagnostics, "provider SDK mock adapter contract");
    provider_sdk_mock_adapter_detail_validate_no_real_provider_access(
        contract.opens_network_connection,
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
            provider_sdk_mock_adapter_detail_emit_validation_error(
                diagnostics,
                "provider SDK mock adapter contract ready status requires ready "
                "payload and no failure");
        }
    } else if (contract.operation_kind ==
                   ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract ||
               !contract.failure_attribution.has_value()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
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
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter execution result format_version must be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (result_record
            .source_durable_store_import_provider_sdk_mock_adapter_contract_format_version !=
        kProviderSdkMockAdapterContractFormatVersion) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter execution result source format_version must be '" +
                std::string(kProviderSdkMockAdapterContractFormatVersion) + "'");
    }
    if (result_record.workflow_canonical_name.empty() || result_record.session_id.empty() ||
        result_record.input_fixture.empty() ||
        result_record.durable_store_import_provider_sdk_mock_adapter_contract_identity.empty() ||
        result_record.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        result_record.normalized_message.empty()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter execution result identity and message "
            "fields must not be empty");
    }
    provider_sdk_mock_adapter_detail_validate_failure(result_record.failure_attribution,
                                                      diagnostics,
                                                      "provider SDK mock adapter execution result");
    provider_sdk_mock_adapter_detail_validate_no_real_provider_access(
        result_record.opens_network_connection,
        result_record.reads_secret_material,
        result_record.materializes_credential_material,
        result_record.invokes_real_provider_sdk,
        diagnostics,
        "provider SDK mock adapter execution result");
    if (result_record.result_status == ProviderSdkMockAdapterStatus::Ready) {
        if (result_record.source_contract_status != ProviderSdkMockAdapterStatus::Ready ||
            result_record.operation_kind != ProviderSdkMockAdapterOperationKind::RunMockAdapter) {
            provider_sdk_mock_adapter_detail_emit_validation_error(
                diagnostics,
                "provider SDK mock adapter execution result ready status "
                "requires ready contract and mock run");
        }
    } else if (result_record.operation_kind ==
                   ProviderSdkMockAdapterOperationKind::RunMockAdapter ||
               !result_record.failure_attribution.has_value()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
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
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter readiness format_version must be '" +
                std::string(kProviderSdkMockAdapterReadinessFormatVersion) + "'");
    }
    if (readiness
            .source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version !=
        kProviderSdkMockAdapterExecutionResultFormatVersion) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter readiness source format_version must be '" +
                std::string(kProviderSdkMockAdapterExecutionResultFormatVersion) + "'");
    }
    if (readiness.workflow_canonical_name.empty() || readiness.session_id.empty() ||
        readiness.input_fixture.empty() ||
        readiness.durable_store_import_provider_sdk_mock_adapter_result_identity.empty() ||
        readiness.normalization_summary.empty() || readiness.next_step_recommendation.empty()) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
            "provider SDK mock adapter readiness identity and summary fields must not be empty");
    }
    provider_sdk_mock_adapter_detail_validate_failure(
        readiness.failure_attribution, diagnostics, "provider SDK mock adapter readiness");
    if (readiness.next_action == ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity &&
        (readiness.result_status != ProviderSdkMockAdapterStatus::Ready ||
         readiness.normalized_result != ProviderSdkMockAdapterNormalizedResultKind::Accepted ||
         readiness.failure_attribution.has_value())) {
        provider_sdk_mock_adapter_detail_emit_validation_error(
            diagnostics,
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
        provider_sdk_mock_adapter_detail_contract_identity(audit, status);
    contract.contract_status = status;
    contract.provider_request_payload_schema_ref = audit.provider_request_payload_schema_ref;
    contract.payload_digest = audit.payload_digest;
    contract.fake_provider_only = audit.fake_provider_only;
    if (ready) {
        contract.operation_kind = ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract;
    } else {
        contract.operation_kind = ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady;
        contract.failure_attribution =
            provider_sdk_mock_adapter_detail_payload_not_ready_failure(audit);
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
        record.failure_attribution =
            provider_sdk_mock_adapter_detail_contract_not_ready_failure(contract);
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
        provider_sdk_mock_adapter_detail_result_identity(contract);
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
    readiness.next_action = provider_sdk_mock_adapter_detail_next_action_for_result(result_record);
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
