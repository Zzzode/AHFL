#include "ahfl/durable_store_import/provider_local_host_execution.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_LOCAL_HOST_EXECUTION";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string status_slug(ProviderLocalHostExecutionStatus status) {
    switch (status) {
    case ProviderLocalHostExecutionStatus::SimulatedReady:
        return "simulated-ready";
    case ProviderLocalHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_plan_identity(const ProviderHostExecutionPlan &plan,
                                                ProviderLocalHostExecutionStatus status) {
    return "durable-store-import-provider-local-host-execution-receipt::" + plan.session_id +
           "::" + status_slug(status);
}

[[nodiscard]] std::optional<std::string>
provider_local_host_execution_receipt_identity(const ProviderHostExecutionPlan &plan) {
    if (!plan.provider_host_execution_descriptor_identity.has_value()) {
        return std::nullopt;
    }

    return "provider-local-host-execution-receipt::" +
           *plan.provider_host_execution_descriptor_identity;
}

[[nodiscard]] ProviderLocalHostExecutionFailureAttribution
host_execution_not_ready_failure(const ProviderHostExecutionPlan &plan) {
    std::string message =
        "provider local host execution receipt cannot proceed because host execution is not ready";
    if (plan.failure_attribution.has_value()) {
        message = plan.failure_attribution->message;
    }

    return ProviderLocalHostExecutionFailureAttribution{
        .kind = ProviderLocalHostExecutionFailureKind::HostExecutionNotReady,
        .message = std::move(message),
    };
}

void validate_failure_attribution(
    const std::optional<ProviderLocalHostExecutionFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

void validate_no_side_effects(bool starts_host_process,
                              bool reads_host_environment,
                              bool opens_network_connection,
                              bool materializes_sdk_request_payload,
                              bool invokes_provider_sdk,
                              bool writes_host_filesystem,
                              DiagnosticBag &diagnostics,
                              std::string_view owner) {
    if (starts_host_process) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot start host process");
    }
    if (reads_host_environment) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot read host environment");
    }
    if (opens_network_connection) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot open network connection");
    }
    if (materializes_sdk_request_payload) {
        emit_validation_error(diagnostics,
                              std::string(owner) + " cannot materialize SDK request payload");
    }
    if (invokes_provider_sdk) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot invoke provider SDK");
    }
    if (writes_host_filesystem) {
        emit_validation_error(diagnostics, std::string(owner) + " cannot write host filesystem");
    }
}

[[nodiscard]] ProviderLocalHostExecutionReceiptReviewNextActionKind
next_action_for_receipt(const ProviderLocalHostExecutionReceipt &receipt) {
    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        return ProviderLocalHostExecutionReceiptReviewNextActionKind::
            ReadyForProviderSdkAdapterPrototype;
    }

    if (receipt.failure_attribution.has_value() &&
        receipt.failure_attribution->kind ==
            ProviderLocalHostExecutionFailureKind::HostExecutionNotReady) {
        return ProviderLocalHostExecutionReceiptReviewNextActionKind::WaitForHostExecutionPlan;
    }

    return ProviderLocalHostExecutionReceiptReviewNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderLocalHostExecutionReceipt &receipt) {
    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        return "provider local host execution receipt was simulated without starting a host "
               "process, reading host environment, opening network, materializing SDK payload, "
               "invoking a provider SDK, or writing host filesystem";
    }

    return "provider local host execution receipt was not simulated because host execution is not "
           "ready";
}

[[nodiscard]] std::string next_step_summary(const ProviderLocalHostExecutionReceipt &receipt) {
    switch (next_action_for_receipt(receipt)) {
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::ReadyForProviderSdkAdapterPrototype:
        return "ready for future provider SDK adapter prototype; local host execution remains "
               "simulated and side-effect free";
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::WaitForHostExecutionPlan:
        return "wait for a ready provider host execution plan before local host execution receipt "
               "simulation";
    case ProviderLocalHostExecutionReceiptReviewNextActionKind::ManualReviewRequired:
        return "manual review required before provider local host execution can proceed";
    }

    return "manual review required before provider local host execution can proceed";
}

} // namespace

