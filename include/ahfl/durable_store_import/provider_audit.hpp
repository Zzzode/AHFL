#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderExecutionAuditEventFormatVersion =
    "ahfl.durable-store-import-provider-execution-audit-event.v1";

inline constexpr std::string_view kProviderTelemetrySummaryFormatVersion =
    "ahfl.durable-store-import-provider-telemetry-summary.v1";

inline constexpr std::string_view kProviderOperatorReviewEventFormatVersion =
    "ahfl.durable-store-import-provider-operator-review-event.v1";

inline constexpr std::string_view kProviderAuditRedactionPolicyVersion =
    "ahfl.provider-audit-redaction.v1";

enum class ProviderAuditOutcome {
    Success,
    Failure,
    RetryPending,
    RecoveryPending,
    Blocked,
};

enum class ProviderAuditNextActionKind {
    Archive,
    RetryWithIdempotency,
    RecoveryReview,
    OperatorReview,
};

struct ProviderExecutionAuditEvent {
    std::string format_version{std::string(kProviderExecutionAuditEventFormatVersion)};
    std::string source_durable_store_import_provider_failure_taxonomy_report_format_version{
        std::string(kProviderFailureTaxonomyReportFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_failure_taxonomy_report_identity;
    std::string durable_store_import_provider_execution_audit_event_identity;
    std::string event_name{"provider.execution.audit"};
    ProviderAuditOutcome outcome{ProviderAuditOutcome::Blocked};
    ProviderFailureKindV1 failure_kind{ProviderFailureKindV1::Blocked};
    ProviderFailureRetryabilityV1 retryability{ProviderFailureRetryabilityV1::Blocked};
    ProviderFailureSecuritySensitivityV1 security_sensitivity{
        ProviderFailureSecuritySensitivityV1::Unknown};
    std::string redaction_policy_version{std::string(kProviderAuditRedactionPolicyVersion)};
    std::string event_summary;
    bool secret_free{true};
    bool raw_telemetry_persisted{false};
};

struct ProviderTelemetrySummary {
    std::string format_version{std::string(kProviderTelemetrySummaryFormatVersion)};
    std::string source_durable_store_import_provider_execution_audit_event_format_version{
        std::string(kProviderExecutionAuditEventFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_execution_audit_event_identity;
    std::string durable_store_import_provider_telemetry_summary_identity;
    ProviderAuditOutcome outcome{ProviderAuditOutcome::Blocked};
    bool provider_write_attempted{true};
    bool provider_write_committed{false};
    bool retry_recommended{false};
    bool recovery_required{false};
    std::string telemetry_summary;
    bool secret_free{true};
    bool raw_telemetry_persisted{false};
};

struct ProviderOperatorReviewEvent {
    std::string format_version{std::string(kProviderOperatorReviewEventFormatVersion)};
    std::string source_durable_store_import_provider_telemetry_summary_format_version{
        std::string(kProviderTelemetrySummaryFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_telemetry_summary_identity;
    ProviderAuditOutcome outcome{ProviderAuditOutcome::Blocked};
    ProviderAuditNextActionKind next_action{ProviderAuditNextActionKind::OperatorReview};
    std::string operator_review_summary;
    std::string next_step_recommendation;
};

struct ProviderExecutionAuditEventValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderTelemetrySummaryValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderOperatorReviewEventValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderExecutionAuditEventResult {
    std::optional<ProviderExecutionAuditEvent> event;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderTelemetrySummaryResult {
    std::optional<ProviderTelemetrySummary> summary;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderOperatorReviewEventResult {
    std::optional<ProviderOperatorReviewEvent> review;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderExecutionAuditEventValidationResult
validate_provider_execution_audit_event(const ProviderExecutionAuditEvent &event);

[[nodiscard]] ProviderTelemetrySummaryValidationResult
validate_provider_telemetry_summary(const ProviderTelemetrySummary &summary);

[[nodiscard]] ProviderOperatorReviewEventValidationResult
validate_provider_operator_review_event(const ProviderOperatorReviewEvent &review);

[[nodiscard]] ProviderExecutionAuditEventResult
build_provider_execution_audit_event(const ProviderFailureTaxonomyReport &report);

[[nodiscard]] ProviderTelemetrySummaryResult
build_provider_telemetry_summary(const ProviderExecutionAuditEvent &event);

[[nodiscard]] ProviderOperatorReviewEventResult
build_provider_operator_review_event(const ProviderTelemetrySummary &summary);

} // namespace ahfl::durable_store_import
