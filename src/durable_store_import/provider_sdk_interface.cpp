#include "ahfl/durable_store_import/provider_sdk_interface.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode =
    "AHFL.VAL.DSI_PROVIDER_SDK_ADAPTER_INTERFACE";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string interface_status_slug(ProviderSdkAdapterInterfaceStatus status) {
    switch (status) {
    case ProviderSdkAdapterInterfaceStatus::Ready:
        return "ready";
    case ProviderSdkAdapterInterfaceStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string interface_plan_identity(const ProviderSdkAdapterRequestPlan &plan,
                                                  ProviderSdkAdapterInterfaceStatus status) {
    return "durable-store-import-provider-sdk-adapter-interface::" + plan.session_id +
           "::" + interface_status_slug(status);
}

[[nodiscard]] std::string provider_registry_identity(const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-registry::" + plan.session_id + "::local-placeholder";
}

[[nodiscard]] std::string adapter_descriptor_identity(const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-descriptor::" + plan.session_id +
           "::local-placeholder-durable-store";
}

[[nodiscard]] std::string
capability_descriptor_identity(const ProviderSdkAdapterRequestPlan &plan) {
    return "provider-sdk-adapter-capability::" + plan.session_id + "::durable-store-import-write";
}

[[nodiscard]] ProviderSdkAdapterInterfaceFailureAttribution
request_not_ready_failure(const ProviderSdkAdapterRequestPlan &plan) {
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

void validate_failure_attribution(
    const std::optional<ProviderSdkAdapterInterfaceFailureAttribution> &failure,
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

void validate_forbidden_material(const ProviderSdkAdapterInterfacePlan &plan,
                                 DiagnosticBag &diagnostics) {
    if (plan.provider_endpoint_uri.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter interface plan cannot "
                              "contain provider_endpoint_uri");
    }
    if (plan.credential_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter interface plan cannot "
                              "contain credential_reference");
    }
    if (plan.sdk_request_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter interface plan cannot "
                              "contain sdk_request_payload");
    }
    if (plan.sdk_response_payload.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider SDK adapter interface plan cannot "
                              "contain sdk_response_payload");
    }
}

[[nodiscard]] ProviderSdkAdapterInterfaceReviewNextActionKind
next_action_for_plan(const ProviderSdkAdapterInterfacePlan &plan) {
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

[[nodiscard]] std::string interface_boundary_summary(const ProviderSdkAdapterInterfacePlan &plan) {
    if (plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        return "provider SDK adapter interface descriptor is ready with local placeholder "
               "registry, durable-store-import capability descriptor, and normalized error "
               "taxonomy; no SDK payload, SDK call, network, secret, host environment, or "
               "filesystem write was used";
    }

    return "provider SDK adapter interface descriptor is blocked because SDK adapter request is "
           "not ready";
}

[[nodiscard]] std::string next_step_summary(const ProviderSdkAdapterInterfacePlan &plan) {
    switch (next_action_for_plan(plan)) {
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
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan format_version must be '" +
                std::string(kProviderSdkAdapterInterfacePlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_sdk_adapter_request_plan_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        emit_validation_error(
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
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan identity and descriptor "
            "fields must not be empty");
    }
    if (plan.interface_abi_version != kProviderSdkAdapterInterfaceAbiVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan interface_abi_version must "
            "be '" +
                std::string(kProviderSdkAdapterInterfaceAbiVersion) + "'");
    }
    if (plan.error_taxonomy_version != kProviderSdkAdapterErrorTaxonomyVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan error_taxonomy_version must "
            "be '" +
                std::string(kProviderSdkAdapterErrorTaxonomyVersion) + "'");
    }
    if (plan.capability_descriptor.input_contract_format_version !=
        kProviderSdkAdapterRequestPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan capability input contract "
            "must be provider SDK adapter request plan");
    }
    if (plan.capability_descriptor.output_contract_format_version !=
        kProviderSdkAdapterResponsePlaceholderFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface plan capability output contract "
            "must be provider SDK adapter response placeholder");
    }
    validate_failure_attribution(plan.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK adapter interface plan");
    validate_no_side_effects(plan.materializes_sdk_request_payload,
                             plan.invokes_provider_sdk,
                             plan.opens_network_connection,
                             plan.reads_secret_material,
                             plan.reads_host_environment,
                             plan.writes_host_filesystem,
                             diagnostics,
                             "durable store import provider SDK adapter interface plan");
    validate_forbidden_material(plan, diagnostics);

    if (plan.interface_status == ProviderSdkAdapterInterfaceStatus::Ready) {
        if (plan.source_sdk_adapter_request_status != ProviderSdkAdapterStatus::Ready ||
            !plan.source_provider_sdk_adapter_request_identity.has_value()) {
            emit_validation_error(
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
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan ready status requires "
                "supported placeholder capability and no normalized error");
        }
        if (plan.failure_attribution.has_value()) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan ready status cannot "
                "contain failure_attribution");
        }
    } else {
        if (plan.operation_kind ==
            ProviderSdkAdapterInterfaceOperationKind::PlanProviderSdkAdapterInterface) {
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface plan blocked status cannot "
                "plan provider SDK adapter interface");
        }
        if (!plan.failure_attribution.has_value()) {
            emit_validation_error(
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
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review format_version must be '" +
                std::string(kProviderSdkAdapterInterfaceReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version !=
        kProviderSdkAdapterInterfacePlanFormatVersion) {
        emit_validation_error(
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
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review identity and summary "
            "fields must not be empty");
    }
    if (review.error_taxonomy_version != kProviderSdkAdapterErrorTaxonomyVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider SDK adapter interface review error_taxonomy_version "
            "must be '" +
                std::string(kProviderSdkAdapterErrorTaxonomyVersion) + "'");
    }
    validate_failure_attribution(review.failure_attribution,
                                 diagnostics,
                                 "durable store import provider SDK adapter interface review");
    validate_no_side_effects(review.materializes_sdk_request_payload,
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
            emit_validation_error(
                diagnostics,
                "durable store import provider SDK adapter interface review ready status requires "
                "supported capability and ready next action");
        }
    } else if (!review.failure_attribution.has_value()) {
        emit_validation_error(
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
        interface_plan_identity(request_plan, status);
    plan.interface_status = status;
    plan.provider_registry_identity = provider_registry_identity(request_plan);
    plan.adapter_descriptor_identity = adapter_descriptor_identity(request_plan);
    plan.capability_descriptor_identity = capability_descriptor_identity(request_plan);

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
        plan.failure_attribution = request_not_ready_failure(request_plan);
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
    review.interface_boundary_summary = interface_boundary_summary(plan);
    review.next_step_recommendation = next_step_summary(plan);
    review.next_action = next_action_for_plan(plan);

    const auto validation = validate_provider_sdk_adapter_interface_review(review);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
