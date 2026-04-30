#include "ahfl/durable_store_import/adapter_execution.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {
namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_ADAPTER_EXECUTION";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

void validate_package_identity(const handoff::PackageIdentity &identity,
                               DiagnosticBag &diagnostics) {
    validation::validate_package_identity(
        identity, diagnostics, "durable store import adapter execution");
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

[[nodiscard]] std::string build_adapter_execution_identity(
    const std::optional<handoff::PackageIdentity> &source_package_identity,
    std::string_view workflow_canonical_name,
    std::string_view session_id,
    PersistenceResponseOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "durable-store-import-adapter-execution::" + identity_anchor +
           "::" + std::string(session_id) + "::" + response_outcome_slug(outcome);
}

[[nodiscard]] std::string
build_fake_persistence_id(const std::optional<handoff::PackageIdentity> &source_package_identity,
                          std::string_view workflow_canonical_name,
                          std::string_view session_id,
                          PersistenceResponseOutcome outcome) {
    const auto &identity_anchor = source_package_identity.has_value()
                                      ? source_package_identity->name
                                      : std::string(workflow_canonical_name);
    return "fake-persistence::" + identity_anchor + "::" + std::string(session_id) +
           "::" + response_outcome_slug(outcome);
}

[[nodiscard]] StoreMutationIntentKind to_mutation_intent_kind(PersistenceResponseStatus status) {
    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return StoreMutationIntentKind::PersistReceipt;
    case PersistenceResponseStatus::Blocked:
        return StoreMutationIntentKind::NoopBlocked;
    case PersistenceResponseStatus::Deferred:
        return StoreMutationIntentKind::NoopDeferred;
    case PersistenceResponseStatus::Rejected:
        return StoreMutationIntentKind::NoopRejected;
    }

    return StoreMutationIntentKind::NoopBlocked;
}

[[nodiscard]] AdapterExecutionFailureKind to_failure_kind(PersistenceResponseStatus status) {
    switch (status) {
    case PersistenceResponseStatus::Blocked:
        return AdapterExecutionFailureKind::SourceResponseBlocked;
    case PersistenceResponseStatus::Deferred:
        return AdapterExecutionFailureKind::SourceResponseDeferred;
    case PersistenceResponseStatus::Rejected:
        return AdapterExecutionFailureKind::SourceResponseRejected;
    case PersistenceResponseStatus::Accepted:
        return AdapterExecutionFailureKind::SourceResponseBlocked;
    }

    return AdapterExecutionFailureKind::SourceResponseBlocked;
}

[[nodiscard]] std::string failure_message_from_response(const PersistenceResponse &response) {
    if (response.response_blocker.has_value()) {
        return response.response_blocker->message;
    }

    switch (response.response_status) {
    case PersistenceResponseStatus::Blocked:
        return "source persistence response blocked adapter execution";
    case PersistenceResponseStatus::Deferred:
        return "source persistence response deferred adapter execution";
    case PersistenceResponseStatus::Rejected:
        return "source persistence response rejected adapter execution";
    case PersistenceResponseStatus::Accepted:
        return "";
    }

    return "source persistence response blocked adapter execution";
}

void validate_common_identity_fields(const AdapterExecutionReceipt &receipt,
                                     DiagnosticBag &diagnostics) {
    if (receipt.workflow_canonical_name.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution workflow_canonical_name must not be empty");
    }
    if (receipt.session_id.empty()) {
        emit_validation_error(
            diagnostics, "durable store import adapter execution session_id must not be empty");
    }
    if (receipt.run_id.has_value() && receipt.run_id->empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution run_id must not be empty when present");
    }
    if (receipt.input_fixture.empty()) {
        emit_validation_error(
            diagnostics, "durable store import adapter execution input_fixture must not be empty");
    }
    if (receipt.durable_store_import_decision_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution durable_store_import_decision_identity "
            "must not be empty");
    }
    if (receipt.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution durable_store_import_receipt_identity "
            "must not be empty");
    }
    if (receipt.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution "
            "durable_store_import_receipt_persistence_request_identity must not be empty");
    }
    if (receipt.durable_store_import_receipt_persistence_response_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution "
            "durable_store_import_receipt_persistence_response_identity must not be empty");
    }
    if (receipt.durable_store_import_adapter_execution_identity.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "durable_store_import_adapter_execution_identity must not be empty");
    }
    if (receipt.planned_durable_identity.empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution planned_durable_identity must not be empty");
    }
}

