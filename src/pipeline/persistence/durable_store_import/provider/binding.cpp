// Consolidated durable-store provider implementation domain.
// Public compatibility declarations remain in the provider_*.hpp headers.

// ---- provider_adapter.cpp ----

#include "pipeline/persistence/durable_store_import/provider/binding/adapter.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_adapter_detail_kValidationDiagnosticCode{"DSI_PROVIDER_ADAPTER"};

void provider_adapter_detail_emit_validation_error(DiagnosticBag &diagnostics,
                                                   std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_adapter_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string provider_adapter_detail_identity_anchor(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name) {
    return source_package_identity.has_value() ? source_package_identity->name
                                               : std::string(workflow_canonical_name);
}

[[nodiscard]] std::string
provider_adapter_detail_response_outcome_slug(PersistenceResponseOutcome outcome) {
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

[[nodiscard]] std::string provider_adapter_detail_provider_write_attempt_identity(
    std::string_view anchor, std::string_view session_id, PersistenceResponseOutcome outcome) {
    return "durable-store-import-provider-write-attempt::" + std::string(anchor) +
           "::" + std::string(session_id) +
           "::" + provider_adapter_detail_response_outcome_slug(outcome);
}

[[nodiscard]] std::string provider_adapter_detail_provider_persistence_id(
    std::string_view anchor, std::string_view session_id, PersistenceResponseOutcome outcome) {
    return "provider-persistence::" + std::string(anchor) + "::" + std::string(session_id) +
           "::" + provider_adapter_detail_response_outcome_slug(outcome);
}

[[nodiscard]] ProviderWriteIntentKind
provider_adapter_detail_noop_intent_kind(PersistenceResponseStatus status) {
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

[[nodiscard]] std::string
provider_adapter_detail_source_failure_message(const AdapterExecutionReceipt &receipt) {
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
provider_adapter_detail_next_action_for_write_attempt(const ProviderWriteAttemptPreview &preview) {
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

[[nodiscard]] std::string
provider_adapter_detail_boundary_summary(const ProviderWriteAttemptPreview &preview) {
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

[[nodiscard]] std::string
provider_adapter_detail_next_step_summary(const ProviderWriteAttemptPreview &preview) {
    switch (provider_adapter_detail_next_action_for_write_attempt(preview)) {
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

void provider_adapter_detail_validate_identity_fields(const ProviderWriteAttemptPreview &preview,
                                                      DiagnosticBag &diagnostics) {
    if (preview.workflow_canonical_name.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt workflow_canonical_name "
            "must not be empty");
    }
    if (preview.session_id.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt session_id must not be empty");
    }
    if (preview.input_fixture.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt input_fixture must not be empty");
    }
    if (preview.durable_store_import_receipt_persistence_response_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt "
            "durable_store_import_receipt_persistence_response_identity must not be empty");
    }
    if (preview.durable_store_import_adapter_execution_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt "
            "durable_store_import_adapter_execution_identity must not be empty");
    }
    if (preview.durable_store_import_provider_write_attempt_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt "
            "durable_store_import_provider_write_attempt_identity must not be empty");
    }
}

void provider_adapter_detail_validate_retry_resume_placeholder(
    const ProviderRetryResumePlaceholder &placeholder,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (placeholder.retry_placeholder_available &&
        (!placeholder.retry_placeholder_identity.has_value() ||
         placeholder.retry_placeholder_identity->empty())) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " retry placeholder requires retry_placeholder_identity");
    }
    if (!placeholder.retry_placeholder_available &&
        placeholder.retry_placeholder_identity.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " unavailable retry placeholder cannot contain identity");
    }
    if (placeholder.resume_placeholder_available &&
        (!placeholder.resume_placeholder_identity.has_value() ||
         placeholder.resume_placeholder_identity->empty())) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " resume placeholder requires resume_placeholder_identity");
    }
    if (!placeholder.resume_placeholder_available &&
        placeholder.resume_placeholder_identity.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            std::string(owner) + " unavailable resume placeholder cannot contain identity");
    }
}

} // namespace

