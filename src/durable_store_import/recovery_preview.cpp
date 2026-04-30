#include "ahfl/durable_store_import/recovery_preview.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_RECOVERY_PREVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] RecoveryPreviewNextActionKind to_next_action(PersistenceResponseStatus status,
                                                           StoreMutationStatus mutation_status) {
    if (status == PersistenceResponseStatus::Accepted &&
        mutation_status == StoreMutationStatus::Persisted) {
        return RecoveryPreviewNextActionKind::NoRecoveryRequired;
    }

    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return RecoveryPreviewNextActionKind::ReviewFailure;
    case PersistenceResponseStatus::Blocked:
        return RecoveryPreviewNextActionKind::ResolveBlocker;
    case PersistenceResponseStatus::Deferred:
        return RecoveryPreviewNextActionKind::WaitForCapability;
    case PersistenceResponseStatus::Rejected:
        return RecoveryPreviewNextActionKind::ReviewFailure;
    }

    return RecoveryPreviewNextActionKind::ResolveBlocker;
}

[[nodiscard]] std::string boundary_summary(const AdapterExecutionReceipt &receipt) {
    switch (receipt.response_status) {
    case PersistenceResponseStatus::Accepted:
        return "fake durable store execution persisted adapter handoff";
    case PersistenceResponseStatus::Blocked:
        return "fake durable store execution skipped because response is blocked";
    case PersistenceResponseStatus::Deferred:
        return "fake durable store execution skipped until required capability is available";
    case PersistenceResponseStatus::Rejected:
        return "fake durable store execution skipped because source workflow failed";
    }

    return "fake durable store execution skipped";
}

[[nodiscard]] std::string next_step_summary(const AdapterExecutionReceipt &receipt) {
    switch (to_next_action(receipt.response_status, receipt.mutation_status)) {
    case RecoveryPreviewNextActionKind::NoRecoveryRequired:
        return "no recovery required; persistence id is available for future adapter handoff";
    case RecoveryPreviewNextActionKind::ResolveBlocker:
        return "resolve source response blocker before retrying adapter execution";
    case RecoveryPreviewNextActionKind::WaitForCapability:
        return "wait for required adapter capability before retrying adapter execution";
    case RecoveryPreviewNextActionKind::ReviewFailure:
        return "review failure attribution before planning recovery";
    }

    return "review adapter execution before planning recovery";
}

} // namespace

RecoveryPreviewValidationResult
validate_recovery_command_preview(const RecoveryCommandPreview &preview) {
    RecoveryPreviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (preview.format_version != kRecoveryCommandPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import recovery command preview format_version must be '" +
                std::string(kRecoveryCommandPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_adapter_execution_format_version !=
        kAdapterExecutionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "source_durable_store_import_adapter_execution_format_version "
                              "must be '" +
                                  std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_decision_receipt_persistence_response_format_version !=
        kPersistenceResponseFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "source_durable_store_import_decision_receipt_persistence_response_"
                              "format_version must be '" +
                                  std::string(kPersistenceResponseFormatVersion) + "'");
    }
    if (preview.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "workflow_canonical_name must not be empty");
    }
    if (preview.session_id.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview session_id "
                              "must not be empty");
    }
    if (preview.input_fixture.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview input_fixture "
                              "must not be empty");
    }
    if (preview.durable_store_import_receipt_persistence_response_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "durable_store_import_receipt_persistence_response_identity "
                              "must not be empty");
    }
    if (preview.durable_store_import_adapter_execution_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "durable_store_import_adapter_execution_identity must not be empty");
    }
    if (preview.execution_summary.adapter_execution_identity !=
        preview.durable_store_import_adapter_execution_identity) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview execution_summary "
                              "adapter_execution_identity must match source execution identity");
    }
    if (preview.recovery_boundary_summary.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "recovery_boundary_summary must not be empty");
    }
    if (preview.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import recovery command preview "
                              "next_step_recommendation must not be empty");
    }

    if (preview.response_status == PersistenceResponseStatus::Accepted) {
        if (preview.mutation_status != StoreMutationStatus::Persisted) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview Accepted status "
                                  "requires Persisted mutation_status");
        }
        if (!preview.persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview Accepted status "
                                  "requires persistence_id");
        }
        if (preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview Accepted status "
                                  "cannot contain failure_attribution");
        }
    } else {
        if (preview.mutation_status != StoreMutationStatus::NotMutated) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview non-accepted "
                                  "status requires NotMutated mutation_status");
        }
        if (preview.persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview non-accepted "
                                  "status cannot contain persistence_id");
        }
        if (!preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import recovery command preview non-accepted "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

RecoveryPreviewResult build_recovery_command_preview(const AdapterExecutionReceipt &receipt) {
    RecoveryPreviewResult result{
        .preview = std::nullopt,
        .diagnostics = {},
    };

    const auto execution_validation = validate_adapter_execution_receipt(receipt);
    result.diagnostics.append(execution_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    RecoveryCommandPreview preview{
        .format_version = std::string(kRecoveryCommandPreviewFormatVersion),
        .source_durable_store_import_adapter_execution_format_version = receipt.format_version,
        .source_durable_store_import_decision_receipt_persistence_response_format_version =
            receipt
                .source_durable_store_import_decision_receipt_persistence_response_format_version,
        .workflow_canonical_name = receipt.workflow_canonical_name,
        .session_id = receipt.session_id,
        .run_id = receipt.run_id,
        .input_fixture = receipt.input_fixture,
        .durable_store_import_receipt_persistence_response_identity =
            receipt.durable_store_import_receipt_persistence_response_identity,
        .durable_store_import_adapter_execution_identity =
            receipt.durable_store_import_adapter_execution_identity,
        .response_status = receipt.response_status,
        .response_outcome = receipt.response_outcome,
        .mutation_status = receipt.mutation_status,
        .persistence_id = receipt.persistence_id,
        .failure_attribution = receipt.failure_attribution,
        .execution_summary =
            RecoveryPreviewExecutionSummary{
                .adapter_execution_identity =
                    receipt.durable_store_import_adapter_execution_identity,
                .mutation_status = receipt.mutation_status,
                .persistence_id = receipt.persistence_id,
            },
        .recovery_boundary_summary = boundary_summary(receipt),
        .next_step_recommendation = next_step_summary(receipt),
        .next_action = to_next_action(receipt.response_status, receipt.mutation_status),
    };

    const auto validation = validate_recovery_command_preview(preview);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.preview = std::move(preview);
    return result;
}

} // namespace ahfl::durable_store_import
