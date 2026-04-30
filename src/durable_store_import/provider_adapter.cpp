#include "ahfl/durable_store_import/provider_adapter.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_PROVIDER_ADAPTER";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
identity_anchor(const std::optional<handoff::PackageIdentity> &source_package_identity,
                std::string_view workflow_canonical_name) {
    return source_package_identity.has_value() ? source_package_identity->name
                                               : std::string(workflow_canonical_name);
}

[[nodiscard]] std::string response_outcome_slug(PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accepted";
    case PersistenceResponseOutcome::BlockBlockedRequest:
        return "blocked";
    case PersistenceResponseOutcome::DeferDeferredRequest:
        return "deferred";
    case PersistenceResponseOutcome::RejectFailedRequest:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string provider_write_attempt_identity(std::string_view anchor,
                                                          std::string_view session_id,
                                                          PersistenceResponseOutcome outcome) {
    return "durable-store-import-provider-write-attempt::" + std::string(anchor) +
           "::" + std::string(session_id) + "::" + response_outcome_slug(outcome);
}

[[nodiscard]] std::string provider_persistence_id(std::string_view anchor,
                                                  std::string_view session_id,
                                                  PersistenceResponseOutcome outcome) {
    return "provider-persistence::" + std::string(anchor) + "::" + std::string(session_id) +
           "::" + response_outcome_slug(outcome);
}

[[nodiscard]] ProviderWriteIntentKind noop_intent_kind(PersistenceResponseStatus status) {
    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return ProviderWriteIntentKind::NoopUnsupportedCapability;
    case PersistenceResponseStatus::Blocked:
        return ProviderWriteIntentKind::NoopBlocked;
    case PersistenceResponseStatus::Deferred:
        return ProviderWriteIntentKind::NoopDeferred;
    case PersistenceResponseStatus::Rejected:
        return ProviderWriteIntentKind::NoopRejected;
    }

    return ProviderWriteIntentKind::NoopBlocked;
}

[[nodiscard]] std::string source_failure_message(const AdapterExecutionReceipt &receipt) {
    if (receipt.failure_attribution.has_value()) {
        return receipt.failure_attribution->message;
    }

    switch (receipt.response_status) {
    case PersistenceResponseStatus::Accepted:
        return "provider write cannot be planned because adapter execution is not persisted";
    case PersistenceResponseStatus::Blocked:
        return "provider write cannot be planned because source adapter execution is blocked";
    case PersistenceResponseStatus::Deferred:
        return "provider write cannot be planned because source adapter execution is deferred";
    case PersistenceResponseStatus::Rejected:
        return "provider write cannot be planned because source adapter execution is rejected";
    }

    return "provider write cannot be planned because source adapter execution is unavailable";
}

[[nodiscard]] ProviderRecoveryHandoffNextActionKind
next_action_for_write_attempt(const ProviderWriteAttemptPreview &preview) {
    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        return ProviderRecoveryHandoffNextActionKind::NoRecoveryRequired;
    }

    if (preview.failure_attribution.has_value() &&
        preview.failure_attribution->kind ==
            ProviderPlanningFailureKind::UnsupportedProviderCapability) {
        if (!preview.retry_resume_placeholder.retry_placeholder_available) {
            return ProviderRecoveryHandoffNextActionKind::RetryUnavailable;
        }
        return ProviderRecoveryHandoffNextActionKind::ResumeUnavailable;
    }

    return ProviderRecoveryHandoffNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string boundary_summary(const ProviderWriteAttemptPreview &preview) {
    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        return "provider-neutral write attempt planned without executing a real provider write";
    }

    if (preview.failure_attribution.has_value() &&
        preview.failure_attribution->kind ==
            ProviderPlanningFailureKind::UnsupportedProviderCapability) {
        return "provider-neutral write attempt skipped because provider capability is unsupported";
    }

    return "provider-neutral write attempt skipped because source adapter execution is not "
           "writable";
}

