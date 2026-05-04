#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"

namespace ahfl::durable_store_import {

inline constexpr std::string_view kProviderLocalFilesystemAlphaPlanFormatVersion =
    "ahfl.durable-store-import-provider-local-filesystem-alpha-plan.v1";

inline constexpr std::string_view kProviderLocalFilesystemAlphaResultFormatVersion =
    "ahfl.durable-store-import-provider-local-filesystem-alpha-result.v1";

inline constexpr std::string_view kProviderLocalFilesystemAlphaReadinessFormatVersion =
    "ahfl.durable-store-import-provider-local-filesystem-alpha-readiness.v1";

enum class ProviderLocalFilesystemAlphaOperationKind {
    PlanLocalFilesystemAlpha,
    RunLocalFilesystemAlpha,
    NoopMockReadinessNotReady,
    NoopAlphaPlanNotReady,
};

enum class ProviderLocalFilesystemAlphaStatus {
    Ready,
    Blocked,
};

enum class ProviderLocalFilesystemAlphaResultKind {
    Accepted,
    DryRunOnly,
    WriteFailed,
    Blocked,
};

enum class ProviderLocalFilesystemAlphaFailureKind {
    MockReadinessNotReady,
    AlphaPlanNotReady,
    OptInRequired,
    FilesystemWriteFailed,
};

enum class ProviderLocalFilesystemAlphaNextActionKind {
    ReadyForIdempotencyContract,
    WaitForMockAdapter,
    ManualReviewRequired,
};

struct ProviderLocalFilesystemAlphaFailureAttribution {
    ProviderLocalFilesystemAlphaFailureKind kind{
        ProviderLocalFilesystemAlphaFailureKind::MockReadinessNotReady};
    std::string message;
};

struct ProviderLocalFilesystemAlphaPlan {
    std::string format_version{std::string(kProviderLocalFilesystemAlphaPlanFormatVersion)};
    std::string source_durable_store_import_provider_sdk_mock_adapter_readiness_format_version{
        std::string(kProviderSdkMockAdapterReadinessFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_sdk_mock_adapter_result_identity;
    ProviderSdkMockAdapterNextActionKind source_mock_adapter_next_action{
        ProviderSdkMockAdapterNextActionKind::ManualReviewRequired};
    std::string durable_store_import_provider_local_filesystem_alpha_plan_identity;
    ProviderLocalFilesystemAlphaOperationKind operation_kind{
        ProviderLocalFilesystemAlphaOperationKind::NoopMockReadinessNotReady};
    ProviderLocalFilesystemAlphaStatus plan_status{ProviderLocalFilesystemAlphaStatus::Blocked};
    std::string provider_key{"local-filesystem-alpha"};
    bool real_provider_alpha{true};
    bool fake_adapter_default_path_preserved{true};
    bool opt_in_required{true};
    bool opt_in_enabled{false};
    std::optional<std::string> target_directory;
    std::string target_object_name;
    std::string planned_payload_digest;
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool invokes_cloud_provider_sdk{false};
    std::optional<ProviderLocalFilesystemAlphaFailureAttribution> failure_attribution;
};

struct ProviderLocalFilesystemAlphaResult {
    std::string format_version{std::string(kProviderLocalFilesystemAlphaResultFormatVersion)};
    std::string source_durable_store_import_provider_local_filesystem_alpha_plan_format_version{
        std::string(kProviderLocalFilesystemAlphaPlanFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_local_filesystem_alpha_plan_identity;
    ProviderLocalFilesystemAlphaStatus source_alpha_plan_status{
        ProviderLocalFilesystemAlphaStatus::Blocked};
    std::string durable_store_import_provider_local_filesystem_alpha_result_identity;
    ProviderLocalFilesystemAlphaOperationKind operation_kind{
        ProviderLocalFilesystemAlphaOperationKind::NoopAlphaPlanNotReady};
    ProviderLocalFilesystemAlphaStatus result_status{ProviderLocalFilesystemAlphaStatus::Blocked};
    ProviderLocalFilesystemAlphaResultKind normalized_result{
        ProviderLocalFilesystemAlphaResultKind::Blocked};
    std::string provider_commit_reference;
    std::string provider_result_digest;
    bool wrote_local_file{false};
    bool opt_in_used{false};
    bool opens_network_connection{false};
    bool reads_secret_material{false};
    bool invokes_cloud_provider_sdk{false};
    std::optional<ProviderLocalFilesystemAlphaFailureAttribution> failure_attribution;
};

struct ProviderLocalFilesystemAlphaReadiness {
    std::string format_version{std::string(kProviderLocalFilesystemAlphaReadinessFormatVersion)};
    std::string source_durable_store_import_provider_local_filesystem_alpha_result_format_version{
        std::string(kProviderLocalFilesystemAlphaResultFormatVersion)};
    std::string workflow_canonical_name;
    std::string session_id;
    std::optional<std::string> run_id;
    std::string input_fixture;
    std::string durable_store_import_provider_local_filesystem_alpha_result_identity;
    ProviderLocalFilesystemAlphaStatus result_status{ProviderLocalFilesystemAlphaStatus::Blocked};
    ProviderLocalFilesystemAlphaResultKind normalized_result{
        ProviderLocalFilesystemAlphaResultKind::Blocked};
    std::string readiness_summary;
    std::string next_step_recommendation;
    ProviderLocalFilesystemAlphaNextActionKind next_action{
        ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired};
    std::optional<ProviderLocalFilesystemAlphaFailureAttribution> failure_attribution;
};

struct ProviderLocalFilesystemAlphaPlanValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalFilesystemAlphaResultValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalFilesystemAlphaReadinessValidationResult {
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalFilesystemAlphaPlanResult {
    std::optional<ProviderLocalFilesystemAlphaPlan> plan;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalFilesystemAlphaExecutionResult {
    std::optional<ProviderLocalFilesystemAlphaResult> result;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct ProviderLocalFilesystemAlphaReadinessResult {
    std::optional<ProviderLocalFilesystemAlphaReadiness> readiness;
    DiagnosticBag diagnostics;
    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] ProviderLocalFilesystemAlphaPlanValidationResult
validate_provider_local_filesystem_alpha_plan(const ProviderLocalFilesystemAlphaPlan &plan);

[[nodiscard]] ProviderLocalFilesystemAlphaResultValidationResult
validate_provider_local_filesystem_alpha_result(const ProviderLocalFilesystemAlphaResult &result);

[[nodiscard]] ProviderLocalFilesystemAlphaReadinessValidationResult
validate_provider_local_filesystem_alpha_readiness(
    const ProviderLocalFilesystemAlphaReadiness &readiness);

[[nodiscard]] ProviderLocalFilesystemAlphaPlanResult
build_provider_local_filesystem_alpha_plan(const ProviderSdkMockAdapterReadiness &readiness);

[[nodiscard]] ProviderLocalFilesystemAlphaExecutionResult
run_provider_local_filesystem_alpha(const ProviderLocalFilesystemAlphaPlan &plan);

[[nodiscard]] ProviderLocalFilesystemAlphaReadinessResult
build_provider_local_filesystem_alpha_readiness(const ProviderLocalFilesystemAlphaResult &result);

} // namespace ahfl::durable_store_import