ProviderAdapterConfigValidationResult
validate_provider_adapter_config(const ProviderAdapterConfig &config) {
    ProviderAdapterConfigValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (config.format_version != kProviderAdapterConfigFormatVersion) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config format_version must be '" +
                std::string(kProviderAdapterConfigFormatVersion) + "'");
    }
    if (config.config_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config config_identity "
            "must not be empty");
    }
    if (config.provider_profile_ref.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config provider_profile_ref must not be empty");
    }
    if (config.provider_namespace.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config provider_namespace must not be empty");
    }
    if (!config.credential_free) {
        provider_adapter_detail_emit_validation_error(
            diagnostics, "durable store import provider adapter config must be credential_free");
    }
    if (config.secret_material_reference.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config cannot contain "
            "secret_material_reference");
    }
    if (config.endpoint_secret_reference.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config cannot contain "
            "endpoint_secret_reference");
    }
    if (config.object_path.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider adapter config cannot contain "
            "provider object_path");
    }
    if (config.database_key.has_value()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
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
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider capability matrix format_version must be '" +
                std::string(kProviderCapabilityMatrixFormatVersion) + "'");
    }
    if (matrix.capability_matrix_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider capability matrix "
            "capability_matrix_identity must not be empty");
    }
    if (matrix.provider_config_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
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
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_adapter_execution_format_version !=
        kAdapterExecutionFormatVersion) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview "
            "source_durable_store_import_adapter_execution_format_version "
            "must be '" +
                std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_decision_receipt_persistence_response_format_version !=
        kPersistenceResponseFormatVersion) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview "
            "source_durable_store_import_decision_receipt_persistence_response_"
            "format_version must be '" +
                std::string(kPersistenceResponseFormatVersion) + "'");
    }

    provider_adapter_detail_validate_identity_fields(preview, diagnostics);
    result.diagnostics.append(
        validate_provider_adapter_config(preview.provider_config).diagnostics);
    result.diagnostics.append(
        validate_provider_capability_matrix(preview.capability_matrix).diagnostics);

    if (preview.capability_matrix.provider_config_identity !=
        preview.provider_config.config_identity) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview capability "
            "matrix provider_config_identity must match provider config");
    }
    if (preview.write_intent.source_adapter_execution_identity !=
        preview.durable_store_import_adapter_execution_identity) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview write_intent "
            "source_adapter_execution_identity must match source execution");
    }
    if (preview.write_intent.provider_namespace != preview.provider_config.provider_namespace) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview write_intent "
            "provider_namespace must match provider config");
    }
    if (preview.source_persistence_id != preview.write_intent.source_persistence_id) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview write_intent "
            "source_persistence_id must match source_persistence_id");
    }
    provider_adapter_detail_validate_retry_resume_placeholder(
        preview.retry_resume_placeholder,
        diagnostics,
        "durable store import provider write attempt preview");

    if (preview.failure_attribution.has_value() && preview.failure_attribution->message.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider write attempt preview "
            "failure_attribution message must not be empty");
    }

    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        if (preview.response_status != PersistenceResponseStatus::Accepted ||
            preview.adapter_mutation_status != StoreMutationStatus::Persisted) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview planned "
                "status requires accepted persisted adapter execution");
        }
        if (!preview.capability_matrix.supports_provider_write) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview planned "
                "status requires provider write capability");
        }
        if (preview.write_intent.kind != ProviderWriteIntentKind::ProviderPersistReceipt ||
            !preview.write_intent.mutates_provider) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview planned "
                "status requires mutating ProviderPersistReceipt write_intent");
        }
        if (!preview.source_persistence_id.has_value() ||
            !preview.write_intent.provider_persistence_id.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview planned "
                "status requires persistence ids");
        }
        if (preview.failure_attribution.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview planned "
                "status cannot contain failure_attribution");
        }
    } else {
        if (preview.write_intent.mutates_provider ||
            preview.write_intent.kind == ProviderWriteIntentKind::ProviderPersistReceipt) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview not-planned "
                "status requires non-mutating write_intent");
        }
        if (preview.write_intent.provider_persistence_id.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider write attempt preview not-planned "
                "status cannot contain provider_adapter_detail_provider_persistence_id");
        }
        if (!preview.failure_attribution.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
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
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview format_version must be '" +
                std::string(kProviderRecoveryHandoffPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (preview.source_durable_store_import_adapter_execution_format_version !=
        kAdapterExecutionFormatVersion) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview "
            "source_durable_store_import_adapter_execution_format_version must be '" +
                std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (preview.workflow_canonical_name.empty() || preview.session_id.empty() ||
        preview.input_fixture.empty() ||
        preview.durable_store_import_adapter_execution_identity.empty() ||
        preview.durable_store_import_provider_write_attempt_identity.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview identity "
            "fields must not be empty");
    }
    provider_adapter_detail_validate_retry_resume_placeholder(
        preview.retry_resume_placeholder,
        diagnostics,
        "durable store import provider recovery handoff preview");
    if (preview.recovery_handoff_boundary_summary.empty() ||
        preview.next_step_recommendation.empty()) {
        provider_adapter_detail_emit_validation_error(
            diagnostics,
            "durable store import provider recovery handoff preview summaries "
            "must not be empty");
    }

    if (preview.planning_status == ProviderWritePlanningStatus::Planned) {
        if (!preview.provider_persistence_id.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider recovery handoff preview planned "
                "status requires provider_adapter_detail_provider_persistence_id");
        }
        if (preview.failure_attribution.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider recovery handoff preview planned "
                "status cannot contain failure_attribution");
        }
    } else {
        if (preview.provider_persistence_id.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider recovery handoff preview "
                "not-planned status cannot contain "
                "provider_adapter_detail_provider_persistence_id");
        }
        if (!preview.failure_attribution.has_value()) {
            provider_adapter_detail_emit_validation_error(
                diagnostics,
                "durable store import provider recovery handoff preview "
                "not-planned status requires failure_attribution");
        }
    }

    return result;
}