ProviderLocalHostExecutionReceiptValidationResult
validate_provider_local_host_execution_receipt(const ProviderLocalHostExecutionReceipt &receipt) {
    ProviderLocalHostExecutionReceiptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kProviderLocalHostExecutionReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt format_version must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_provider_sdk_request_envelope_plan_format_version !=
        kProviderSdkRequestEnvelopePlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt "
            "source_durable_store_import_provider_sdk_request_envelope_plan_format_version must "
            "be '" +
                std::string(kProviderSdkRequestEnvelopePlanFormatVersion) + "'");
    }
    if (receipt.workflow_canonical_name.empty() || receipt.session_id.empty() ||
        receipt.input_fixture.empty() ||
        receipt.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        receipt.durable_store_import_provider_host_execution_identity.empty() ||
        receipt.durable_store_import_provider_local_host_execution_receipt_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt identity fields must not "
            "be empty");
    }
    validate_failure_attribution(receipt.failure_attribution,
                                 diagnostics,
                                 "durable store import provider local host execution receipt");
    validate_no_side_effects(receipt.starts_host_process,
                             receipt.reads_host_environment,
                             receipt.opens_network_connection,
                             receipt.materializes_sdk_request_payload,
                             receipt.invokes_provider_sdk,
                             receipt.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider local host execution receipt");

    if (receipt.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        if (receipt.source_host_execution_status != ProviderHostExecutionStatus::Ready ||
            !receipt.source_provider_host_execution_descriptor_identity.has_value() ||
            !receipt.source_provider_host_receipt_placeholder_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status requires ready host execution descriptor and receipt placeholder "
                "identities");
        }
        if (receipt.operation_kind != ProviderLocalHostExecutionOperationKind::
                                          SimulateProviderLocalHostExecutionReceipt ||
            !receipt.provider_local_host_execution_receipt_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status requires local host receipt identity");
        }
        if (receipt.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt simulated-ready "
                "status cannot contain failure_attribution");
        }
    } else {
        if (receipt.operation_kind ==
            ProviderLocalHostExecutionOperationKind::SimulateProviderLocalHostExecutionReceipt) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "cannot simulate local host execution");
        }
        if (receipt.provider_local_host_execution_receipt_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "cannot contain local host receipt identity");
        }
        if (!receipt.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt blocked status "
                "requires failure_attribution");
        }
    }

    return result;
}

ProviderLocalHostExecutionReceiptReviewValidationResult
validate_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceiptReview &review) {
    ProviderLocalHostExecutionReceiptReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderLocalHostExecutionReceiptReviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_local_host_execution_receipt_format_version !=
        kProviderLocalHostExecutionReceiptFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review "
            "source_durable_store_import_provider_local_host_execution_receipt_format_version "
            "must be '" +
                std::string(kProviderLocalHostExecutionReceiptFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_host_execution_plan_format_version !=
        kProviderHostExecutionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review "
            "source_durable_store_import_provider_host_execution_plan_format_version must be '" +
                std::string(kProviderHostExecutionPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_sdk_request_envelope_identity.empty() ||
        review.durable_store_import_provider_host_execution_identity.empty() ||
        review.durable_store_import_provider_local_host_execution_receipt_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review identity fields "
            "must not be empty");
    }
    if (review.local_host_execution_boundary_summary.empty() ||
        review.next_step_recommendation.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider local host execution receipt review summaries must "
            "not be empty");
    }
    validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider local host execution receipt review");
    validate_no_side_effects(review.starts_host_process,
                             review.reads_host_environment,
                             review.opens_network_connection,
                             review.materializes_sdk_request_payload,
                             review.invokes_provider_sdk,
                             review.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider local host execution receipt review");

    if (review.execution_status == ProviderLocalHostExecutionStatus::SimulatedReady) {
        if (review.operation_kind != ProviderLocalHostExecutionOperationKind::
                                         SimulateProviderLocalHostExecutionReceipt ||
            !review.provider_local_host_execution_receipt_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review "
                "simulated-ready status requires local host receipt identity");
        }
        if (review.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review "
                "simulated-ready status cannot contain failure_attribution");
        }
    } else {
        if (review.provider_local_host_execution_receipt_identity.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review blocked "
                "status cannot contain local host receipt identity");
        }
        if (!review.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider local host execution receipt review blocked "
                "status requires failure_attribution");
        }
    }

    return result;
}