[[nodiscard]] std::string next_step_summary(const ProviderWriteAttemptPreview &preview) {
    switch (next_action_for_write_attempt(preview)) {
    case ProviderRecoveryHandoffNextActionKind::NoRecoveryRequired:
        return "no recovery required; provider-neutral persistence id is available for future "
               "provider driver handoff";
    case ProviderRecoveryHandoffNextActionKind::RetryUnavailable:
        return "retry is unavailable until provider write capability is declared";
    case ProviderRecoveryHandoffNextActionKind::ResumeUnavailable:
        return "resume is unavailable until provider resume capability is declared";
    case ProviderRecoveryHandoffNextActionKind::ManualReviewRequired:
        return "manual review required before provider recovery handoff can proceed";
    }

    return "manual review required before provider recovery handoff can proceed";
}

void validate_identity_fields(const ProviderWriteAttemptPreview &preview,
                              DiagnosticBag &diagnostics) {
    if (preview.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt workflow_canonical_name "
                              "must not be empty");
    }
    if (preview.session_id.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider write attempt session_id must not be empty");
    }
    if (preview.input_fixture.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider write attempt input_fixture must not be empty");
    }
    if (preview.durable_store_import_receipt_persistence_response_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider write attempt "
            "durable_store_import_receipt_persistence_response_identity must not be empty");
    }
    if (preview.durable_store_import_adapter_execution_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt "
                              "durable_store_import_adapter_execution_identity must not be empty");
    }
    if (preview.durable_store_import_provider_write_attempt_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider write attempt "
            "durable_store_import_provider_write_attempt_identity must not be empty");
    }
}

void validate_retry_resume_placeholder(const ProviderRetryResumePlaceholder &placeholder,
                                       DiagnosticBag &diagnostics,
                                       std::string_view owner) {
    if (placeholder.retry_placeholder_available &&
        (!placeholder.retry_placeholder_identity.has_value() ||
         placeholder.retry_placeholder_identity->empty())) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " retry placeholder requires retry_placeholder_identity");
    }
    if (!placeholder.retry_placeholder_available &&
        placeholder.retry_placeholder_identity.has_value()) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " unavailable retry placeholder cannot contain identity");
    }
    if (placeholder.resume_placeholder_available &&
        (!placeholder.resume_placeholder_identity.has_value() ||
         placeholder.resume_placeholder_identity->empty())) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " resume placeholder requires resume_placeholder_identity");
    }
    if (!placeholder.resume_placeholder_available &&
        placeholder.resume_placeholder_identity.has_value()) {
        emit_validation_error(diagnostics,
                              std::string(owner) +
                                  " unavailable resume placeholder cannot contain identity");
    }
}

} // namespace

ProviderAdapterConfigValidationResult
validate_provider_adapter_config(const ProviderAdapterConfig &config) {
    ProviderAdapterConfigValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (config.format_version != kProviderAdapterConfigFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider adapter config format_version must be '" +
                std::string(kProviderAdapterConfigFormatVersion) + "'");
    }
    if (config.config_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider adapter config config_identity "
                              "must not be empty");
    }
    if (config.provider_profile_ref.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider adapter config provider_profile_ref must not be empty");
    }
    if (config.provider_namespace.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import provider adapter config provider_namespace must not be empty");
    }
    if (!config.credential_free) {
        emit_validation_error(
            diagnostics, "durable store import provider adapter config must be credential_free");
    }
    if (config.secret_material_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider adapter config cannot contain "
                              "secret_material_reference");
    }
    if (config.endpoint_secret_reference.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider adapter config cannot contain "
                              "endpoint_secret_reference");
    }
    if (config.object_path.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider adapter config cannot contain "
                              "provider object_path");
    }
    if (config.database_key.has_value()) {
        emit_validation_error(diagnostics,
                              "durable store import provider adapter config cannot contain "
                              "provider database_key");
    }

    return result;
}

ProviderCapabilityMatrixValidationResult
validate_provider_capability_matrix(const ProviderCapabilityMatrix &matrix) {
    ProviderCapabilityMatrixValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (matrix.format_version != kProviderCapabilityMatrixFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider capability matrix format_version must be '" +
                std::string(kProviderCapabilityMatrixFormatVersion) + "'");
    }
    if (matrix.capability_matrix_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider capability matrix "
                              "capability_matrix_identity must not be empty");
    }
    if (matrix.provider_config_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider capability matrix "
                              "provider_config_identity must not be empty");
    }

    return result;
}