ProviderAdapterConfig
build_default_provider_adapter_config(const AdapterExecutionReceipt &receipt) {
    const auto anchor = provider_adapter_detail_identity_anchor(receipt.source_package_identity,
                                                                receipt.workflow_canonical_name);
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

    const auto anchor = provider_adapter_detail_identity_anchor(receipt.source_package_identity,
                                                                receipt.workflow_canonical_name);
    const auto can_plan = receipt.response_status == PersistenceResponseStatus::Accepted &&
                          receipt.mutation_status == StoreMutationStatus::Persisted &&
                          receipt.persistence_id.has_value() && matrix.supports_provider_write;
    const auto outcome = receipt.response_outcome;

    ProviderPlanningFailureAttribution failure{
        .kind = matrix.supports_provider_write
                    ? ProviderPlanningFailureKind::SourceExecutionNotPersisted
                    : ProviderPlanningFailureKind::UnsupportedProviderCapability,
        .message = matrix.supports_provider_write
                       ? provider_adapter_detail_source_failure_message(receipt)
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
            provider_adapter_detail_provider_write_attempt_identity(
                anchor, receipt.session_id, outcome),
        .response_status = receipt.response_status,
        .response_outcome = receipt.response_outcome,
        .adapter_mutation_status = receipt.mutation_status,
        .source_persistence_id = receipt.persistence_id,
        .provider_config = config,
        .capability_matrix = matrix,
        .write_intent =
            ProviderWriteIntent{
                .kind = can_plan
                            ? ProviderWriteIntentKind::ProviderPersistReceipt
                            : provider_adapter_detail_noop_intent_kind(receipt.response_status),
                .source_adapter_execution_identity =
                    receipt.durable_store_import_adapter_execution_identity,
                .source_persistence_id = receipt.persistence_id,
                .provider_namespace = config.provider_namespace,
                .provider_persistence_id =
                    can_plan ? std::optional<std::string>(
                                   provider_adapter_detail_provider_persistence_id(
                                       anchor, receipt.session_id, outcome))
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
        .recovery_handoff_boundary_summary = provider_adapter_detail_boundary_summary(preview),
        .next_step_recommendation = provider_adapter_detail_next_step_summary(preview),
        .next_action = provider_adapter_detail_next_action_for_write_attempt(preview),
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

// ---- provider_driver.cpp ----

#include "pipeline/persistence/durable_store_import/provider/binding/driver.hpp"
#include "pipeline/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr ErrorCode<DiagnosticCategory::Validation> provider_driver_detail_kValidationDiagnosticCode{"DSI_PROVIDER_DRIVER"};

void provider_driver_detail_emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(
        diagnostics, provider_driver_detail_kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string
provider_driver_detail_planning_status_slug(ProviderWritePlanningStatus status) {
    switch (status) {
    case ProviderWritePlanningStatus::Planned:
        return "planned";
    case ProviderWritePlanningStatus::NotPlanned:
        return "not-planned";
    }

    return "invalid";
}

[[nodiscard]] std::string
provider_driver_detail_binding_identity(const ProviderWriteAttemptPreview &preview) {
    return "durable-store-import-provider-driver-binding::" + preview.session_id +
           "::" + provider_driver_detail_planning_status_slug(preview.planning_status);
}

[[nodiscard]] std::optional<std::string>
provider_driver_detail_operation_descriptor_identity(const ProviderWriteAttemptPreview &preview) {
    if (!preview.write_intent.provider_persistence_id.has_value()) {
        return std::nullopt;
    }

    return "provider-driver-operation::" + *preview.write_intent.provider_persistence_id;
}

[[nodiscard]] bool
provider_driver_detail_profile_matches_write_attempt(const ProviderWriteAttemptPreview &preview,
                                                     const ProviderDriverProfile &profile) {
    return profile.provider_profile_ref == preview.provider_config.provider_profile_ref &&
           profile.provider_namespace == preview.provider_config.provider_namespace;
}

[[nodiscard]] ProviderDriverBindingFailureAttribution
provider_driver_detail_source_not_planned_failure(const ProviderWriteAttemptPreview &preview) {
    std::string message = "provider driver binding cannot proceed because source provider write "
                          "attempt is not planned";
    if (preview.failure_attribution.has_value()) {
        message = preview.failure_attribution->message;
    }

    return ProviderDriverBindingFailureAttribution{
        .kind = ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned,
        .message = std::move(message),
        .missing_capability = std::nullopt,
    };
}

[[nodiscard]] ProviderDriverReadinessNextActionKind
provider_driver_detail_next_action_for_binding_plan(const ProviderDriverBindingPlan &plan) {
    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        return ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation;
    }

    if (plan.failure_attribution.has_value()) {
        switch (plan.failure_attribution->kind) {
        case ProviderDriverBindingFailureKind::DriverProfileMismatch:
            return ProviderDriverReadinessNextActionKind::FixProviderProfile;
        case ProviderDriverBindingFailureKind::UnsupportedDriverCapability:
            return ProviderDriverReadinessNextActionKind::WaitForDriverCapability;
        case ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned:
            return ProviderDriverReadinessNextActionKind::ManualReviewRequired;
        }
    }

    return ProviderDriverReadinessNextActionKind::ManualReviewRequired;
}

[[nodiscard]] std::string
provider_driver_detail_boundary_summary(const ProviderDriverBindingPlan &plan) {
    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        return "provider-specific driver binding is planned without invoking a provider SDK";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind == ProviderDriverBindingFailureKind::DriverProfileMismatch) {
        return "provider driver binding skipped because driver profile does not match provider "
               "adapter config";
    }

    if (plan.failure_attribution.has_value() &&
        plan.failure_attribution->kind ==
            ProviderDriverBindingFailureKind::UnsupportedDriverCapability) {
        return "provider driver binding skipped because driver capability is unsupported";
    }

    return "provider driver binding skipped because source provider write attempt is not planned";
}

[[nodiscard]] std::string
provider_driver_detail_next_step_summary(const ProviderDriverBindingPlan &plan) {
    switch (provider_driver_detail_next_action_for_binding_plan(plan)) {
    case ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation:
        return "ready for future provider driver implementation; no provider SDK was invoked";
    case ProviderDriverReadinessNextActionKind::FixProviderProfile:
        return "fix provider profile reference and namespace before binding a driver";
    case ProviderDriverReadinessNextActionKind::WaitForDriverCapability:
        return "wait for driver write intent translation capability before binding";
    case ProviderDriverReadinessNextActionKind::ManualReviewRequired:
        return "manual review required before provider driver binding can proceed";
    }

    return "manual review required before provider driver binding can proceed";
}

void provider_driver_detail_validate_failure_attribution(
    const std::optional<ProviderDriverBindingFailureAttribution> &failure,
    DiagnosticBag &diagnostics,
    std::string_view owner) {
    if (failure.has_value() && failure->message.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics, std::string(owner) + " failure_attribution message must not be empty");
    }
}

} // namespace