ProviderLocalHostExecutionReceiptResult
build_provider_local_host_execution_receipt(const ProviderHostExecutionPlan &plan) {
    ProviderLocalHostExecutionReceiptResult result{
        .receipt = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_host_execution_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto can_simulate = plan.execution_status == ProviderHostExecutionStatus::Ready &&
                              plan.provider_host_execution_descriptor_identity.has_value() &&
                              plan.provider_host_receipt_placeholder_identity.has_value();
    const auto status = can_simulate ? ProviderLocalHostExecutionStatus::SimulatedReady
                                     : ProviderLocalHostExecutionStatus::Blocked;
    const auto operation_kind =
        can_simulate
            ? ProviderLocalHostExecutionOperationKind::SimulateProviderLocalHostExecutionReceipt
            : ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady;

    ProviderLocalHostExecutionReceipt receipt{
        .format_version = std::string(kProviderLocalHostExecutionReceiptFormatVersion),
        .source_durable_store_import_provider_host_execution_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_sdk_request_envelope_plan_format_version =
            plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_sdk_request_envelope_identity =
            plan.durable_store_import_provider_sdk_request_envelope_identity,
        .durable_store_import_provider_host_execution_identity =
            plan.durable_store_import_provider_host_execution_identity,
        .source_host_execution_status = plan.execution_status,
        .source_provider_host_execution_descriptor_identity =
            plan.provider_host_execution_descriptor_identity,
        .source_provider_host_receipt_placeholder_identity =
            plan.provider_host_receipt_placeholder_identity,
        .durable_store_import_provider_local_host_execution_receipt_identity =
            receipt_plan_identity(plan, status),
        .operation_kind = operation_kind,
        .execution_status = status,
        .provider_local_host_execution_receipt_identity =
            can_simulate ? provider_local_host_execution_receipt_identity(plan) : std::nullopt,
        .starts_host_process = false,
        .reads_host_environment = false,
        .opens_network_connection = false,
        .materializes_sdk_request_payload = false,
        .invokes_provider_sdk = false,
        .writes_host_filesystem = false,
        .failure_attribution = can_simulate
                                   ? std::nullopt
                                   : std::optional<ProviderLocalHostExecutionFailureAttribution>(
                                         host_execution_not_ready_failure(plan)),
    };

    result.diagnostics.append(validate_provider_local_host_execution_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

ProviderLocalHostExecutionReceiptReviewResult build_provider_local_host_execution_receipt_review(
    const ProviderLocalHostExecutionReceipt &receipt) {
    ProviderLocalHostExecutionReceiptReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_local_host_execution_receipt(receipt).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderLocalHostExecutionReceiptReview review{
        .format_version = std::string(kProviderLocalHostExecutionReceiptReviewFormatVersion),
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
        .execution_status = receipt.execution_status,
        .operation_kind = receipt.operation_kind,
        .provider_local_host_execution_receipt_identity =
            receipt.provider_local_host_execution_receipt_identity,
        .starts_host_process = receipt.starts_host_process,
        .reads_host_environment = receipt.reads_host_environment,
        .opens_network_connection = receipt.opens_network_connection,
        .materializes_sdk_request_payload = receipt.materializes_sdk_request_payload,
        .invokes_provider_sdk = receipt.invokes_provider_sdk,
        .writes_host_filesystem = receipt.writes_host_filesystem,
        .failure_attribution = receipt.failure_attribution,
        .local_host_execution_boundary_summary = boundary_summary(receipt),
        .next_step_recommendation = next_step_summary(receipt),
        .next_action = next_action_for_receipt(receipt),
    };

    result.diagnostics.append(
        validate_provider_local_host_execution_receipt_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