ProviderWriteAttemptValidationResult
validate_provider_write_attempt_preview(const ProviderWriteAttemptPreview &preview) {
    ProviderWriteAttemptValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (preview.format_version != kProviderWriteAttemptPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_adapter_execution_format_version !=
        kAdapterExecutionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview "
                              "source_durable_store_import_adapter_execution_format_version "
                              "must be '" +
                                  std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_decision_receipt_persistence_response_format_version !=
        kPersistenceResponseFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview "
                              "source_durable_store_import_decision_receipt_persistence_response_"
                              "format_version must be '" +
                                  std::string(kPersistenceResponseFormatVersion) + "'");
    }

    validate_identity_fields(preview, diagnostics);
    result.diagnostics.append(
        validate_provider_adapter_config(preview.provider_config).diagnostics);
    result.diagnostics.append(
        validate_provider_capability_matrix(preview.capability_matrix).diagnostics);

    if (preview.capability_matrix.provider_config_identity !=
        preview.provider_config.config_identity) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview capability "
                              "matrix provider_config_identity must match provider config");
    }
    if (preview.write_intent.source_adapter_execution_identity !=
        preview.durable_store_import_adapter_execution_identity) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview write_intent "
                              "source_adapter_execution_identity must match source execution");
    }
    if (preview.write_intent.provider_namespace != preview.provider_config.provider_namespace) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview write_intent "
                              "provider_namespace must match provider config");
    }
    if (preview.source_persistence_id != preview.write_intent.source_persistence_id) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview write_intent "
                              "source_persistence_id must match source_persistence_id");
    }
    validate_retry_resume_placeholder(preview.retry_resume_placeholder,
                                      diagnostics,
                                      "durable store import provider write attempt preview");

    if (preview.failure_attribution.has_value() && preview.failure_attribution->message.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider write attempt preview "
                              "failure_attribution message must not be empty");
    }

    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        if (preview.response_status != PersistenceResponseStatus::Accepted ||
            preview.adapter_mutation_status != StoreMutationStatus::Persisted) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview planned "
                                  "status requires accepted persisted adapter execution");
        }
        if (!preview.capability_matrix.supports_provider_write) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview planned "
                                  "status requires provider write capability");
        }
        if (preview.write_intent.kind != ProviderWriteIntentKind::ProviderPersistReceipt ||
            !preview.write_intent.mutates_provider) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview planned "
                                  "status requires mutating ProviderPersistReceipt write_intent");
        }
        if (!preview.source_persistence_id.has_value() ||
            !preview.write_intent.provider_persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview planned "
                                  "status requires persistence ids");
        }
        if (preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview planned "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (preview.write_intent.mutates_provider ||
            preview.write_intent.kind == ProviderWriteIntentKind::ProviderPersistReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview not-planned "
                                  "status requires non-mutating write_intent");
        }
        if (preview.write_intent.provider_persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview not-planned "
                                  "status cannot contain provider_persistence_id");
        }
        if (!preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider write attempt preview not-planned "
                                  "status requires failure_attribution");
        }
    }

    return result;
}