ProviderDriverProfileValidationResult
validate_provider_driver_profile(const ProviderDriverProfile &profile) {
    ProviderDriverProfileValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (profile.format_version != kProviderDriverProfileFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile format_version must be '" +
                std::string(kProviderDriverProfileFormatVersion) + "'");
    }
    if (profile.driver_profile_identity.empty()) {
        provider_driver_detail_emit_validation_error(diagnostics,
                                                     "durable store import provider driver profile "
                                                     "driver_profile_identity must not be empty");
    }
    if (profile.provider_profile_ref.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile provider_profile_ref "
            "must not be empty");
    }
    if (profile.provider_namespace.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile provider_namespace "
            "must not be empty");
    }
    if (profile.secret_free_config_ref.empty()) {
        provider_driver_detail_emit_validation_error(diagnostics,
                                                     "durable store import provider driver profile "
                                                     "secret_free_config_ref must not be empty");
    }
    if (!profile.credential_free) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile must be "
            "credential_free");
    }
    if (profile.credential_reference.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "credential_reference");
    }
    if (profile.endpoint_uri.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "endpoint_uri");
    }
    if (profile.endpoint_secret_reference.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "endpoint_secret_reference");
    }
    if (profile.object_path.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "object_path");
    }
    if (profile.database_table.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "database_table");
    }
    if (profile.sdk_payload_schema.has_value()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver profile cannot contain "
            "sdk_payload_schema");
    }

    return result;
}

