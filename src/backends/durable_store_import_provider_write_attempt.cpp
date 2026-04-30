#include "ahfl/backends/durable_store_import_provider_write_attempt.hpp"

#include "ahfl/support/json.hpp"

#include <ostream>
#include <string>

namespace ahfl {
namespace {

class DurableStoreImportProviderWriteAttemptJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportProviderWriteAttemptJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

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
                if (preview.failure_attribution.has_value()) {
                    print_failure_attribution(*preview.failure_attribution, 1);
                    return;
                }
                out_ << "null";
            });
        });
        out_ << '\n';
    }

  private:
    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }

    void print_adapter_kind(durable_store_import::ProviderAdapterKind kind) {
        switch (kind) {
        case durable_store_import::ProviderAdapterKind::ProviderNeutralShim:
            write_string("provider_neutral_shim");
            return;
        }
    }

    void print_response_status(durable_store_import::PersistenceResponseStatus status) {
        switch (status) {
        case durable_store_import::PersistenceResponseStatus::Accepted:
            write_string("accepted");
            return;
        case durable_store_import::PersistenceResponseStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::PersistenceResponseStatus::Deferred:
            write_string("deferred");
            return;
        case durable_store_import::PersistenceResponseStatus::Rejected:
            write_string("rejected");
            return;
        }
    }

    void print_response_outcome(durable_store_import::PersistenceResponseOutcome outcome) {
        switch (outcome) {
        case durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest:
            write_string("accept_persistence_request");
            return;
        case durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest:
            write_string("block_blocked_request");
            return;
        case durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest:
            write_string("defer_deferred_request");
            return;
        case durable_store_import::PersistenceResponseOutcome::RejectFailedRequest:
            write_string("reject_failed_request");
            return;
        }
    }

    void print_mutation_status(durable_store_import::StoreMutationStatus status) {
        switch (status) {
        case durable_store_import::StoreMutationStatus::Persisted:
            write_string("persisted");
            return;
        case durable_store_import::StoreMutationStatus::NotMutated:
            write_string("not_mutated");
            return;
        }
    }

    void print_write_intent_kind(durable_store_import::ProviderWriteIntentKind kind) {
        switch (kind) {
        case durable_store_import::ProviderWriteIntentKind::ProviderPersistReceipt:
            write_string("provider_persist_receipt");
            return;
        case durable_store_import::ProviderWriteIntentKind::NoopBlocked:
            write_string("noop_blocked");
            return;
        case durable_store_import::ProviderWriteIntentKind::NoopDeferred:
            write_string("noop_deferred");
            return;
        case durable_store_import::ProviderWriteIntentKind::NoopRejected:
            write_string("noop_rejected");
            return;
        case durable_store_import::ProviderWriteIntentKind::NoopUnsupportedCapability:
            write_string("noop_unsupported_capability");
            return;
        }
    }

    void print_planning_status(durable_store_import::ProviderWritePlanningStatus status) {
        switch (status) {
        case durable_store_import::ProviderWritePlanningStatus::Planned:
            write_string("planned");
            return;
        case durable_store_import::ProviderWritePlanningStatus::NotPlanned:
            write_string("not_planned");
            return;
        }
    }

    void print_capability(durable_store_import::ProviderCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderCapabilityKind::PlanProviderWrite:
            write_string("plan_provider_write");
            return;
        case durable_store_import::ProviderCapabilityKind::PlanRetryPlaceholder:
            write_string("plan_retry_placeholder");
            return;
        case durable_store_import::ProviderCapabilityKind::PlanResumePlaceholder:
            write_string("plan_resume_placeholder");
            return;
        case durable_store_import::ProviderCapabilityKind::PlanRecoveryHandoff:
            write_string("plan_recovery_handoff");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderPlanningFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderPlanningFailureKind::SourceExecutionNotPersisted:
            write_string("source_execution_not_persisted");
            return;
        case durable_store_import::ProviderPlanningFailureKind::UnsupportedProviderCapability:
            write_string("unsupported_provider_capability");
            return;
        }
    }

    void print_provider_config(const durable_store_import::ProviderAdapterConfig &config,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(config.format_version); });
            field("adapter_kind", [&]() { print_adapter_kind(config.adapter_kind); });
            field("config_identity", [&]() { write_string(config.config_identity); });
            field("provider_profile_ref", [&]() { write_string(config.provider_profile_ref); });
            field("provider_namespace", [&]() { write_string(config.provider_namespace); });
            field("credential_free",
                  [&]() { out_ << (config.credential_free ? "true" : "false"); });
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
            field("supports_provider_write",
                  [&]() { out_ << (matrix.supports_provider_write ? "true" : "false"); });
            field("supports_retry_placeholder",
                  [&]() { out_ << (matrix.supports_retry_placeholder ? "true" : "false"); });
            field("supports_resume_placeholder",
                  [&]() { out_ << (matrix.supports_resume_placeholder ? "true" : "false"); });
            field("supports_recovery_handoff",
                  [&]() { out_ << (matrix.supports_recovery_handoff ? "true" : "false"); });
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
            field("mutates_provider",
                  [&]() { out_ << (intent.mutates_provider ? "true" : "false"); });
        });
    }

    void print_retry_resume_placeholder(
        const durable_store_import::ProviderRetryResumePlaceholder &placeholder, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("retry_placeholder_available",
                  [&]() { out_ << (placeholder.retry_placeholder_available ? "true" : "false"); });
            field("resume_placeholder_available",
                  [&]() { out_ << (placeholder.resume_placeholder_available ? "true" : "false"); });
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
                if (failure.missing_capability.has_value()) {
                    print_capability(*failure.missing_capability);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_durable_store_import_provider_write_attempt_json(
    const durable_store_import::ProviderWriteAttemptPreview &preview, std::ostream &out) {
    DurableStoreImportProviderWriteAttemptJsonPrinter(out).print(preview);
}

} // namespace ahfl