void validate_mutation_intent(const AdapterExecutionReceipt &receipt, DiagnosticBag &diagnostics) {
    if (receipt.mutation_intent.source_response_identity !=
        receipt.durable_store_import_receipt_persistence_response_identity) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution mutation_intent "
                              "source_response_identity must match source response identity");
    }
    if (receipt.mutation_intent.target_receipt_identity !=
        receipt.durable_store_import_receipt_identity) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution mutation_intent "
                              "target_receipt_identity must match receipt identity");
    }
    if (receipt.mutation_intent.target_planned_durable_identity !=
        receipt.planned_durable_identity) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution mutation_intent "
            "target_planned_durable_identity must match planned durable identity");
    }
}

} // namespace

AdapterExecutionValidationResult
validate_adapter_execution_receipt(const AdapterExecutionReceipt &receipt) {
    AdapterExecutionValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (receipt.format_version != kAdapterExecutionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution format_version must be '" +
                                  std::string(kAdapterExecutionFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_decision_receipt_persistence_response_format_version !=
        kPersistenceResponseFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "source_durable_store_import_decision_receipt_persistence_response_"
                              "format_version must be '" +
                                  std::string(kPersistenceResponseFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_decision_receipt_persistence_request_format_version !=
        kPersistenceRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "source_durable_store_import_decision_receipt_persistence_request_"
                              "format_version must be '" +
                                  std::string(kPersistenceRequestFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_decision_receipt_format_version !=
        kReceiptFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "source_durable_store_import_decision_receipt_format_version "
                              "must be '" +
                                  std::string(kReceiptFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_decision_format_version != kDecisionFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "source_durable_store_import_decision_format_version must be '" +
                                  std::string(kDecisionFormatVersion) + "'");
    }
    if (receipt.source_durable_store_import_request_format_version != kRequestFormatVersion) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution "
                              "source_durable_store_import_request_format_version must be '" +
                                  std::string(kRequestFormatVersion) + "'");
    }
    if (receipt.local_fake_store_contract_version != kLocalFakeDurableStoreContractVersion) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution local_fake_store_contract_version must be '" +
                std::string(kLocalFakeDurableStoreContractVersion) + "'");
    }

    if (receipt.source_package_identity.has_value()) {
        validate_package_identity(*receipt.source_package_identity, diagnostics);
    }

    validate_common_identity_fields(receipt, diagnostics);
    validate_mutation_intent(receipt, diagnostics);

    if (receipt.persistence_id.has_value() && receipt.persistence_id->empty()) {
        emit_validation_error(
            diagnostics,
            "durable store import adapter execution persistence_id must not be empty when present");
    }
    if (receipt.failure_attribution.has_value() && receipt.failure_attribution->message.empty()) {
        emit_validation_error(diagnostics,
                              "durable store import adapter execution failure_attribution "
                              "message must not be empty");
    }

    switch (receipt.response_status) {
    case PersistenceResponseStatus::Accepted:
        if (receipt.response_outcome != PersistenceResponseOutcome::AcceptPersistenceRequest) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires AcceptPersistenceRequest outcome");
        }
        if (!receipt.acknowledged_for_response) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires acknowledged_for_response");
        }
        if (receipt.response_boundary_kind !=
            PersistenceResponseBoundaryKind::AdapterResponseConsumable) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires AdapterResponseConsumable response boundary");
        }
        if (receipt.mutation_intent.kind != StoreMutationIntentKind::PersistReceipt ||
            !receipt.mutation_intent.mutates_store) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires mutating PersistReceipt mutation_intent");
        }
        if (receipt.mutation_status != StoreMutationStatus::Persisted) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires Persisted mutation_status");
        }
        if (!receipt.persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "requires persistence_id");
        }
        if (receipt.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Accepted status "
                                  "cannot contain failure_attribution");
        }
        break;
    case PersistenceResponseStatus::Blocked:
    case PersistenceResponseStatus::Deferred:
    case PersistenceResponseStatus::Rejected:
        if (receipt.acknowledged_for_response) {
            emit_validation_error(
                diagnostics,
                "durable store import adapter execution non-accepted status cannot be "
                "acknowledged_for_response");
        }
        if (receipt.mutation_intent.mutates_store ||
            receipt.mutation_intent.kind == StoreMutationIntentKind::PersistReceipt) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution non-accepted status "
                                  "requires non-mutating mutation_intent");
        }
        if (receipt.mutation_status != StoreMutationStatus::NotMutated) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution non-accepted status "
                                  "requires NotMutated mutation_status");
        }
        if (receipt.persistence_id.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution non-accepted status "
                                  "cannot contain persistence_id");
        }
        if (!receipt.failure_attribution.has_value()) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution non-accepted status "
                                  "requires failure_attribution");
        }
        if (receipt.response_status == PersistenceResponseStatus::Blocked &&
            receipt.mutation_intent.kind != StoreMutationIntentKind::NoopBlocked) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Blocked status "
                                  "requires NoopBlocked mutation_intent");
        }
        if (receipt.response_status == PersistenceResponseStatus::Deferred &&
            receipt.mutation_intent.kind != StoreMutationIntentKind::NoopDeferred) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Deferred status "
                                  "requires NoopDeferred mutation_intent");
        }
        if (receipt.response_status == PersistenceResponseStatus::Rejected &&
            receipt.mutation_intent.kind != StoreMutationIntentKind::NoopRejected) {
            emit_validation_error(diagnostics,
                                  "durable store import adapter execution Rejected status "
                                  "requires NoopRejected mutation_intent");
        }
        break;
    }

    return result;
}