ProviderDriverBindingPlanValidationResult
validate_provider_driver_binding_plan(const ProviderDriverBindingPlan &plan) {
    ProviderDriverBindingPlanValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (plan.format_version != kProviderDriverBindingPlanFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan format_version must be '" +
                std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_adapter_config_format_version !=
        kProviderAdapterConfigFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan "
            "source_durable_store_import_provider_adapter_config_format_version "
            "must be '" +
                std::string(kProviderAdapterConfigFormatVersion) + "'");
    }
    if (plan.source_durable_store_import_provider_capability_matrix_format_version !=
        kProviderCapabilityMatrixFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan "
            "source_durable_store_import_provider_capability_matrix_format_version must be '" +
                std::string(kProviderCapabilityMatrixFormatVersion) + "'");
    }
    if (plan.workflow_canonical_name.empty() || plan.session_id.empty() ||
        plan.input_fixture.empty() ||
        plan.durable_store_import_provider_write_attempt_identity.empty() ||
        plan.durable_store_import_provider_driver_binding_identity.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan identity fields "
            "must not be empty");
    }

    result.diagnostics.append(validate_provider_driver_profile(plan.driver_profile).diagnostics);
    provider_driver_detail_validate_failure_attribution(
        plan.failure_attribution, diagnostics, "durable store import provider driver binding plan");

    if (plan.invokes_provider_sdk) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver binding plan cannot invoke "
            "provider SDK");
    }

    if (plan.binding_status == ProviderDriverBindingStatus::Bound) {
        if (plan.source_planning_status != ProviderWritePlanningStatus::Planned ||
            !plan.provider_persistence_id.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan bound status "
                "requires planned source write and provider_persistence_id");
        }
        if (!plan.driver_profile.supports_secret_free_profile_load) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan bound status "
                "requires secret-free profile load capability");
        }
        if (!plan.driver_profile.supports_write_intent_translation) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan bound status "
                "requires write intent translation capability");
        }
        if (plan.operation_kind != ProviderDriverOperationKind::TranslateProviderPersistReceipt ||
            !plan.operation_descriptor_identity.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan bound status "
                "requires translation operation descriptor");
        }
        if (plan.failure_attribution.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan bound status "
                "cannot contain failure_attribution");
        }
    } else {
        if (plan.operation_kind == ProviderDriverOperationKind::TranslateProviderPersistReceipt) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan not-bound "
                "status cannot translate provider persist receipt");
        }
        if (plan.operation_descriptor_identity.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan not-bound "
                "status cannot contain provider_driver_detail_operation_descriptor_identity");
        }
        if (!plan.failure_attribution.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver binding plan not-bound "
                "status requires failure_attribution");
        }
    }

    return result;
}

