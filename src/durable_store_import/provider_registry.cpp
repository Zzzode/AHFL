#include "ahfl/durable_store_import/provider_registry.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_REGISTRY";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string selection_slug(ProviderSelectionStatus status) {
    switch (status) {
    case ProviderSelectionStatus::Selected:
        return "selected";
    case ProviderSelectionStatus::FallbackSelected:
        return "fallback-selected";
    case ProviderSelectionStatus::Blocked:
        return "blocked";
    }
    return "blocked";
}

[[nodiscard]] ProviderCapabilityNegotiationStatus
negotiation_status_for_selection(ProviderSelectionStatus status) {
    switch (status) {
    case ProviderSelectionStatus::Selected:
        return ProviderCapabilityNegotiationStatus::Compatible;
    case ProviderSelectionStatus::FallbackSelected:
        return ProviderCapabilityNegotiationStatus::Degraded;
    case ProviderSelectionStatus::Blocked:
        return ProviderCapabilityNegotiationStatus::Blocked;
    }
    return ProviderCapabilityNegotiationStatus::Blocked;
}

} // namespace

ProviderRegistryValidationResult validate_provider_registry(const ProviderRegistry &registry) {
    ProviderRegistryValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (registry.format_version != kProviderRegistryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider registry format_version must be '" +
                                  std::string(kProviderRegistryFormatVersion) + "'");
    }
    if (registry.source_durable_store_import_provider_compatibility_report_format_version !=
        kProviderCompatibilityReportFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider registry source format_version must be '" +
                                  std::string(kProviderCompatibilityReportFormatVersion) + "'");
    }
    if (registry.workflow_canonical_name.empty() || registry.session_id.empty() ||
        registry.input_fixture.empty() ||
        registry.durable_store_import_provider_compatibility_report_identity.empty() ||
        registry.durable_store_import_provider_registry_identity.empty() ||
        registry.primary_provider_id.empty() || registry.fallback_provider_id.empty()) {
        emit_validation_error(diagnostics,
                              "provider registry identity and provider fields must not be empty");
    }
    if (!registry.mock_adapter_registered || !registry.local_filesystem_alpha_registered ||
        registry.unaudited_fallback_allowed || registry.registered_provider_count < 2) {
        emit_validation_error(
            diagnostics, "provider registry must register audited mock and alpha providers only");
    }
    return result;
}

