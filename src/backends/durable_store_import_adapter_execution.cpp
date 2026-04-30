#include "ahfl/backends/durable_store_import_adapter_execution.hpp"

#include <ostream>
#include <string>

#include "ahfl/support/json.hpp"

namespace ahfl {
namespace {

class DurableStoreImportAdapterExecutionJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit DurableStoreImportAdapterExecutionJsonPrinter(std::ostream &out)
        : PrettyJsonWriter(out) {}

    void print(const durable_store_import::AdapterExecutionReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field(
                "source_durable_store_import_decision_receipt_persistence_response_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_decision_receipt_persistence_response_format_version);
                });
            field(
                "source_durable_store_import_decision_receipt_persistence_request_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_decision_receipt_persistence_request_format_version);
                });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(receipt.source_durable_store_import_request_format_version);
            });
            field("source_package_identity", [&]() {
                if (receipt.source_package_identity.has_value()) {
                    print_package_identity(*receipt.source_package_identity, 1);
                    return;
                }
                out_ << "null";
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() {
                if (receipt.run_id.has_value()) {
                    write_string(*receipt.run_id);
                    return;
                }
                out_ << "null";
            });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(receipt.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(receipt.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(receipt.durable_store_import_receipt_persistence_request_identity);
            });
            field("durable_store_import_receipt_persistence_response_identity", [&]() {
                write_string(receipt.durable_store_import_receipt_persistence_response_identity);
            });
            field("durable_store_import_adapter_execution_identity",
                  [&]() { write_string(receipt.durable_store_import_adapter_execution_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(receipt.planned_durable_identity); });
            field("response_status", [&]() { print_response_status(receipt.response_status); });
            field("response_outcome", [&]() { print_response_outcome(receipt.response_outcome); });
            field("response_boundary_kind",
                  [&]() { print_response_boundary_kind(receipt.response_boundary_kind); });
            field("acknowledged_for_response",
                  [&]() { out_ << (receipt.acknowledged_for_response ? "true" : "false"); });
            field("store_kind", [&]() { print_store_kind(receipt.store_kind); });
            field("local_fake_store_contract_version",
                  [&]() { write_string(receipt.local_fake_store_contract_version); });
            field("mutation_intent", [&]() { print_mutation_intent(receipt.mutation_intent, 1); });
            field("mutation_status", [&]() { print_mutation_status(receipt.mutation_status); });
            field("persistence_id", [&]() {
                if (receipt.persistence_id.has_value()) {
                    write_string(*receipt.persistence_id);
                    return;
                }
                out_ << "null";
            });
            field("failure_attribution", [&]() {
                if (receipt.failure_attribution.has_value()) {
                    print_failure_attribution(*receipt.failure_attribution, 1);
                    return;
                }
                out_ << "null";
            });
        });
        out_ << '\n';
    }

  private:
    void print_package_identity(const handoff::PackageIdentity &identity, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(identity.format_version); });
            field("name", [&]() { write_string(identity.name); });
            field("version", [&]() { write_string(identity.version); });
        });
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

    void print_response_boundary_kind(durable_store_import::PersistenceResponseBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
            write_string("local_contract_only");
            return;
        case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
            write_string("adapter_response_consumable");
            return;
        }
    }

    void print_store_kind(durable_store_import::AdapterExecutionStoreKind kind) {
        switch (kind) {
        case durable_store_import::AdapterExecutionStoreKind::LocalFakeDurableStore:
            write_string("local_fake_durable_store");
            return;
        }
    }

    void print_mutation_intent_kind(durable_store_import::StoreMutationIntentKind kind) {
        switch (kind) {
        case durable_store_import::StoreMutationIntentKind::PersistReceipt:
            write_string("persist_receipt");
            return;
        case durable_store_import::StoreMutationIntentKind::NoopBlocked:
            write_string("noop_blocked");
            return;
        case durable_store_import::StoreMutationIntentKind::NoopDeferred:
            write_string("noop_deferred");
            return;
        case durable_store_import::StoreMutationIntentKind::NoopRejected:
            write_string("noop_rejected");
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

    void print_capability(durable_store_import::AdapterCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
            write_string("consume_store_import_descriptor");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
            write_string("consume_export_manifest");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
            write_string("consume_persistence_descriptor");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
            write_string("consume_human_review_context");
            return;
        case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
            write_string("consume_checkpoint_record");
            return;
        case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
            write_string("preserve_partial_workflow_state");
            return;
        }
    }

    void print_failure_kind(durable_store_import::AdapterExecutionFailureKind kind) {
        switch (kind) {
        case durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked:
            write_string("source_response_blocked");
            return;
        case durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred:
            write_string("source_response_deferred");
            return;
        case durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected:
            write_string("source_response_rejected");
            return;
        }
    }

    void print_mutation_intent(const durable_store_import::StoreMutationIntent &intent,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_mutation_intent_kind(intent.kind); });
            field("source_response_identity",
                  [&]() { write_string(intent.source_response_identity); });
            field("target_receipt_identity",
                  [&]() { write_string(intent.target_receipt_identity); });
            field("target_planned_durable_identity",
                  [&]() { write_string(intent.target_planned_durable_identity); });
            field("mutates_store", [&]() { out_ << (intent.mutates_store ? "true" : "false"); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::AdapterExecutionFailureAttribution &failure, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("required_capability", [&]() {
                if (failure.required_capability.has_value()) {
                    print_capability(*failure.required_capability);
                    return;
                }
                out_ << "null";
            });
        });
    }
};

} // namespace

void print_durable_store_import_adapter_execution_json(
    const durable_store_import::AdapterExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportAdapterExecutionJsonPrinter(out).print(receipt);
}

} // namespace ahfl