ProviderRecoveryHandoffValidationResult
validate_provider_recovery_handoff_preview(const ProviderRecoveryHandoffPreview &preview) {
    ProviderRecoveryHandoffValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (preview.format_version != kProviderRecoveryHandoffPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview format_version must be '" +
                std::string(kProviderRecoveryHandoffPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_adapter_execution_format_version !=
        kAdapterExecutionFormatVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview "
            "source_durable_store_import_adapter_execution_format_version must be '" +
                std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (preview.workflow_canonical_name.empty() || preview.session_id.empty() ||
        preview.input_fixture.empty() ||
        preview.durable_store_import_adapter_execution_identity.empty() ||
        preview.durable_store_import_provider_write_attempt_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider recovery handoff preview identity "
                              "fields must not be empty");
    }
    validate_retry_resume_placeholder(preview.retry_resume_placeholder,
                                      diagnostics,
                                      "durable store import provider recovery handoff preview");
    if (preview.recovery_handoff_boundary_summary.empty() ||
        preview.next_step_recommendation.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import provider recovery handoff preview summaries "
                              "must not be empty");
    }

    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        if (!preview.provider_persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider recovery handoff preview planned "
                                  "status requires provider_persistence_id");
        }
        if (preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider recovery handoff preview planned "
                                  "status cannot contain failure_attribution");
        }
    } else {
        if (preview.provider_persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider recovery handoff preview "
                                  "not-planned status cannot contain provider_persistence_id");
        }
        if (!preview.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import provider recovery handoff preview "
                                  "not-planned status requires failure_attribution");
        }
    }

    return result;
}

ProviderAdapterConfig
build_default_provider_adapter_config(const AdapterExecutionReceipt &receipt) {
    const auto anchor =
        identity_anchor(receipt.source_package_identity, receipt.workflow_canonical_name);
    return ProviderAdapterConfig{
        .format_version = std::string(kProviderAdapterConfigFormatVersion),
        .adapter_kind = ProviderAdapterKind::ProviderNeutralShim,
        .config_identity =
            "provider-adapter-config::" + anchor + "::" + receipt.session_id + "::provider-neutral",
        .provider_profile_ref = "provider-neutral-default",
        .provider_namespace = "provider-neutral::" + anchor,
        .credential_free = true,
        .secret_material_reference = std::nullopt,
        .endpoint_secret_reference = std::nullopt,
        .object_path = std::nullopt,
        .database_key = std::nullopt,
    };
}

ProviderCapabilityMatrix
build_default_provider_capability_matrix(const ProviderAdapterConfig &config) {
    return ProviderCapabilityMatrix{
        .format_version = std::string(kProviderCapabilityMatrixFormatVersion),
        .capability_matrix_identity = "provider-capability-matrix::" + config.config_identity,
        .provider_config_identity = config.config_identity,
        .supports_provider_write = true,
        .supports_retry_placeholder = true,
        .supports_resume_placeholder = true,
        .supports_recovery_handoff = true,
    };
}

ProviderWriteAttemptResult
build_provider_write_attempt_preview(const AdapterExecutionReceipt &receipt) {
    auto config = build_default_provider_adapter_config(receipt);
    auto matrix = build_default_provider_capability_matrix(config);
    return build_provider_write_attempt_preview(receipt, config, matrix);
}

