#include "ahfl/durable_store_import/receipt_persistence_response_review.hpp"
#include "ahfl/validation/common.hpp"

#include <string>
#include <utility>

namespace ahfl::durable_store_import {

namespace {

inline constexpr std::string_view kValidationDiagnosticCode = "AHFL.VAL.DSI_RECEIPT_PERSISTENCE_RESPONSE_REVIEW";

void emit_validation_error(DiagnosticBag &diagnostics, std::string message) {
    validation::emit_validation_error(diagnostics, kValidationDiagnosticCode, message);
}

[[nodiscard]] std::string response_status_summary(
    PersistenceResponseStatus status) {
    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return "accepted for persistence response";
    case PersistenceResponseStatus::Blocked:
        return "blocked";
    case PersistenceResponseStatus::Deferred:
        return "deferred for partial persistence";
    case PersistenceResponseStatus::Rejected:
        return "rejected due to failure";
    }

    return "unknown";
}

[[nodiscard]] std::string next_action_summary(
    PersistenceResponseStatus status,
    bool acknowledged) {
    if (acknowledged) {
        return "response acknowledged; ready for next persistence phase";
    }

    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return "acknowledge response";
    case PersistenceResponseStatus::Blocked:
        return "resolve blocker before proceeding";
    case PersistenceResponseStatus::Deferred:
        return "wait for capability or resolve partial state";
    case PersistenceResponseStatus::Rejected:
        return "review failure and determine recovery path";
    }

    return "unknown action";
}

[[nodiscard]] PersistenceResponseReviewNextActionKind
to_next_action_kind(PersistenceResponseStatus status, bool acknowledged) {
    if (acknowledged) {
        return PersistenceResponseReviewNextActionKind::AcknowledgeResponse;
    }

    switch (status) {
    case PersistenceResponseStatus::Accepted:
        return PersistenceResponseReviewNextActionKind::AcknowledgeResponse;
    case PersistenceResponseStatus::Blocked:
        return PersistenceResponseReviewNextActionKind::ResolveBlocker;
    case PersistenceResponseStatus::Deferred:
        return PersistenceResponseReviewNextActionKind::WaitforCapability;
    case PersistenceResponseStatus::Rejected:
        return PersistenceResponseReviewNextActionKind::ReviewFailure;
    }

    return PersistenceResponseReviewNextActionKind::ResolveBlocker;
}

} // namespace

PersistenceResponseReviewValidationResult
validate_persistence_response_review(const PersistenceResponseReviewSummary &review) {
    PersistenceResponseReviewValidationResult result;
    auto &diagnostics = result.diagnostics;

    if (review.format_version != kPersistenceResponseReviewFormatVersion) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review format_version must be '" +
                          std::string(kPersistenceResponseReviewFormatVersion) + "'");
    }

    if (review.source_durable_store_import_decision_receipt_persistence_response_format_version !=
        kPersistenceResponseFormatVersion) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence response review source_durable_store_import_decision_receipt_persistence_response_format_version must be '" +
            std::string(kPersistenceResponseFormatVersion) + "'");
    }

    if (review.workflow_canonical_name.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review workflow_canonical_name "
                          "must not be empty");
    }

    if (review.session_id.empty()) {
        emit_validation_error(diagnostics,
            "durable store import receipt persistence response review session_id must not be empty");
    }

    if (review.durable_store_import_decision_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review "
                          "durable_store_import_decision_identity must not be empty");
    }

    if (review.durable_store_import_receipt_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review "
                          "durable_store_import_receipt_identity must not be empty");
    }

    if (review.durable_store_import_receipt_persistence_request_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review "
                          "durable_store_import_receipt_persistence_request_identity must not be empty");
    }

    if (review.durable_store_import_receipt_persistence_response_identity.empty()) {
        emit_validation_error(diagnostics, "durable store import receipt persistence response review "
                          "durable_store_import_receipt_persistence_response_identity must not be empty");
    }

    return result;
}

PersistenceResponseReviewResult
build_persistence_response_review(const PersistenceResponse &response) {
    PersistenceResponseReviewResult result{
        .review = std::nullopt,
        .diagnostics = {},
    };

    const auto response_validation = validate_persistence_response(response);
    result.diagnostics.append(response_validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    PersistenceResponseReviewSummary review{
        .format_version = std::string(kPersistenceResponseReviewFormatVersion),
        .source_durable_store_import_decision_receipt_persistence_response_format_version = response.format_version,
        .source_durable_store_import_decision_receipt_persistence_request_format_version =
            response.source_durable_store_import_decision_receipt_persistence_request_format_version,
        .source_durable_store_import_decision_receipt_format_version =
            response.source_durable_store_import_decision_receipt_format_version,
        .source_durable_store_import_decision_format_version =
            response.source_durable_store_import_decision_format_version,
        .source_durable_store_import_request_format_version =
            response.source_durable_store_import_request_format_version,
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
        .response_status = response.response_status,
        .response_outcome = response.response_outcome,
        .response_boundary_kind = response.receipt_persistence_response_boundary_kind,
        .acknowledged_for_response = response.acknowledged_for_response,
        .next_required_adapter_capability = response.next_required_adapter_capability,
        .response_blocker = response.response_blocker,
        .response_preview = PersistenceResponsePreview{
            .response_identity = response.durable_store_import_receipt_persistence_response_identity,
            .response_status = response.response_status,
            .response_outcome = response.response_outcome,
            .acknowledged_for_response = response.acknowledged_for_response,
            .response_boundary_kind = response.receipt_persistence_response_boundary_kind,
        },
        .adapter_response_contract_summary = response_status_summary(response.response_status),
        .next_step_recommendation = next_action_summary(response.response_status,
                                                        response.acknowledged_for_response),
        .next_action = to_next_action_kind(response.response_status,
                                           response.acknowledged_for_response),
    };

    const auto validation = validate_persistence_response_review(review);
    result.diagnostics.append(validation.diagnostics);
    if (result.has_errors()) {
        return result;
    }

    result.review = std::move(review);
    return result;
}

} // namespace ahfl::durable_store_import