ProviderDriverReadinessReviewValidationResult
validate_provider_driver_readiness_review(const ProviderDriverReadinessReview &review) {
    ProviderDriverReadinessReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kProviderDriverReadinessReviewFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review format_version must be '" +
                std::string(kProviderDriverReadinessReviewFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_driver_binding_plan_format_version !=
        kProviderDriverBindingPlanFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review "
            "source_durable_store_import_provider_driver_binding_plan_format_"
            "version must be '" +
                std::string(kProviderDriverBindingPlanFormatVersion) + "'");
    }
    if (review.source_durable_store_import_provider_write_attempt_preview_format_version !=
        kProviderWriteAttemptPreviewFormatVersion) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review "
            "source_durable_store_import_provider_write_attempt_preview_format_version must be '" +
                std::string(kProviderWriteAttemptPreviewFormatVersion) + "'");
    }
    if (review.workflow_canonical_name.empty() || review.session_id.empty() ||
        review.input_fixture.empty() ||
        review.durable_store_import_provider_write_attempt_identity.empty() ||
        review.durable_store_import_provider_driver_binding_identity.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review identity "
            "fields must not be empty");
    }
    if (review.invokes_provider_sdk) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review cannot "
            "invoke provider SDK");
    }
    if (review.driver_boundary_summary.empty() || review.next_step_recommendation.empty()) {
        provider_driver_detail_emit_validation_error(
            diagnostics,
            "durable store import provider driver readiness review summaries "
            "must not be empty");
    }
    provider_driver_detail_validate_failure_attribution(
        review.failure_attribution,
        diagnostics,
        "durable store import provider driver readiness review");

    if (review.binding_status == ProviderDriverBindingStatus::Bound) {
        if (review.operation_kind != ProviderDriverOperationKind::TranslateProviderPersistReceipt ||
            !review.operation_descriptor_identity.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver readiness review bound "
                "status requires translation operation descriptor");
        }
        if (review.failure_attribution.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver readiness review bound "
                "status cannot contain failure_attribution");
        }
    } else {
        if (review.operation_descriptor_identity.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver readiness review "
                "not-bound status cannot contain "
                "provider_driver_detail_operation_descriptor_identity");
        }
        if (!review.failure_attribution.has_value()) {
            provider_driver_detail_emit_validation_error(
                diagnostics,
                "durable store import provider driver readiness review "
                "not-bound status requires failure_attribution");
        }
    }

    return result;
}

ProviderDriverProfile
build_default_provider_driver_profile(const ProviderWriteAttemptPreview &preview) {
    return ProviderDriverProfile{
        .format_version = std::string(kProviderDriverProfileFormatVersion),
        .driver_kind = ProviderDriverKind::ProviderNeutralPreviewDriver,
        .driver_profile_identity =
            "provider-driver-profile::" + preview.provider_config.config_identity,
        .provider_profile_ref = preview.provider_config.provider_profile_ref,
        .provider_namespace = preview.provider_config.provider_namespace,
        .secret_free_config_ref =
            "secret-free-driver-config::" + preview.provider_config.config_identity,
        .credential_free = true,
        .supports_secret_free_profile_load = true,
        .supports_write_intent_translation = true,
        .supports_retry_placeholder_translation = true,
        .supports_resume_placeholder_translation = true,
        .credential_reference = std::nullopt,
        .endpoint_uri = std::nullopt,
        .endpoint_secret_reference = std::nullopt,
        .object_path = std::nullopt,
        .database_table = std::nullopt,
        .sdk_payload_schema = std::nullopt,
    };
}

ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview) {
    auto profile = build_default_provider_driver_profile(preview);
    return build_provider_driver_binding_plan(preview, profile);
}