AdapterExecutionResult build_adapter_execution_receipt(const PersistenceResponse &response) {
    AdapterExecutionResult result{
        .receipt = std::nullopt,
        .diagnostics = {},
    };

    const auto response_validation = validate_persistence_response(response);
    result.diagnostics.append(response_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    const auto mutates_store = response.response_status == PersistenceResponseStatus::Accepted;
    AdapterExecutionReceipt receipt{
        .format_version = std::string(kAdapterExecutionFormatVersion),
        .source_durable_store_import_decision_receipt_persistence_response_format_version =
            response.format_version,
        .source_durable_store_import_decision_receipt_persistence_request_format_version =
            response
                .source_durable_store_import_decision_receipt_persistence_request_format_version,
        .source_durable_store_import_decision_receipt_format_version =
            response.source_durable_store_import_decision_receipt_format_version,
        .source_durable_store_import_decision_format_version =
            response.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            response.source_durable_store_import_request_format_version,
        .source_package_identity = response.source_package_identity,
        .workflow_canonical_name = response.workflow_canonical_name,
        .session_id = response.session_id,
        .run_id = response.run_id,
        .input_fixture = response.input_fixture,
        .durable_store_import_decision_identity = response.durable_store_import_decision_identity,
        .durable_store_import_receipt_identity = response.durable_store_import_receipt_identity,
        .durable_store_import_receipt_persistence_request_identity =
            response.durable_store_import_receipt_persistence_request_identity,
        .durable_store_import_receipt_persistence_response_identity =
            response.durable_store_import_receipt_persistence_response_identity,
        .durable_store_import_adapter_execution_identity =
            build_adapter_execution_identity(response.source_package_identity,
                                             response.workflow_canonical_name,
                                             response.session_id,
                                             response.response_outcome),
        .planned_durable_identity = response.planned_durable_identity,
        .response_status = response.response_status,
        .response_outcome = response.response_outcome,
        .response_boundary_kind = response.receipt_persistence_response_boundary_kind,
        .acknowledged_for_response = response.acknowledged_for_response,
        .store_kind = AdapterExecutionStoreKind::LocalFakeDurableStore,
        .local_fake_store_contract_version = std::string(kLocalFakeDurableStoreContractVersion),
        .mutation_intent =
            StoreMutationIntent{
                .kind = to_mutation_intent_kind(response.response_status),
                .source_response_identity =
                    response.durable_store_import_receipt_persistence_response_identity,
                .target_receipt_identity = response.durable_store_import_receipt_identity,
                .target_planned_durable_identity = response.planned_durable_identity,
                .mutates_store = mutates_store,
            },
        .mutation_status =
            mutates_store ? StoreMutationStatus::Persisted : StoreMutationStatus::NotMutated,
        .persistence_id = mutates_store ? std::optional<std::string>(build_fake_persistence_id(
                                              response.source_package_identity,
                                              response.workflow_canonical_name,
                                              response.session_id,
                                              response.response_outcome))
                                        : std::nullopt,
        .failure_attribution =
            mutates_store
                ? std::nullopt
                : std::optional<AdapterExecutionFailureAttribution>(
                      AdapterExecutionFailureAttribution{
                          .kind = to_failure_kind(response.response_status),
                          .message = failure_message_from_response(response),
                          .required_capability = response.next_required_adapter_capability,
                      }),
    };

    const auto validation = validate_adapter_execution_receipt(receipt);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.receipt = std::move(receipt);
    return result;
}

} // namespace ahfl::durable_store_import
