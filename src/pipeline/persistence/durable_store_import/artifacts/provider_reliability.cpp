#include "model_includes.hpp"

#include "artifact_writer.hpp"

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_recovery_handoff {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
planning_status_name(durable_store_import::ProviderWritePlanningStatus status) {
    switch (status) {
    case durable_store_import::ProviderWritePlanningStatus::Planned:
        return "planned";
    case durable_store_import::ProviderWritePlanningStatus::NotPlanned:
        return "not_planned";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderRecoveryHandoffNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::NoRecoveryRequired:
        return "no_recovery_required";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::RetryUnavailable:
        return "retry_unavailable";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::ResumeUnavailable:
        return "resume_unavailable";
    case durable_store_import::ProviderRecoveryHandoffNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::ProviderCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderCapabilityKind::PlanProviderWrite:
        return "plan_provider_write";
    case durable_store_import::ProviderCapabilityKind::PlanRetryPlaceholder:
        return "plan_retry_placeholder";
    case durable_store_import::ProviderCapabilityKind::PlanResumePlaceholder:
        return "plan_resume_placeholder";
    case durable_store_import::ProviderCapabilityKind::PlanRecoveryHandoff:
        return "plan_recovery_handoff";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderPlanningFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderPlanningFailureKind::SourceExecutionNotPersisted:
        return "source_execution_not_persisted";
    case durable_store_import::ProviderPlanningFailureKind::UnsupportedProviderCapability:
        return "unsupported_provider_capability";
    }

    return "invalid";
}

void print_retry_resume_placeholder(
    std::ostream &out,
    int indent_level,
    const durable_store_import::ProviderRetryResumePlaceholder &placeholder) {
    line(out, indent_level, "retry_resume_placeholder {");
    line(out,
         indent_level + 1,
         "retry_placeholder_available " +
             std::string(placeholder.retry_placeholder_available ? "true" : "false"));
    line(out,
         indent_level + 1,
         "resume_placeholder_available " +
             std::string(placeholder.resume_placeholder_available ? "true" : "false"));
    line(out,
         indent_level + 1,
         "retry_placeholder_identity " + (placeholder.retry_placeholder_identity.has_value()
                                              ? *placeholder.retry_placeholder_identity
                                              : std::string("none")));
    line(out,
         indent_level + 1,
         "resume_placeholder_identity " + (placeholder.resume_placeholder_identity.has_value()
                                               ? *placeholder.resume_placeholder_identity
                                               : std::string("none")));
    line(out, indent_level, "}");
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderPlanningFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "missing_capability " + (failure->missing_capability.has_value()
                                      ? capability_name(*failure->missing_capability)
                                      : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_recovery_handoff(
    const durable_store_import::ProviderRecoveryHandoffPreview &preview, std::ostream &out) {
    out << preview.format_version << '\n';
    line(out,
         0,
         "source_provider_write_attempt_format " +
             preview.source_durable_store_import_provider_write_attempt_preview_format_version);
    line(out,
         0,
         "source_adapter_execution_format " +
             preview.source_durable_store_import_adapter_execution_format_version);
    line(out, 0, "workflow " + preview.workflow_canonical_name);
    line(out, 0, "session " + preview.session_id);
    line(out, 0, "run_id " + (preview.run_id.has_value() ? *preview.run_id : "none"));
    line(out, 0, "input_fixture " + preview.input_fixture);
    line(out,
         0,
         "durable_store_import_adapter_execution_identity " +
             preview.durable_store_import_adapter_execution_identity);
    line(out,
         0,
         "durable_store_import_provider_write_attempt_identity " +
             preview.durable_store_import_provider_write_attempt_identity);
    line(out, 0, "planning_status " + planning_status_name(preview.planning_status));
    line(out,
         0,
         "provider_persistence_id " + (preview.provider_persistence_id.has_value()
                                           ? *preview.provider_persistence_id
                                           : std::string("none")));
    line(out, 0, "next_action " + next_action_name(preview.next_action));
    line(out, 0, "recovery_handoff_boundary " + preview.recovery_handoff_boundary_summary);
    line(out, 0, "next_step " + preview.next_step_recommendation);
    print_retry_resume_placeholder(out, 0, preview.retry_resume_placeholder);
    print_failure_attribution(out, 0, preview.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_recovery_handoff

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_attempt {
namespace {

class DurableStoreImportProviderWriteAttemptJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderWriteAttemptJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderWriteAttemptPreview &preview) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(preview.format_version); });
            field("source_durable_store_import_adapter_execution_format_version", [&]() {
                write_string(preview.source_durable_store_import_adapter_execution_format_version);
            });
            field(
                "source_durable_store_import_decision_receipt_persistence_response_format_version",
                [&]() {
                    write_string(
                        preview
                            .source_durable_store_import_decision_receipt_persistence_response_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(preview.workflow_canonical_name); });
            field("session_id", [&]() { write_string(preview.session_id); });
            field("run_id", [&]() { print_optional_string(preview.run_id); });
            field("input_fixture", [&]() { write_string(preview.input_fixture); });
            field("durable_store_import_receipt_persistence_response_identity", [&]() {
                write_string(preview.durable_store_import_receipt_persistence_response_identity);
            });
            field("durable_store_import_adapter_execution_identity",
                  [&]() { write_string(preview.durable_store_import_adapter_execution_identity); });
            field("durable_store_import_provider_write_attempt_identity", [&]() {
                write_string(preview.durable_store_import_provider_write_attempt_identity);
            });
            field("response_status", [&]() { print_response_status(preview.response_status); });
            field("response_outcome", [&]() { print_response_outcome(preview.response_outcome); });
            field("adapter_mutation_status",
                  [&]() { print_mutation_status(preview.adapter_mutation_status); });
            field("source_persistence_id",
                  [&]() { print_optional_string(preview.source_persistence_id); });
            field("provider_config", [&]() { print_provider_config(preview.provider_config, 1); });
            field("capability_matrix",
                  [&]() { print_capability_matrix(preview.capability_matrix, 1); });
            field("write_intent", [&]() { print_write_intent(preview.write_intent, 1); });
            field("planning_status", [&]() { print_planning_status(preview.planning_status); });
            field("retry_resume_placeholder",
                  [&]() { print_retry_resume_placeholder(preview.retry_resume_placeholder, 1); });
            field("failure_attribution", [&]() {
                print_optional(preview.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_adapter_kind(durable_store_import::ProviderAdapterKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAdapterKind::ProviderNeutralShim,
                                    "provider_neutral_shim"));
    }

    void print_response_status(durable_store_import::PersistenceResponseStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Accepted,
                                    "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Deferred,
                                    "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceResponseStatus::Rejected,
                                    "rejected"));
    }

    void print_response_outcome(durable_store_import::PersistenceResponseOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest,
                "accept_persistence_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest,
                "block_blocked_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest,
                "defer_deferred_request");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseOutcome::RejectFailedRequest,
                "reject_failed_request"));
    }

    void print_mutation_status(durable_store_import::StoreMutationStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::Persisted,
                                    "persisted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::NotMutated,
                                    "not_mutated"));
    }

    void print_write_intent_kind(durable_store_import::ProviderWriteIntentKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteIntentKind::ProviderPersistReceipt,
                "provider_persist_receipt");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteIntentKind::NoopBlocked,
                                    "noop_blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteIntentKind::NoopDeferred,
                                    "noop_deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteIntentKind::NoopRejected,
                                    "noop_rejected");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteIntentKind::NoopUnsupportedCapability,
                "noop_unsupported_capability"));
    }

    void print_planning_status(durable_store_import::ProviderWritePlanningStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::Planned,
                                    "planned");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::NotPlanned,
                                    "not_planned"));
    }

    void print_capability(durable_store_import::ProviderCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCapabilityKind::PlanProviderWrite,
                                    "plan_provider_write");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderCapabilityKind::PlanRetryPlaceholder,
                "plan_retry_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderCapabilityKind::PlanResumePlaceholder,
                "plan_resume_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderCapabilityKind::PlanRecoveryHandoff,
                "plan_recovery_handoff"));
    }

    void print_failure_kind(durable_store_import::ProviderPlanningFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderPlanningFailureKind::SourceExecutionNotPersisted,
                "source_execution_not_persisted");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderPlanningFailureKind::UnsupportedProviderCapability,
                "unsupported_provider_capability"));
    }

    void print_provider_config(const durable_store_import::ProviderAdapterConfig &config,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(config.format_version); });
            field("adapter_kind", [&]() { print_adapter_kind(config.adapter_kind); });
            field("config_identity", [&]() { write_string(config.config_identity); });
            field("provider_profile_ref", [&]() { write_string(config.provider_profile_ref); });
            field("provider_namespace", [&]() { write_string(config.provider_namespace); });
            field("credential_free", [&]() { write_bool(config.credential_free); });
            field("secret_material_reference",
                  [&]() { print_optional_string(config.secret_material_reference); });
            field("endpoint_secret_reference",
                  [&]() { print_optional_string(config.endpoint_secret_reference); });
            field("object_path", [&]() { print_optional_string(config.object_path); });
            field("database_key", [&]() { print_optional_string(config.database_key); });
        });
    }

    void print_capability_matrix(const durable_store_import::ProviderCapabilityMatrix &matrix,
                                 int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(matrix.format_version); });
            field("capability_matrix_identity",
                  [&]() { write_string(matrix.capability_matrix_identity); });
            field("provider_config_identity",
                  [&]() { write_string(matrix.provider_config_identity); });
            field("supports_provider_write", [&]() { write_bool(matrix.supports_provider_write); });
            field("supports_retry_placeholder",
                  [&]() { write_bool(matrix.supports_retry_placeholder); });
            field("supports_resume_placeholder",
                  [&]() { write_bool(matrix.supports_resume_placeholder); });
            field("supports_recovery_handoff",
                  [&]() { write_bool(matrix.supports_recovery_handoff); });
        });
    }

    void print_write_intent(const durable_store_import::ProviderWriteIntent &intent,
                            int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_write_intent_kind(intent.kind); });
            field("source_adapter_execution_identity",
                  [&]() { write_string(intent.source_adapter_execution_identity); });
            field("source_persistence_id",
                  [&]() { print_optional_string(intent.source_persistence_id); });
            field("provider_namespace", [&]() { write_string(intent.provider_namespace); });
            field("provider_persistence_id",
                  [&]() { print_optional_string(intent.provider_persistence_id); });
            field("mutates_provider", [&]() { write_bool(intent.mutates_provider); });
        });
    }

    void print_retry_resume_placeholder(
        const durable_store_import::ProviderRetryResumePlaceholder &placeholder, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("retry_placeholder_available",
                  [&]() { write_bool(placeholder.retry_placeholder_available); });
            field("resume_placeholder_available",
                  [&]() { write_bool(placeholder.resume_placeholder_available); });
            field("retry_placeholder_identity",
                  [&]() { print_optional_string(placeholder.retry_placeholder_identity); });
            field("resume_placeholder_identity",
                  [&]() { print_optional_string(placeholder.resume_placeholder_identity); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderPlanningFailureAttribution &failure, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("missing_capability", [&]() {
                print_optional(failure.missing_capability,
                               [&](const auto &value) { print_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_attempt_json(
    const durable_store_import::ProviderWriteAttemptPreview &preview, std::ostream &out) {
    DurableStoreImportProviderWriteAttemptJsonPrinter(out).print(preview);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_attempt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_commit_receipt {
namespace {

class CommitReceiptJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit CommitReceiptJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderWriteCommitReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("write_commit_receipt_identity", [&]() {
                write_string(receipt.durable_store_import_provider_write_commit_receipt_identity);
            });
            field("commit_status", [&]() { print_status(receipt.commit_status); });
            field("provider_commit_reference",
                  [&]() { write_string(receipt.provider_commit_reference); });
            field("provider_commit_digest",
                  [&]() { write_string(receipt.provider_commit_digest); });
            field("idempotency_key", [&]() { write_string(receipt.idempotency_key); });
            field("secret_free", [&]() { write_bool(receipt.secret_free); });
            field("raw_provider_payload_persisted",
                  [&]() { write_bool(receipt.raw_provider_payload_persisted); });
            field("failure_attribution", [&]() { print_failure(receipt.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderWriteCommitStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Committed,
                                    "committed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Duplicate,
                                    "duplicate");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Partial,
                                    "partial");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Failed,
                                    "failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderWriteCommitFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteCommitFailureKind::AdapterResultNotReady,
                "adapter_result_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteCommitFailureKind::RetryDecisionNotReady,
                "retry_decision_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteCommitFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteCommitFailureKind::DuplicateDetected,
                "duplicate_detected");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteCommitFailureKind::PartialCommit,
                "partial_commit"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderWriteCommitFailureAttribution> &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_commit_receipt_json(
    const durable_store_import::ProviderWriteCommitReceipt &receipt, std::ostream &out) {
    CommitReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_commit_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_commit_review {
namespace {

void print_status(durable_store_import::ProviderWriteCommitStatus status, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Committed,
                                        "committed");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Duplicate,
                                        "duplicate");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Partial,
                                        "partial");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Failed,
                                        "failed");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderWriteCommitStatus::Blocked,
                                        "blocked"));
}

void print_next(durable_store_import::ProviderWriteCommitNextActionKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteCommitNextActionKind::ReadyForRecoveryAudit,
            "ready_for_recovery_audit");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteCommitNextActionKind::RetryUsingIdempotencyContract,
            "retry_using_idempotency_contract");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteCommitNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_write_commit_review(
    const durable_store_import::ProviderWriteCommitReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "commit_receipt " << review.durable_store_import_provider_write_commit_receipt_identity
        << '\n';
    out << "commit_status ";
    print_status(review.commit_status, out);
    out << '\n';
    out << "provider_commit_reference " << review.provider_commit_reference << '\n';
    out << "provider_commit_digest " << review.provider_commit_digest << '\n';
    out << "summary " << review.commit_review_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_commit_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_recovery_checkpoint {
namespace {

class RecoveryCheckpointJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit RecoveryCheckpointJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderWriteRecoveryCheckpoint &checkpoint) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(checkpoint.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(checkpoint.workflow_canonical_name); });
            field("session_id", [&]() { write_string(checkpoint.session_id); });
            field("run_id", [&]() { print_optional_string(checkpoint.run_id); });
            field("input_fixture", [&]() { write_string(checkpoint.input_fixture); });
            field("write_recovery_checkpoint_identity", [&]() {
                write_string(
                    checkpoint.durable_store_import_provider_write_recovery_checkpoint_identity);
            });
            field("recovery_checkpoint_reference",
                  [&]() { write_string(checkpoint.recovery_checkpoint_reference); });
            field("resume_token_version", [&]() { write_string(checkpoint.resume_token_version); });
            field("resume_token_placeholder_identity",
                  [&]() { write_string(checkpoint.resume_token_placeholder_identity); });
            field("idempotency_key", [&]() { write_string(checkpoint.idempotency_key); });
            field("checkpoint_status", [&]() { print_status(checkpoint.checkpoint_status); });
            field("recovery_eligibility",
                  [&]() { print_eligibility(checkpoint.recovery_eligibility); });
            field("recovery_summary", [&]() { write_string(checkpoint.recovery_summary); });
            field("failure_attribution",
                  [&]() { print_failure(checkpoint.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderWriteRecoveryCheckpointStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryCheckpointStatus::StableCommitted,
                "stable_committed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryCheckpointStatus::DuplicateReview,
                "duplicate_review");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryCheckpointStatus::PartialWrite,
                "partial_write");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryCheckpointStatus::FailedWrite,
                "failed_write");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryCheckpointStatus::Blocked, "blocked"));
    }

    void print_eligibility(durable_store_import::ProviderWriteRecoveryEligibility eligibility) {
        AHFL_ARTIFACT_PRINT_ENUM(
            eligibility,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::NotRequired,
                "not_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::ResumeRequired,
                "resume_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::DuplicateLookupRequired,
                "duplicate_lookup_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::ManualRecoveryRequired,
                "manual_recovery_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRecoveryEligibility::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderWriteRecoveryFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::CommitReceiptNotReady,
                "commit_receipt_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::PartialWrite,
                "partial_write");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::DuplicateCommitUnresolved,
                "duplicate_commit_unresolved");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRecoveryFailureKind::Blocked,
                                    "blocked"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderWriteRecoveryFailureAttribution> &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_recovery_checkpoint_json(
    const durable_store_import::ProviderWriteRecoveryCheckpoint &checkpoint, std::ostream &out) {
    RecoveryCheckpointJsonPrinter(out).print(checkpoint);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_recovery_checkpoint

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_recovery_plan {
namespace {

class RecoveryPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit RecoveryPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderWriteRecoveryPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("write_recovery_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_write_recovery_plan_identity);
            });
            field("recovery_eligibility", [&]() { print_eligibility(plan.recovery_eligibility); });
            field("plan_action", [&]() { print_action(plan.plan_action); });
            field("partial_write_recovery_plan",
                  [&]() { write_string(plan.partial_write_recovery_plan); });
            field("resume_token_placeholder_identity",
                  [&]() { write_string(plan.resume_token_placeholder_identity); });
            field("requires_provider_commit_lookup",
                  [&]() { write_bool(plan.requires_provider_commit_lookup); });
            field("requires_operator_approval",
                  [&]() { write_bool(plan.requires_operator_approval); });
            field("secret_free", [&]() { write_bool(plan.secret_free); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_eligibility(durable_store_import::ProviderWriteRecoveryEligibility eligibility) {
        AHFL_ARTIFACT_PRINT_ENUM(
            eligibility,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::NotRequired,
                "not_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::ResumeRequired,
                "resume_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::DuplicateLookupRequired,
                "duplicate_lookup_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryEligibility::ManualRecoveryRequired,
                "manual_recovery_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRecoveryEligibility::Blocked,
                                    "blocked"));
    }

    void print_action(durable_store_import::ProviderWriteRecoveryPlanAction action) {
        AHFL_ARTIFACT_PRINT_ENUM(
            action,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryPlanAction::NoopCommitted,
                "noop_committed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryPlanAction::LookupDuplicateCommit,
                "lookup_duplicate_commit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey,
                "resume_with_idempotency_key");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryPlanAction::ManualRemediation,
                "manual_remediation");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryPlanAction::WaitForCommitReceipt,
                "wait_for_commit_receipt"));
    }

    void print_failure_kind(durable_store_import::ProviderWriteRecoveryFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::CommitReceiptNotReady,
                "commit_receipt_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::PartialWrite,
                "partial_write");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::DuplicateCommitUnresolved,
                "duplicate_commit_unresolved");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRecoveryFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRecoveryFailureKind::Blocked,
                                    "blocked"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderWriteRecoveryFailureAttribution> &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_recovery_plan_json(
    const durable_store_import::ProviderWriteRecoveryPlan &plan, std::ostream &out) {
    RecoveryPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_recovery_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_recovery_review {
namespace {

void print_action(durable_store_import::ProviderWriteRecoveryPlanAction action, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        action,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryPlanAction::NoopCommitted, "noop_committed");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryPlanAction::LookupDuplicateCommit,
            "lookup_duplicate_commit");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey,
            "resume_with_idempotency_key");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryPlanAction::ManualRemediation,
            "manual_remediation");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryPlanAction::WaitForCommitReceipt,
            "wait_for_commit_receipt"));
}

