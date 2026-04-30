#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ahfl/durable_store_import/adapter_execution.hpp"

namespace ahfl::durable_store_import {

// Format version - stable contract, DO NOT CHANGE
inline constexpr std::string_view kRecoveryCommandPreviewFormatVersion =
    "ahfl.durable-store-import-recovery-command-preview.v1";

enum class RecoveryPreviewNextActionKind {
    NoRecoveryRequired,
    ResolveBlocker,
    WaitForCapability,
    ReviewFailure,
};

struct RecoveryPreviewExecutionSummary {
    std::string adapter_execution_identity;
    StoreMutationStatus mutation_status{StoreMutationStatus::NotMutated};
    std::optional<std::string> persistence_id;
};

struct RecoveryCommandPreview {
    std::string format_version{std::string(kRecoveryCommandPreviewFormatVersion)};
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
    PersistenceResponseStatus response_status{PersistenceResponseStatus::Blocked};
    PersistenceResponseOutcome response_outcome{PersistenceResponseOutcome::BlockBlockedRequest};
    StoreMutationStatus mutation_status{StoreMutationStatus::NotMutated};
    std::optional<std::string> persistence_id;
    std::optional<AdapterExecutionFailureAttribution> failure_attribution;
    RecoveryPreviewExecutionSummary execution_summary;
    std::string recovery_boundary_summary;
    std::string next_step_recommendation;
    RecoveryPreviewNextActionKind next_action{RecoveryPreviewNextActionKind::ResolveBlocker};
};

struct RecoveryPreviewValidationResult {
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

struct RecoveryPreviewResult {
    std::optional<RecoveryCommandPreview> preview;
    DiagnosticBag diagnostics;

    [[nodiscard]] bool has_errors() const noexcept {
        return diagnostics.has_error();
    }
};

[[nodiscard]] RecoveryPreviewValidationResult
validate_recovery_command_preview(const RecoveryCommandPreview &preview);

[[nodiscard]] RecoveryPreviewResult
build_recovery_command_preview(const AdapterExecutionReceipt &receipt);

} // namespace ahfl::durable_store_import