ProviderDriverBindingPlanResult
build_provider_driver_binding_plan(const ProviderWriteAttemptPreview &preview,
                                   const ProviderDriverProfile &profile) {
    ProviderDriverBindingPlanResult result{
        .plan = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_write_attempt_preview(preview).diagnostics);
    result.diagnostics.append(validate_provider_driver_profile(profile).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto profile_matches =
        provider_driver_detail_profile_matches_write_attempt(preview, profile);
    const auto can_bind = preview.planning_status == ProviderWritePlanningStatus::Planned &&
                          preview.write_intent.provider_persistence_id.has_value() &&
                          profile_matches && profile.supports_secret_free_profile_load &&
                          profile.supports_write_intent_translation;

    ProviderDriverOperationKind operation_kind{ProviderDriverOperationKind::NoopSourceNotPlanned};
    std::optional<ProviderDriverBindingFailureAttribution> failure = std::nullopt;
    if (can_bind) {
        operation_kind = ProviderDriverOperationKind::TranslateProviderPersistReceipt;
    } else if (preview.planning_status != ProviderWritePlanningStatus::Planned) {
        operation_kind = ProviderDriverOperationKind::NoopSourceNotPlanned;
        failure = provider_driver_detail_source_not_planned_failure(preview);
    } else if (!profile_matches) {
        operation_kind = ProviderDriverOperationKind::NoopProfileMismatch;
        failure = ProviderDriverBindingFailureAttribution{
            .kind = ProviderDriverBindingFailureKind::DriverProfileMismatch,
            .message = "provider driver profile does not match provider adapter config",
            .missing_capability = std::nullopt,
        };
    } else {
        operation_kind = ProviderDriverOperationKind::NoopUnsupportedDriverCapability;
        auto missing_capability = ProviderDriverCapabilityKind::TranslateWriteIntent;
        std::string message = "provider driver does not support write intent translation";
        if (!profile.supports_secret_free_profile_load) {
            missing_capability = ProviderDriverCapabilityKind::LoadSecretFreeProfile;
            message = "provider driver does not support secret-free profile load";
        }
        failure = ProviderDriverBindingFailureAttribution{
            .kind = ProviderDriverBindingFailureKind::UnsupportedDriverCapability,
            .message = std::move(message),
            .missing_capability = missing_capability,
        };
    }

    ProviderDriverBindingPlan plan{
        .format_version = std::string(kProviderDriverBindingPlanFormatVersion),
        .source_durable_store_import_provider_write_attempt_preview_format_version =
            preview.format_version,
        .source_durable_store_import_provider_adapter_config_format_version =
            preview.provider_config.format_version,
        .source_durable_store_import_provider_capability_matrix_format_version =
            preview.capability_matrix.format_version,
        .workflow_canonical_name = preview.workflow_canonical_name,
        .session_id = preview.session_id,
        .run_id = preview.run_id,
        .input_fixture = preview.input_fixture,
        .durable_store_import_provider_write_attempt_identity =
            preview.durable_store_import_provider_write_attempt_identity,
        .durable_store_import_provider_driver_binding_identity =
            provider_driver_detail_binding_identity(preview),
        .source_planning_status = preview.planning_status,
        .provider_persistence_id = preview.write_intent.provider_persistence_id,
        .driver_profile = profile,
        .operation_kind = operation_kind,
        .binding_status =
            can_bind ? ProviderDriverBindingStatus::Bound : ProviderDriverBindingStatus::NotBound,
        .operation_descriptor_identity =
            can_bind ? provider_driver_detail_operation_descriptor_identity(preview) : std::nullopt,
        .invokes_provider_sdk = false,
        .failure_attribution = failure,
    };

    result.diagnostics.append(validate_provider_driver_binding_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.plan = std::move(plan);
    return result;
}

ProviderDriverReadinessReviewResult
build_provider_driver_readiness_review(const ProviderDriverBindingPlan &plan) {
    ProviderDriverReadinessReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    result.diagnostics.append(validate_provider_driver_binding_plan(plan).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    ProviderDriverReadinessReview review{
        .format_version = std::string(kProviderDriverReadinessReviewFormatVersion),
        .source_durable_store_import_provider_driver_binding_plan_format_version =
            plan.format_version,
        .source_durable_store_import_provider_write_attempt_preview_format_version =
            plan.source_durable_store_import_provider_write_attempt_preview_format_version,
        .workflow_canonical_name = plan.workflow_canonical_name,
        .session_id = plan.session_id,
        .run_id = plan.run_id,
        .input_fixture = plan.input_fixture,
        .durable_store_import_provider_write_attempt_identity =
            plan.durable_store_import_provider_write_attempt_identity,
        .durable_store_import_provider_driver_binding_identity =
            plan.durable_store_import_provider_driver_binding_identity,
        .binding_status = plan.binding_status,
        .operation_kind = plan.operation_kind,
        .operation_descriptor_identity = plan.operation_descriptor_identity,
        .invokes_provider_sdk = plan.invokes_provider_sdk,
        .failure_attribution = plan.failure_attribution,
        .driver_boundary_summary = provider_driver_detail_boundary_summary(plan),
        .next_step_recommendation = provider_driver_detail_next_step_summary(plan),
        .next_action = provider_driver_detail_next_action_for_binding_plan(plan),
    };

    result.diagnostics.append(validate_provider_driver_readiness_review(review).diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
