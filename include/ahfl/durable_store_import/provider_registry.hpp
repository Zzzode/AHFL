#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_compatibility.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderRegistryFormatVersion =
    "ahfl.durable-store-import-provider-registry.v1";

inline constexpr std::string_view kProviderSelectionPlanFormatVersion =
    "ahfl.durable-store-import-provider-selection-plan.v1";

inline constexpr std::string_view kProviderCapabilityNegotiationReviewFormatVersion =
    "ahfl.durable-store-import-provider-capability-negotiation-review.v1";

enum class ProviderSelectionStatus {
    Selected,
    FallbackSelected,
    Blocked,
};

enum class ProviderCapabilityNegotiationStatus {
    Compatible,
    Degraded,
    Blocked,
};

struct ProviderRegistry {
    std::string format_version{std::string(kProviderRegistryFormatVersion)};
    std::string source_durable_store_import_provider_compatibility_report_format_version{
        std::string(kProviderCompatibilityReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_compatibility_report_identity;
    std::string durable_store_import_provider_registry_identity;
    std::string primary_provider_id{"local-filesystem-alpha"};
    std::string fallback_provider_id{"sdk-mock-adapter"};
    bool mock_adapter_registered{true};
    bool local_filesystem_alpha_registered{true};
    bool unaudited_fallback_allowed{false};
    int registered_provider_count{2};
    ProviderCompatibilityStatus compatibility_status{ProviderCompatibilityStatus::Blocked};
};

struct ProviderSelectionPlan {
    std::string format_version{std::string(kProviderSelectionPlanFormatVersion)};
    std::string source_durable_store_import_provider_registry_format_version{
        std::string(kProviderRegistryFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_registry_identity;
    std::string durable_store_import_provider_selection_plan_identity;
    std::string selected_provider_id;
    std::string fallback_provider_id;
    ProviderSelectionStatus selection_status{ProviderSelectionStatus::Blocked};
    bool degradation_allowed{false};
    bool requires_operator_review{true};
    std::string fallback_policy;
    std::string capability_gap_summary;
};

struct ProviderCapabilityNegotiationReview {
    std::string format_version{std::string(kProviderCapabilityNegotiationReviewFormatVersion)};
    std::string source_durable_store_import_provider_selection_plan_format_version{
        std::string(kProviderSelectionPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_selection_plan_identity;
    ProviderSelectionStatus selection_status{ProviderSelectionStatus::Blocked};
    ProviderCapabilityNegotiationStatus negotiation_status{
        ProviderCapabilityNegotiationStatus::Blocked};
    std::string selected_provider_id;
    std::string negotiation_summary;
    std::string operator_recommendation;
};

struct ProviderRegistryValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSelectionPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCapabilityNegotiationReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRegistryResult {
    std::optional<ProviderRegistry> registry;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderSelectionPlanResult {
    std::optional<ProviderSelectionPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCapabilityNegotiationReviewResult {
    std::optional<ProviderCapabilityNegotiationReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderRegistryValidationResult
validate_provider_registry(const ProviderRegistry &registry);

[[nodiscard]] ProviderSelectionPlanValidationResult
validate_provider_selection_plan(const ProviderSelectionPlan &plan);

[[nodiscard]] ProviderCapabilityNegotiationReviewValidationResult
validate_provider_capability_negotiation_review(const ProviderCapabilityNegotiationReview &review);

[[nodiscard]] ProviderRegistryResult
build_provider_registry(const ProviderCompatibilityReport &report);

[[nodiscard]] ProviderSelectionPlanResult
build_provider_selection_plan(const ProviderRegistry &registry);

[[nodiscard]] ProviderCapabilityNegotiationReviewResult
build_provider_capability_negotiation_review(const ProviderSelectionPlan &plan);

} // namespace ahfl::durable_store_import