ProviderWriteAttemptResult
build_provider_write_attempt_preview(const AdapterExecutionReceipt &receipt,
                                     const ProviderAdapterConfig &config,
                                     const ProviderCapabilityMatrix &matrix) {
    ProviderWriteAttemptResult result{
        .preview = std::nullopt,
        .diagnostics = {},
    };

    const auto execution_validation = validate_adapter_execution_receipt(receipt);
    result.diagnostics.append(execution_validation.diagnostics);
    result.diagnostics.append(validate_provider_adapter_config(config).diagnostics);
    result.diagnostics.append(validate_provider_capability_matrix(matrix).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto anchor =
        identity_anchor(receipt.source_package_identity, receipt.workflow_canonical_name);
    const auto can_plan = receipt.response_status == PersistenceResponseStatus::Accepted &&
                          receipt.mutation_status == StoreMutationStatus::Persisted &&
                          receipt.persistence_id.has_value() && matrix.supports_provider_write;
    const auto outcome = receipt.response_outcome;

    ProviderPlanningFailureAttribution failure{
        .kind = matrix.supports_provider_write
                    ? ProviderPlanningFailureKind::SourceExecutionNotPersisted
                    : ProviderPlanningFailureKind::UnsupportedProviderCapability,
        .message = matrix.supports_provider_write ? source_failure_message(receipt)
                                                  : "provider-neutral adapter capability matrix "
                                                    "does not support provider write planning",
        .missing_capability =
            matrix.supports_provider_write
                ? std::nullopt
                : std::optional<ProviderCapabilityKind>(ProviderCapabilityKind::PlanProviderWrite),
    };

    ProviderWriteAttemptPreview preview{
        .format_version = std::string(kProviderWriteAttemptPreviewFormatVersion),
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
        .durable_store_import_provider_write_attempt_identity =
            provider_write_attempt_identity(anchor, receipt.session_id, outcome),
        .response_status = receipt.response_status,
        .response_outcome = receipt.response_outcome,
        .adapter_mutation_status = receipt.mutation_status,
        .source_persistence_id = receipt.persistence_id,
        .provider_config = config,
        .capability_matrix = matrix,
        .write_intent =
            ProviderWriteIntent{
                .kind = can_plan ? ProviderWriteIntentKind::ProviderPersistReceipt
                                 : noop_intent_kind(receipt.response_status),
                .source_adapter_execution_identity =
                    receipt.durable_store_import_adapter_execution_identity,
                .source_persistence_id = receipt.persistence_id,
                .provider_namespace = config.provider_namespace,
                .provider_persistence_id =
                    can_plan ? std::optional<std::string>(
                                   provider_persistence_id(anchor, receipt.session_id, outcome))
                             : std::nullopt,
                .mutates_provider = can_plan,
            },
        .planning_status = can_plan ? ProviderWritePlanningStatus::Planned
                                    : ProviderWritePlanningStatus::NotPlanned,
        .retry_resume_placeholder =
            ProviderRetryResumePlaceholder{
                .retry_placeholder_available = can_plan && matrix.supports_retry_placeholder,
                .resume_placeholder_available = can_plan && matrix.supports_resume_placeholder,
                .retry_placeholder_identity =
                    can_plan && matrix.supports_retry_placeholder
                        ? std::optional<std::string>("retry-placeholder::" + anchor +
                                                     "::" + receipt.session_id + "::provider-write")
                        : std::nullopt,
                .resume_placeholder_identity =
                    can_plan && matrix.supports_resume_placeholder
                        ? std::optional<std::string>("resume-placeholder::" + anchor +
                                                     "::" + receipt.session_id + "::provider-write")
                        : std::nullopt,
            },
        .failure_attribution =
            can_plan ? std::nullopt : std::optional<ProviderPlanningFailureAttribution>(failure),
    };

    const auto validation = validate_provider_write_attempt_preview(preview);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.preview = std::move(preview);
    return result;
}

ProviderRecoveryHandoffResult
build_provider_recovery_handoff_preview(const ProviderWriteAttemptPreview &preview) {
    ProviderRecoveryHandoffResult result{
        .preview = std::nullopt,
        .diagnostics = {},
    };

    const auto write_validation = validate_provider_write_attempt_preview(preview);
    result.diagnostics.append(write_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderRecoveryHandoffPreview handoff{
        .format_version = std::string(kProviderRecoveryHandoffPreviewFormatVersion),
        .source_durable_store_import_provider_write_attempt_preview_format_version =
            preview.format_version,
        .source_durable_store_import_adapter_execution_format_version =
            preview.source_durable_store_import_adapter_execution_format_version,
        .workflow_canonical_name = preview.workflow_canonical_name,
        .session_id = preview.session_id,
        .run_id = preview.run_id,
        .input_fixture = preview.input_fixture,
        .durable_store_import_adapter_execution_identity =
            preview.durable_store_import_adapter_execution_identity,
        .durable_store_import_provider_write_attempt_identity =
            preview.durable_store_import_provider_write_attempt_identity,
        .planning_status = preview.planning_status,
        .provider_persistence_id = preview.write_intent.provider_persistence_id,
        .retry_resume_placeholder = preview.retry_resume_placeholder,
        .failure_attribution = preview.failure_attribution,
        .recovery_handoff_boundary_summary = boundary_summary(preview),
        .next_step_recommendation = next_step_summary(preview),
        .next_action = next_action_for_write_attempt(preview),
    };

    const auto validation = validate_provider_recovery_handoff_preview(handoff);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.preview = std::move(handoff);
    return result;
}

} // namespace ahfl::durable_store_import
