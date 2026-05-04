#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_recovery.hpp"
#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderFailureTaxonomyReportFormatVersion =
    "ahfl.durable-store-import-provider-failure-taxonomy-report.v1";

inline constexpr std::string_view kProviderFailureTaxonomyReviewFormatVersion =
    "ahfl.durable-store-import-provider-failure-taxonomy-review.v1";

enum class ProviderFailureKindV1 {
    None,
    Authentication,
    Authorization,
    Network,
    Throttling,
    Conflict,
    SchemaMismatch,
    ProviderInternal,
    Unknown,
    Blocked,
};

enum class ProviderFailureCategoryV1 {
    None,
    Security,
    Transport,
    RateLimit,
    Consistency,
    Contract,
    Provider,
    Unknown,
    Blocked,
};

enum class ProviderFailureRetryabilityV1 {
    NotApplicable,
    Retryable,
    NonRetryable,
    DuplicateReviewRequired,
    Blocked,
};

enum class ProviderFailureOperatorActionV1 {
    None,
    RotateCredentials,
    GrantPermission,
    RetryLater,
    InspectDuplicate,
    FixSchema,
    EscalateProvider,
    ManualReview,
};

enum class ProviderFailureSecuritySensitivityV1 {
    None,
    SecretRelated,
    PermissionRelated,
    NonSensitive,
    Unknown,
};

struct ProviderFailureTaxonomyReport {
    std::string format_version{std::string(kProviderFailureTaxonomyReportFormatVersion)};
    std::string
        source_durable_store_import_provider_sdk_mock_adapter_execution_result_format_version{
            std::string(kProviderSdkMockAdapterExecutionResultFormatVersion)};
    std::string source_durable_store_import_provider_write_recovery_plan_format_version{
        std::string(kProviderWriteRecoveryPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_mock_adapter_result_identity;
    std::string durable_store_import_provider_write_recovery_plan_identity;
    std::string durable_store_import_provider_failure_taxonomy_report_identity;
    ProviderSdkMockAdapterNormalizedResultKind source_normalized_result{
        ProviderSdkMockAdapterNormalizedResultKind::Blocked};
    ProviderFailureKindV1 failure_kind{ProviderFailureKindV1::Blocked};
    ProviderFailureCategoryV1 failure_category{ProviderFailureCategoryV1::Blocked};
    ProviderFailureRetryabilityV1 retryability{ProviderFailureRetryabilityV1::Blocked};
    ProviderFailureOperatorActionV1 operator_action{ProviderFailureOperatorActionV1::ManualReview};
    ProviderFailureSecuritySensitivityV1 security_sensitivity{
        ProviderFailureSecuritySensitivityV1::Unknown};
    std::string redacted_failure_summary;
    std::string recovery_review_failure_summary;
    bool secret_bearing_error_persisted{false};
    bool raw_provider_error_persisted{false};
};

struct ProviderFailureTaxonomyReview {
    std::string format_version{std::string(kProviderFailureTaxonomyReviewFormatVersion)};
    std::string source_durable_store_import_provider_failure_taxonomy_report_format_version{
        std::string(kProviderFailureTaxonomyReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_failure_taxonomy_report_identity;
    ProviderFailureKindV1 failure_kind{ProviderFailureKindV1::Blocked};
    ProviderFailureRetryabilityV1 retryability{ProviderFailureRetryabilityV1::Blocked};
    ProviderFailureOperatorActionV1 operator_action{ProviderFailureOperatorActionV1::ManualReview};
    ProviderFailureSecuritySensitivityV1 security_sensitivity{
        ProviderFailureSecuritySensitivityV1::Unknown};
    std::string taxonomy_review_summary;
    std::string operator_recommendation;
};

struct ProviderFailureTaxonomyReportValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderFailureTaxonomyReviewValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderFailureTaxonomyReportResult {
    std::optional<ProviderFailureTaxonomyReport> report;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderFailureTaxonomyReviewResult {
    std::optional<ProviderFailureTaxonomyReview> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderFailureTaxonomyReportValidationResult
validate_provider_failure_taxonomy_report(const ProviderFailureTaxonomyReport &report);

[[nodiscard]] ProviderFailureTaxonomyReviewValidationResult
validate_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReview &review);

[[nodiscard]] ProviderFailureTaxonomyReportResult
build_provider_failure_taxonomy_report(const ProviderSdkMockAdapterExecutionResult &adapter_result,
                                       const ProviderWriteRecoveryPlan &recovery_plan);

[[nodiscard]] ProviderFailureTaxonomyReviewResult
build_provider_failure_taxonomy_review(const ProviderFailureTaxonomyReport &report);

} // namespace ahfl::durable_store_import
