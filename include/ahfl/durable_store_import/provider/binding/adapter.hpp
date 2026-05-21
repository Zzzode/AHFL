#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/adapter_execution.hpp"

namespace ahfl::durable_store_import {

// Format versions - stable contracts, DO NOT CHANGE
inline constexpr std::string_view kProviderAdapterConfigFormatVersion =
    "ahfl.durable-store-import-provider-adapter-config.v1";

inline constexpr std::string_view kProviderCapabilityMatrixFormatVersion =
    "ahfl.durable-store-import-provider-capability-matrix.v1";

inline constexpr std::string_view kProviderWriteAttemptPreviewFormatVersion =
    "ahfl.durable-store-import-provider-write-attempt-preview.v1";

inline constexpr std::string_view kProviderRecoveryHandoffPreviewFormatVersion =
    "ahfl.durable-store-import-provider-recovery-handoff-preview.v1";

enum class ProviderAdapterKind {
    ProviderNeutralShim,
};

enum class ProviderWriteIntentKind {
    ProviderPersistReceipt,
    NoopBlocked,
    NoopDeferred,
    NoopRejected,
    NoopUnsupportedCapability,
};

enum class ProviderWritePlanningStatus {
    Planned,
    NotPlanned,
};

enum class ProviderCapabilityKind {
    PlanProviderWrite,
    PlanRetryPlaceholder,
    PlanResumePlaceholder,
    PlanRecoveryHandoff,
};

enum class ProviderPlanningFailureKind {
    SourceExecutionNotPersisted,
    UnsupportedProviderCapability,
};

enum class ProviderRecoveryHandoffNextActionKind {
    NoRecoveryRequired,
    RetryUnavailable,
    ResumeUnavailable,
    ManualReviewRequired,
};

struct ProviderAdapterConfig {
    std::string format_version{std::string(kProviderAdapterConfigFormatVersion)};
    ProviderAdapterKind adapter_kind{ProviderAdapterKind::ProviderNeutralShim};
    std::string config_identity;
    std::string provider_profile_ref;
    std::string provider_namespace;
    bool credential_free{true};
    std::optional<std::string> secret_material_reference;
    std::optional<std::string> endpoint_secret_reference;
    std::optional<std::string> object_path;
    std::optional<std::string> database_key;
};

struct ProviderCapabilityMatrix {
    std::string format_version{std::string(kProviderCapabilityMatrixFormatVersion)};
    std::string capability_matrix_identity;
    std::string provider_config_identity;
    bool supports_provider_write{true};
    bool supports_retry_placeholder{true};
    bool supports_resume_placeholder{true};
    bool supports_recovery_handoff{true};
};

struct ProviderWriteIntent {
    ProviderWriteIntentKind kind{ProviderWriteIntentKind::NoopBlocked};
    std::string source_adapter_execution_identity;
    std::optional<std::string> source_persistence_id;
    std::string provider_namespace;
    std::optional<std::string> provider_persistence_id;
    bool mutates_provider{false};
};

struct ProviderRetryResumePlaceholder {
    bool retry_placeholder_available{false};
    bool resume_placeholder_available{false};
    std::optional<std::string> retry_placeholder_identity;
    std::optional<std::string> resume_placeholder_identity;
};

struct ProviderPlanningFailureAttribution {
    ProviderPlanningFailureKind kind{ProviderPlanningFailureKind::SourceExecutionNotPersisted};
    std::string message;
    std::optional<ProviderCapabilityKind> missing_capability;
};

struct ProviderWriteAttemptPreview {
    std::string format_version{std::string(kProviderWriteAttemptPreviewFormatVersion)};
    std::string source_durable_store_import_adapter_execution_format_version{
        std::string(kAdapterExecutionFormatVersion)};
    std::string source_durable_store_import_decision_receipt_persistence_response_format_version{
        std::string(kPersistenceResponseFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_receipt_persistence_response_identity;
    std::string durable_store_import_adapter_execution_identity;
    std::string durable_store_import_provider_write_attempt_identity;
    PersistenceResponseStatus response_status{PersistenceResponseStatus::Blocked};
    PersistenceResponseOutcome response_outcome{PersistenceResponseOutcome::BlockBlockedRequest};
    StoreMutationStatus adapter_mutation_status{StoreMutationStatus::NotMutated};
    std::optional<std::string> source_persistence_id;
    ProviderAdapterConfig provider_config;
    ProviderCapabilityMatrix capability_matrix;
    ProviderWriteIntent write_intent;
    ProviderWritePlanningStatus planning_status{ProviderWritePlanningStatus::NotPlanned};
    ProviderRetryResumePlaceholder retry_resume_placeholder;
    std::optional<ProviderPlanningFailureAttribution> failure_attribution;
};

struct ProviderRecoveryHandoffPreview {
    std::string format_version{std::string(kProviderRecoveryHandoffPreviewFormatVersion)};
    std::string source_durable_store_import_provider_write_attempt_preview_format_version{
        std::string(kProviderWriteAttemptPreviewFormatVersion)};
    std::string source_durable_store_import_adapter_execution_format_version{
        std::string(kAdapterExecutionFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_adapter_execution_identity;
    std::string durable_store_import_provider_write_attempt_identity;
    ProviderWritePlanningStatus planning_status{ProviderWritePlanningStatus::NotPlanned};
    std::optional<std::string> provider_persistence_id;
    ProviderRetryResumePlaceholder retry_resume_placeholder;
    std::optional<ProviderPlanningFailureAttribution> failure_attribution;
    std::string recovery_handoff_boundary_summary;
    std::string next_step_recommendation;
    ProviderRecoveryHandoffNextActionKind next_action{
        ProviderRecoveryHandoffNextActionKind::ManualReviewRequired};
};

struct ProviderAdapterConfigValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderCapabilityMatrixValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteAttemptValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRecoveryHandoffValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderWriteAttemptResult {
    std::optional<ProviderWriteAttemptPreview> preview;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderRecoveryHandoffResult {
    std::optional<ProviderRecoveryHandoffPreview> preview;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderAdapterConfigValidationResult
validate_provider_adapter_config(const ProviderAdapterConfig &config);

[[nodiscard]] ProviderCapabilityMatrixValidationResult
validate_provider_capability_matrix(const ProviderCapabilityMatrix &matrix);

[[nodiscard]] ProviderWriteAttemptValidationResult
validate_provider_write_attempt_preview(const ProviderWriteAttemptPreview &preview);

[[nodiscard]] ProviderRecoveryHandoffValidationResult
validate_provider_recovery_handoff_preview(const ProviderRecoveryHandoffPreview &preview);

[[nodiscard]] ProviderAdapterConfig
build_default_provider_adapter_config(const AdapterExecutionReceipt &receipt);

[[nodiscard]] ProviderCapabilityMatrix
build_default_provider_capability_matrix(const ProviderAdapterConfig &config);

[[nodiscard]] ProviderWriteAttemptResult
build_provider_write_attempt_preview(const AdapterExecutionReceipt &receipt);

[[nodiscard]] ProviderWriteAttemptResult
build_provider_write_attempt_preview(const AdapterExecutionReceipt &receipt,
                                     const ProviderAdapterConfig &config,
                                     const ProviderCapabilityMatrix &matrix);

[[nodiscard]] ProviderRecoveryHandoffResult
build_provider_recovery_handoff_preview(const ProviderWriteAttemptPreview &preview);

} // namespace ahfl::durable_store_import