void print_next(durable_store_import::ProviderWriteRecoveryNextActionKind action,
                std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        action,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryNextActionKind::ReadyForAuditEvent,
            "ready_for_audit_event");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryNextActionKind::ResumeUsingToken,
            "resume_using_token");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderWriteRecoveryNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_write_recovery_review(
    const durable_store_import::ProviderWriteRecoveryReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "recovery_plan " << review.durable_store_import_provider_write_recovery_plan_identity
        << '\n';
    out << "plan_action ";
    print_action(review.plan_action, out);
    out << '\n';
    out << "summary " << review.recovery_review_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_recovery_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_write_retry_decision {
namespace {

class RetryDecisionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit RetryDecisionJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderWriteRetryDecision &decision) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(decision.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(decision.workflow_canonical_name); });
            field("session_id", [&]() { write_string(decision.session_id); });
            field("run_id", [&]() { print_optional_string(decision.run_id); });
            field("input_fixture", [&]() { write_string(decision.input_fixture); });
            field("write_retry_decision_identity", [&]() {
                write_string(decision.durable_store_import_provider_write_retry_decision_identity);
            });
            field("idempotency_key_namespace",
                  [&]() { write_string(decision.idempotency_key_namespace); });
            field("idempotency_key", [&]() { write_string(decision.idempotency_key); });
            field("retry_token_version", [&]() { write_string(decision.retry_token_version); });
            field("retry_token_placeholder_identity",
                  [&]() { write_string(decision.retry_token_placeholder_identity); });
            field("retry_eligibility", [&]() { print_eligibility(decision.retry_eligibility); });
            field("retry_allowed", [&]() { write_bool(decision.retry_allowed); });
            field("duplicate_write_possible",
                  [&]() { write_bool(decision.duplicate_write_possible); });
            field("duplicate_detection_summary",
                  [&]() { write_string(decision.duplicate_detection_summary); });
            field("retry_decision_summary",
                  [&]() { write_string(decision.retry_decision_summary); });
            field("failure_attribution", [&]() { print_failure(decision.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_eligibility(durable_store_import::ProviderWriteRetryEligibility eligibility) {
        AHFL_ARTIFACT_PRINT_ENUM(
            eligibility,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRetryEligibility::Retryable,
                                    "retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryEligibility::NonRetryable, "non_retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryEligibility::DuplicateReviewRequired,
                "duplicate_review_required");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryEligibility::NotApplicable,
                "not_applicable");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWriteRetryEligibility::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderWriteRetryFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryFailureKind::AdapterResultNotReady,
                "adapter_result_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryFailureKind::RetryableProviderFailure,
                "retryable_provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryFailureKind::NonRetryableProviderFailure,
                "non_retryable_provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderWriteRetryFailureKind::DuplicateWriteSuspected,
                "duplicate_write_suspected"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderWriteRetryFailureAttribution> &failure,
        int indent_level) {
        if (!failure.has_value()) {
            out_ << "null";
            return;
        }
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure->kind); });
            field("message", [&]() { write_string(failure->message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_retry_decision_json(
    const durable_store_import::ProviderWriteRetryDecision &decision, std::ostream &out) {
    RetryDecisionJsonPrinter(out).print(decision);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_write_retry_decision