ProviderSelectionPlanValidationResult
validate_provider_selection_plan(const ProviderSelectionPlan &plan) {
    ProviderSelectionPlanValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (plan.format_version != kProviderSelectionPlanFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider selection plan format_version must be '" +
                                  std::string(kProviderSelectionPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_registry_format_version !=
        kProviderRegistryFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider selection plan source format_version must be '" +
                                  std::string(kProviderRegistryFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_registry_identity.empty() ||
        plan.durable_store_import_provider_selection_plan_identity.empty() ||
        plan.selected_provider_id.empty() || plan.fallback_provider_id.empty() ||
        plan.fallback_policy.empty() || plan.capability_gap_summary.empty()) {
        emit_validation_error(
            diagnostics, "provider selection plan identity and policy fields must not be empty");
    }
    if (plan.selection_status == ProviderSelectionStatus::Blocked &&
        !plan.requires_operator_review) {
        emit_validation_error(diagnostics,
                              "blocked provider selection plan must require operator review");
    }
    return result;
}

ProviderCapabilityNegotiationReviewValidationResult
validate_provider_capability_negotiation_review(const ProviderCapabilityNegotiationReview &review) {
    ProviderCapabilityNegotiationReviewValidationResult result;
    auto &diagnostics = result.diagnostics;
    if (review.format_version != kProviderCapabilityNegotiationReviewFormatVersion) {
        emit_validation_error(diagnostics,
                              "provider capability negotiation review format_version must be '" +
                                  std::string(kProviderCapabilityNegotiationReviewFormatVersion) +
                                  "'");
    }
    if (review.source_durable_store_import_provider_selection_plan_format_version !=
        kProviderSelectionPlanFormatVersion) {
        emit_validation_error(
            diagnostics,
            "provider capability negotiation review source format_version must be '" +
                std::string(kProviderSelectionPlanFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_selection_plan_identity.empty() ||
        review.selected_provider_id.empty() || review.negotiation_summary.empty() ||
        review.operator_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "provider capability negotiation review fields must not be empty");
    }
    return result;
}

ProviderRegistryResult build_provider_registry(const ProviderCompatibilityReport &report) {
    ProviderRegistryResult result;
    result.diagnostics.append(validate_provider_compatibility_report(report).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderRegistry registry;
    registry.workflow_canonical_name = report.workflow_canonical_name;
    registry.session_id = report.session_id;
    registry.run_id = report.run_id;
    registry.input_fixture = report.input_fixture;
    registry.durable_store_import_provider_compatibility_report_identity =
        report.durable_store_import_provider_compatibility_report_identity;
    registry.durable_store_import_provider_registry_identity =
        "durable-store-import-provider-registry::" + registry.session_id;
    registry.compatibility_status = report.status;

    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.registry = std::move(registry);
    return result;
}

ProviderSelectionPlanResult build_provider_selection_plan(const ProviderRegistry &registry) {
    ProviderSelectionPlanResult result;
    result.diagnostics.append(validate_provider_registry(registry).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderSelectionPlan plan;
    plan.workflow_canonical_name = registry.workflow_canonical_name;
    plan.session_id = registry.session_id;
    plan.run_id = registry.run_id;
    plan.input_fixture = registry.input_fixture;
    plan.durable_store_import_provider_registry_identity =
        registry.durable_store_import_provider_registry_identity;
    plan.fallback_provider_id = registry.fallback_provider_id;
    if (registry.compatibility_status == ProviderCompatibilityStatus::Passed) {
        plan.selection_status = ProviderSelectionStatus::Selected;
        plan.selected_provider_id = registry.primary_provider_id;
        plan.requires_operator_review = false;
        plan.capability_gap_summary = "no provider capability gap detected";
    } else if (registry.mock_adapter_registered &&
               registry.compatibility_status == ProviderCompatibilityStatus::Failed) {
        plan.selection_status = ProviderSelectionStatus::FallbackSelected;
        plan.selected_provider_id = registry.fallback_provider_id;
        plan.degradation_allowed = true;
        plan.capability_gap_summary =
            "primary provider compatibility failed; audited fallback selected";
    } else {
        plan.selection_status = ProviderSelectionStatus::Blocked;
        plan.selected_provider_id = registry.fallback_provider_id;
        plan.capability_gap_summary =
            "provider selection blocked until compatibility report passes";
    }
    plan.fallback_policy = "audited-fallback-only";
    plan.durable_store_import_provider_selection_plan_identity =
        "durable-store-import-provider-selection-plan::" + plan.session_id +
        "::" + selection_slug(plan.selection_status);

    result.diagnostics.append(validate_provider_selection_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.plan = std::move(plan);
    return result;
}

ProviderCapabilityNegotiationReviewResult
build_provider_capability_negotiation_review(const ProviderSelectionPlan &plan) {
    ProviderCapabilityNegotiationReviewResult result;
    result.diagnostics.append(validate_provider_selection_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderCapabilityNegotiationReview review;
    review.workflow_canonical_name = plan.workflow_canonical_name;
    review.session_id = plan.session_id;
    review.run_id = plan.run_id;
    review.input_fixture = plan.input_fixture;
    review.durable_store_import_provider_selection_plan_identity =
        plan.durable_store_import_provider_selection_plan_identity;
    review.selection_status = plan.selection_status;
    review.negotiation_status = negotiation_status_for_selection(plan.selection_status);
    review.selected_provider_id = plan.selected_provider_id;
    review.negotiation_summary =
        "provider capability negotiation is " + selection_slug(plan.selection_status);
    review.operator_recommendation = plan.requires_operator_review
                                         ? "operator review required before provider execution"
                                         : "provider selection can proceed";

    result.diagnostics.append(validate_provider_capability_negotiation_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }
    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
