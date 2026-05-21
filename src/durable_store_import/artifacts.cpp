#include "ahfl/durable_store_import/artifacts.hpp"

#include "ahfl/durable_store_import/adapter_execution.hpp"
#include "ahfl/durable_store_import/decision.hpp"
#include "ahfl/durable_store_import/decision_review.hpp"
#include "ahfl/durable_store_import/provider.hpp"
#include "ahfl/durable_store_import/receipt.hpp"
#include "ahfl/durable_store_import/receipt_persistence.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response.hpp"
#include "ahfl/durable_store_import/receipt_persistence_response_review.hpp"
#include "ahfl/durable_store_import/receipt_persistence_review.hpp"
#include "ahfl/durable_store_import/receipt_review.hpp"
#include "ahfl/durable_store_import/recovery_preview.hpp"
#include "ahfl/durable_store_import/request.hpp"
#include "ahfl/durable_store_import/review.hpp"

#include "ahfl/support/json.hpp"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace ahfl::durable_store_import_artifacts_detail {

#define AHFL_ARTIFACT_PRINT_ENUM(value, ...)                                                       \
    do {                                                                                           \
        switch (value) { __VA_ARGS__; }                                                            \
    } while (false)

#define AHFL_ARTIFACT_ENUM_CASE(enumerator, wire_name)                                             \
    case enumerator:                                                                               \
        write_string(wire_name);                                                                   \
        return

#define AHFL_ARTIFACT_OSTREAM_ENUM_CASE(enumerator, wire_name)                                     \
    case enumerator:                                                                               \
        out << wire_name;                                                                          \
        return

class ArtifactJsonWriter : protected PrettyJsonWriter {
  protected:
    explicit ArtifactJsonWriter(std::ostream &out) : PrettyJsonWriter(out) {}

    using PrettyJsonWriter::print_array;
    using PrettyJsonWriter::print_object;
    using PrettyJsonWriter::write_string;

    void write_bool(bool value) {
        out_ << (value ? "true" : "false");
    }

    void write_null() {
        out_ << "null";
    }

    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        write_null();
    }

    template <typename Value, typename PrintValue>
    void print_optional(const std::optional<Value> &value, PrintValue print_value) {
        if (value.has_value()) {
            print_value(*value);
            return;
        }
        write_null();
    }
};

} // namespace ahfl::durable_store_import_artifacts_detail

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_adapter_execution {
namespace {

class DurableStoreImportAdapterExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportAdapterExecutionJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

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
                print_optional(receipt.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
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
                  [&]() { write_bool(receipt.acknowledged_for_response); });
            field("store_kind", [&]() { print_store_kind(receipt.store_kind); });
            field("local_fake_store_contract_version",
                  [&]() { write_string(receipt.local_fake_store_contract_version); });
            field("mutation_intent", [&]() { print_mutation_intent(receipt.mutation_intent, 1); });
            field("mutation_status", [&]() { print_mutation_status(receipt.mutation_status); });
            field("persistence_id", [&]() { print_optional_string(receipt.persistence_id); });
            field("failure_attribution", [&]() {
                print_optional(receipt.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
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

    void print_response_boundary_kind(durable_store_import::PersistenceResponseBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable,
                "adapter_response_consumable"));
    }

    void print_store_kind(durable_store_import::AdapterExecutionStoreKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionStoreKind::LocalFakeDurableStore,
                "local_fake_durable_store"));
    }

    void print_mutation_intent_kind(durable_store_import::StoreMutationIntentKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::PersistReceipt,
                                    "persist_receipt");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopBlocked,
                                    "noop_blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopDeferred,
                                    "noop_deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationIntentKind::NoopRejected,
                                    "noop_rejected"));
    }

    void print_mutation_status(durable_store_import::StoreMutationStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::Persisted,
                                    "persisted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::StoreMutationStatus::NotMutated,
                                    "not_mutated"));
    }

    void print_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_failure_kind(durable_store_import::AdapterExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked,
                "source_response_blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred,
                "source_response_deferred");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected,
                "source_response_rejected"));
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
            field("mutates_store", [&]() { write_bool(intent.mutates_store); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::AdapterExecutionFailureAttribution &failure, int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
            field("required_capability", [&]() {
                print_optional(failure.required_capability,
                               [&](const auto &value) { print_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_adapter_execution_json(
    const durable_store_import::AdapterExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportAdapterExecutionJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_adapter_execution

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision {

namespace {

class DurableStoreImportDecisionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Decision &decision) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(decision.format_version); });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(decision.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(decision.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(decision.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(decision.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(decision.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(decision.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(decision.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(decision.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(decision.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(decision.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(decision.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(decision.workflow_canonical_name); });
            field("session_id", [&]() { write_string(decision.session_id); });
            field("run_id", [&]() { print_optional_string(decision.run_id); });
            field("input_fixture", [&]() { write_string(decision.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(decision.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(decision.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(decision.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(decision.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(decision.descriptor_status); });
            field("request_status", [&]() { print_request_status(decision.request_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(decision.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(decision.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(decision.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(decision.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(decision.durable_store_import_decision_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(decision.planned_durable_identity); });
            field("request_boundary_kind",
                  [&]() { print_request_boundary_kind(decision.request_boundary_kind); });
            field("decision_boundary_kind",
                  [&]() { print_decision_boundary_kind(decision.decision_boundary_kind); });
            field("decision_status", [&]() { print_decision_status(decision.decision_status); });
            field("decision_outcome", [&]() { print_decision_outcome(decision.decision_outcome); });
            field("accepted_for_future_execution",
                  [&]() { write_bool(decision.accepted_for_future_execution); });
            field("next_required_adapter_capability", [&]() {
                print_optional(decision.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("decision_blocker", [&]() {
                print_optional(decision.decision_blocker,
                               [&](const auto &value) { print_decision_blocker(value, 1); });
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

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_boundary_kind(durable_store_import::RequestBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBoundaryKind::LocalIntentOnly,
                                    "local_intent_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBoundaryKind::AdapterContractConsumable,
                "adapter_contract_consumable"));
    }

    void print_decision_boundary_kind(durable_store_import::DecisionBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable,
                "adapter_decision_consumable"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_decision_outcome(durable_store_import::DecisionOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::AcceptRequest,
                                    "accept_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::BlockRequest,
                                    "block_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::DeferPartialRequest,
                                    "defer_partial_request");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionOutcome::RejectFailedRequest,
                                    "reject_failed_request"));
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_decision_blocker(const durable_store_import::DecisionBlocker &blocker,
                                int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::DecisionBlockerKind::SourceRequestBlocked:
                    write_string("source_request_blocked");
                    return;
                case durable_store_import::DecisionBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::DecisionBlockerKind::WorkflowFailure:
                    write_string("workflow_failure");
                    return;
                case durable_store_import::DecisionBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_decision_json(const durable_store_import::Decision &decision,
                                              std::ostream &out) {
    DurableStoreImportDecisionJsonPrinter(out).print(decision);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status) {
    switch (status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        return "ready_to_import";
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        return "blocked";
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string request_status_name(durable_store_import::RequestStatus status) {
    switch (status) {
    case durable_store_import::RequestStatus::ReadyForAdapter:
        return "ready_for_adapter";
    case durable_store_import::RequestStatus::Blocked:
        return "blocked";
    case durable_store_import::RequestStatus::TerminalCompleted:
        return "terminal_completed";
    case durable_store_import::RequestStatus::TerminalFailed:
        return "terminal_failed";
    case durable_store_import::RequestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string decision_status_name(durable_store_import::DecisionStatus status) {
    switch (status) {
    case durable_store_import::DecisionStatus::Accepted:
        return "accepted";
    case durable_store_import::DecisionStatus::Blocked:
        return "blocked";
    case durable_store_import::DecisionStatus::Deferred:
        return "deferred";
    case durable_store_import::DecisionStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string decision_outcome_name(durable_store_import::DecisionOutcome outcome) {
    switch (outcome) {
    case durable_store_import::DecisionOutcome::AcceptRequest:
        return "accept_request";
    case durable_store_import::DecisionOutcome::BlockRequest:
        return "block_request";
    case durable_store_import::DecisionOutcome::DeferPartialRequest:
        return "defer_partial_request";
    case durable_store_import::DecisionOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string
decision_boundary_kind_name(durable_store_import::DecisionBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::DecisionBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable:
        return "adapter_decision_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::DecisionReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::DecisionReviewNextActionKind::HandoffDurableStoreImportDecision:
        return "handoff_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::DecisionReviewNextActionKind::
        ArchiveCompletedDurableStoreImportDecision:
        return "archive_completed_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::
        PreservePartialDurableStoreImportDecision:
        return "preserve_partial_durable_store_import_decision";
    case durable_store_import::DecisionReviewNextActionKind::
        InvestigateDurableStoreImportDecisionRejection:
        return "investigate_durable_store_import_decision_rejection";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::DecisionBlockerKind kind) {
    switch (kind) {
    case durable_store_import::DecisionBlockerKind::SourceRequestBlocked:
        return "source_request_blocked";
    case durable_store_import::DecisionBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::DecisionBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case durable_store_import::DecisionBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_decision_blocker(std::ostream &out,
                            int indent_level,
                            const std::optional<durable_store_import::DecisionBlocker> &blocker) {
    line(out, indent_level, "decision_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_decision_review(
    const durable_store_import::DecisionReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_decision_format " + summary.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "manifest_status " + manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "request_status " + request_status_name(summary.request_status));
    line(out, 0, "decision_status " + decision_status_name(summary.decision_status));
    line(out,
         0,
         "decision_boundary_kind " + decision_boundary_kind_name(summary.decision_boundary_kind));
    line(out, 0, "decision_outcome " + decision_outcome_name(summary.decision_outcome));
    line(out,
         0,
         std::string("accepted_for_future_execution ") +
             (summary.accepted_for_future_execution ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out,
         0,
         "durable_store_import_decision_identity " +
             summary.durable_store_import_decision_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_contract " + summary.adapter_contract_summary);
    line(out, 0, "decision_preview " + summary.decision_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_decision_blocker(out, 0, summary.decision_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_decision_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_approval_receipt {

void print_durable_store_import_provider_approval_receipt_json(
    const durable_store_import::ApprovalReceipt &receipt, std::ostream &out) {
    out << receipt.format_version << '\n';
    out << "workflow " << receipt.workflow_canonical_name << '\n';
    out << "session " << receipt.session_id << '\n';
    if (receipt.run_id.has_value()) {
        out << "run_id " << *receipt.run_id << '\n';
    }
    out << "approval_request " << receipt.approval_request_identity << '\n';
    out << "approval_decision " << receipt.approval_decision_identity << '\n';
    out << "final_decision " << durable_store_import::to_string_view(receipt.final_decision)
        << '\n';
    out << "release_evidence_archive " << receipt.release_evidence_archive_manifest_identity
        << '\n';
    out << "production_readiness_evidence " << receipt.production_readiness_evidence_identity
        << '\n';
    out << "is_approved " << (receipt.is_approved ? "true" : "false") << '\n';
    out << "receipt_summary " << receipt.receipt_summary << '\n';
    out << "blocking_reason_summary " << receipt.blocking_reason_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_approval_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_capability_negotiation_review {
namespace {

void print_selection(durable_store_import::ProviderSelectionStatus status, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Selected,
                                        "selected");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSelectionStatus::FallbackSelected, "fallback_selected");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Blocked,
                                        "blocked"));
}

void print_negotiation(durable_store_import::ProviderCapabilityNegotiationStatus status,
                       std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Compatible, "compatible");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Degraded, "degraded");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCapabilityNegotiationStatus::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_capability_negotiation_review(
    const durable_store_import::ProviderCapabilityNegotiationReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "selection_plan " << review.durable_store_import_provider_selection_plan_identity
        << '\n';
    out << "selected_provider " << review.selected_provider_id << '\n';
    out << "selection_status ";
    print_selection(review.selection_status, out);
    out << '\n';
    out << "negotiation_status ";
    print_negotiation(review.negotiation_status, out);
    out << '\n';
    out << "summary " << review.negotiation_summary << '\n';
    out << "recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_capability_negotiation_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_compatibility_report {
namespace {

class CompatibilityReportJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit CompatibilityReportJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("compatibility_report_identity", [&]() {
                write_string(report.durable_store_import_provider_compatibility_report_identity);
            });
            field("status", [&]() { print_status(report.status); });
            field("mock_adapter_compatible", [&]() { write_bool(report.mock_adapter_compatible); });
            field("local_filesystem_alpha_compatible",
                  [&]() { write_bool(report.local_filesystem_alpha_compatible); });
            field("capability_matrix_complete",
                  [&]() { write_bool(report.capability_matrix_complete); });
            field("external_service_required",
                  [&]() { write_bool(report.external_service_required); });
            field("compatibility_summary", [&]() { write_string(report.compatibility_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderCompatibilityStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Passed,
                                    "passed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Failed,
                                    "failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCompatibilityStatus::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_compatibility_report_json(
    const durable_store_import::ProviderCompatibilityReport &report, std::ostream &out) {
    CompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_compatibility_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_compatibility_test_manifest {
namespace {

class ManifestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ManifestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderCompatibilityTestManifest &manifest) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(manifest.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(manifest.workflow_canonical_name); });
            field("session_id", [&]() { write_string(manifest.session_id); });
            field("run_id", [&]() { print_optional_string(manifest.run_id); });
            field("input_fixture", [&]() { write_string(manifest.input_fixture); });
            field("compatibility_test_manifest_identity", [&]() {
                write_string(
                    manifest.durable_store_import_provider_compatibility_test_manifest_identity);
            });
            field("includes_mock_adapter", [&]() { write_bool(manifest.includes_mock_adapter); });
            field("includes_local_filesystem_alpha",
                  [&]() { write_bool(manifest.includes_local_filesystem_alpha); });
            field("provider_count", [&]() { out_ << manifest.provider_count; });
            field("fixture_count", [&]() { out_ << manifest.fixture_count; });
            field("capability_matrix_reference",
                  [&]() { write_string(manifest.capability_matrix_reference); });
            field("external_service_required",
                  [&]() { write_bool(manifest.external_service_required); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_compatibility_test_manifest_json(
    const durable_store_import::ProviderCompatibilityTestManifest &manifest, std::ostream &out) {
    ManifestJsonPrinter(out).print(manifest);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_compatibility_test_manifest

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_bundle_validation_report {
namespace {

void print_validation_status(durable_store_import::ConfigValidationStatus status,
                             std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(status,
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Valid, "valid");
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Invalid, "invalid");
                             AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
                                 durable_store_import::ConfigValidationStatus::Missing, "missing"));
}

} // namespace

void print_durable_store_import_provider_config_bundle_validation_report(
    const durable_store_import::ProviderConfigBundleValidationReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "config_bundle " << report.config_bundle_identity << '\n';

    // Provider references
    for (const auto &ref : report.provider_references) {
        out << "provider_reference " << ref.provider_name << " ";
        print_validation_status(ref.status, out);
        if (ref.validation_error.has_value()) {
            out << " error=" << *ref.validation_error;
        }
        out << '\n';
    }

    // Secret handles（redacted 输出，不暴露 secret value）
    for (const auto &sh : report.secret_handles) {
        out << "secret_handle " << sh.secret_name << " scope=" << sh.secret_scope << " presence=";
        print_validation_status(sh.presence_status, out);
        out << " scope_status=";
        print_validation_status(sh.scope_status, out);
        out << " redaction=" << (sh.has_redaction_policy ? "yes" : "no") << '\n';
    }

    // Endpoint shapes
    for (const auto &ep : report.endpoint_shapes) {
        out << "endpoint_shape " << ep.endpoint_name << " expected=" << ep.expected_shape << " ";
        print_validation_status(ep.status, out);
        out << '\n';
    }

    // Environment bindings
    for (const auto &eb : report.environment_bindings) {
        out << "environment_binding " << eb.binding_name << " ";
        print_validation_status(eb.status, out);
        out << '\n';
    }

    // Summary
    out << "summary " << report.validation_summary << '\n';
    out << "blocking " << report.blocking_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_bundle_validation_report

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_load {
namespace {

class ProviderConfigLoadJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderConfigLoadJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderConfigLoadPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_sdk_adapter_interface_plan_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_sdk_adapter_interface_plan_format_version);
                });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_interface_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_interface_identity);
            });
            field("source_adapter_interface_status",
                  [&]() { print_interface_status(plan.source_adapter_interface_status); });
            field("provider_registry_identity",
                  [&]() { write_string(plan.provider_registry_identity); });
            field("adapter_descriptor_identity",
                  [&]() { write_string(plan.adapter_descriptor_identity); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("durable_store_import_provider_config_load_identity",
                  [&]() { write_string(plan.durable_store_import_provider_config_load_identity); });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("config_load_status", [&]() { print_status(plan.config_load_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(plan.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity",
                  [&]() { write_string(plan.provider_config_snapshot_placeholder_identity); });
            field("profile_descriptor", [&]() { print_profile(plan.profile_descriptor, 1); });
            print_side_effect_fields(field,
                                     plan.reads_secret_material,
                                     plan.materializes_secret_value,
                                     plan.materializes_credential_material,
                                     plan.opens_network_connection,
                                     plan.reads_host_environment,
                                     plan.writes_host_filesystem,
                                     plan.materializes_sdk_request_payload,
                                     plan.invokes_provider_sdk);
            field("secret_value", [&]() { print_optional_string(plan.secret_value); });
            field("credential_material",
                  [&]() { print_optional_string(plan.credential_material); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("remote_config_uri", [&]() { print_optional_string(plan.remote_config_uri); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    template <typename Field>
    void print_side_effect_fields(const Field &field,
                                  bool reads_secret_material,
                                  bool materializes_secret_value,
                                  bool materializes_credential_material,
                                  bool opens_network_connection,
                                  bool reads_host_environment,
                                  bool writes_host_filesystem,
                                  bool materializes_sdk_request_payload,
                                  bool invokes_provider_sdk) {
        field("reads_secret_material", [&]() { write_bool(reads_secret_material); });
        field("materializes_secret_value", [&]() { write_bool(materializes_secret_value); });
        field("materializes_credential_material",
              [&]() { write_bool(materializes_credential_material); });
        field("opens_network_connection", [&]() { write_bool(opens_network_connection); });
        field("reads_host_environment", [&]() { write_bool(reads_host_environment); });
        field("writes_host_filesystem", [&]() { write_bool(writes_host_filesystem); });
        field("materializes_sdk_request_payload",
              [&]() { write_bool(materializes_sdk_request_payload); });
        field("invokes_provider_sdk", [&]() { write_bool(invokes_provider_sdk); });
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
    }

    void print_status(durable_store_import::ProviderConfigStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                    "blocked"));
    }

    void print_operation(durable_store_import::ProviderConfigOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad:
            write_string("plan_provider_config_load");
            return;
        case durable_store_import::ProviderConfigOperationKind::
            PlanProviderConfigSnapshotPlaceholder:
            write_string("plan_provider_config_snapshot_placeholder");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady:
            write_string("noop_adapter_interface_not_ready");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady:
            write_string("noop_config_load_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderConfigFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady,
                "adapter_interface_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady,
                "config_load_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::MissingConfigProfile,
                "missing_config_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema,
                "incompatible_config_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile,
                "unsupported_config_profile"));
    }

    void print_profile(const durable_store_import::ProviderConfigProfileDescriptor &profile,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("profile_key", [&]() { write_string(profile.profile_key); });
            field("provider_key", [&]() { write_string(profile.provider_key); });
            field("config_schema_version", [&]() { write_string(profile.config_schema_version); });
            field("requires_secret_handle", [&]() { write_bool(profile.requires_secret_handle); });
            field("materializes_secret_value",
                  [&]() { write_bool(profile.materializes_secret_value); });
            field("contains_endpoint_uri", [&]() { write_bool(profile.contains_endpoint_uri); });
            field("opens_network_connection",
                  [&]() { write_bool(profile.opens_network_connection); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderConfigFailureAttribution> &failure,
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

void print_durable_store_import_provider_config_load_json(
    const durable_store_import::ProviderConfigLoadPlan &plan, std::ostream &out) {
    ProviderConfigLoadJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_load

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_readiness {
namespace {

void print_status(durable_store_import::ProviderConfigStatus status, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                        "blocked"));
}

void print_operation(durable_store_import::ProviderConfigOperationKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad,
            "plan_provider_config_load");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderConfigOperationKind::
                                            PlanProviderConfigSnapshotPlaceholder,
                                        "plan_provider_config_snapshot_placeholder");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady,
            "noop_adapter_interface_not_ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady,
            "noop_config_load_not_ready"));
}

void print_next_action(durable_store_import::ProviderConfigReadinessNextActionKind kind,
                       std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::
                ReadyForSecretHandleResolver,
            "ready_for_secret_handle_resolver");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::WaitForAdapterInterface,
            "wait_for_adapter_interface");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderConfigReadinessNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

void print_failure(const durable_store_import::ProviderConfigReadinessReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady:
        out << "adapter_interface_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady:
        out << "config_load_not_ready ";
        break;
    case durable_store_import::ProviderConfigFailureKind::MissingConfigProfile:
        out << "missing_config_profile ";
        break;
    case durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema:
        out << "incompatible_config_schema ";
        break;
    case durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile:
        out << "unsupported_config_profile ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_config_readiness(
    const durable_store_import::ProviderConfigReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "config_load " << review.durable_store_import_provider_config_load_identity << '\n';
    out << "config_snapshot " << review.durable_store_import_provider_config_snapshot_identity
        << '\n';
    out << "snapshot_status ";
    print_status(review.snapshot_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "profile " << review.provider_config_profile_identity << '\n';
    out << "snapshot_placeholder " << review.provider_config_snapshot_placeholder_identity << '\n';
    out << "config_schema " << review.config_schema_version << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " sdk_payload=" << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false") << '\n';
    out << "config_boundary " << review.config_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_config_snapshot {
namespace {

class ProviderConfigSnapshotJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderConfigSnapshotJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderConfigSnapshotPlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field("source_durable_store_import_provider_config_load_plan_format_version", [&]() {
                write_string(
                    placeholder
                        .source_durable_store_import_provider_config_load_plan_format_version);
            });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_config_load_identity", [&]() {
                write_string(placeholder.durable_store_import_provider_config_load_identity);
            });
            field("source_config_load_status",
                  [&]() { print_status(placeholder.source_config_load_status); });
            field("durable_store_import_provider_config_snapshot_identity", [&]() {
                write_string(placeholder.durable_store_import_provider_config_snapshot_identity);
            });
            field("operation_kind", [&]() { print_operation(placeholder.operation_kind); });
            field("snapshot_status", [&]() { print_status(placeholder.snapshot_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(placeholder.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity", [&]() {
                write_string(placeholder.provider_config_snapshot_placeholder_identity);
            });
            field("config_schema_version",
                  [&]() { write_string(placeholder.config_schema_version); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(placeholder.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(placeholder.materializes_credential_material); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(placeholder.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(placeholder.invokes_provider_sdk); });
            field("failure_attribution",
                  [&]() { print_failure(placeholder.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderConfigStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderConfigStatus::Blocked,
                                    "blocked"));
    }

    void print_operation(durable_store_import::ProviderConfigOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderConfigOperationKind::PlanProviderConfigLoad:
            write_string("plan_provider_config_load");
            return;
        case durable_store_import::ProviderConfigOperationKind::
            PlanProviderConfigSnapshotPlaceholder:
            write_string("plan_provider_config_snapshot_placeholder");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopAdapterInterfaceNotReady:
            write_string("noop_adapter_interface_not_ready");
            return;
        case durable_store_import::ProviderConfigOperationKind::NoopConfigLoadNotReady:
            write_string("noop_config_load_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderConfigFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady,
                "adapter_interface_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady,
                "config_load_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::MissingConfigProfile,
                "missing_config_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema,
                "incompatible_config_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile,
                "unsupported_config_profile"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderConfigFailureAttribution> &failure,
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

void print_durable_store_import_provider_config_snapshot_json(
    const durable_store_import::ProviderConfigSnapshotPlaceholder &placeholder, std::ostream &out) {
    ProviderConfigSnapshotJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_config_snapshot

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_conformance_report {
namespace {

// 辅助函数：输出检查结果
void print_result(durable_store_import::ConformanceCheckResult result, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        result,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Pass, "pass");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Fail, "fail");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ConformanceCheckResult::Skipped,
                                        "skipped"));
}

} // namespace

void print_durable_store_import_provider_conformance_report(
    const durable_store_import::ProviderConformanceReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "input_fixture " << report.input_fixture << '\n';
    out << "compatibility_report "
        << report.durable_store_import_provider_compatibility_report_identity << '\n';
    out << "registry " << report.durable_store_import_provider_registry_identity << '\n';
    out << "readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "pass_count " << report.pass_count << '\n';
    out << "fail_count " << report.fail_count << '\n';
    out << "skipped_count " << report.skipped_count << '\n';
    out << "checks " << report.checks.size() << '\n';
    for (const auto &check : report.checks) {
        out << "check " << check.check_name << ' ';
        print_result(check.result, out);
        out << ' ' << check.artifact_reference;
        if (check.failure_reason.has_value()) {
            out << " \"" << *check.failure_reason << '"';
        }
        out << '\n';
    }
    out << "summary " << report.conformance_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_conformance_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_driver_binding {
namespace {

class DurableStoreImportProviderDriverBindingJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderDriverBindingJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderDriverBindingPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_write_attempt_preview_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_write_attempt_preview_format_version);
            });
            field("source_durable_store_import_provider_adapter_config_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_adapter_config_format_version);
            });
            field("source_durable_store_import_provider_capability_matrix_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_capability_matrix_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_write_attempt_identity", [&]() {
                write_string(plan.durable_store_import_provider_write_attempt_identity);
            });
            field("durable_store_import_provider_driver_binding_identity", [&]() {
                write_string(plan.durable_store_import_provider_driver_binding_identity);
            });
            field("source_planning_status",
                  [&]() { print_planning_status(plan.source_planning_status); });
            field("provider_persistence_id",
                  [&]() { print_optional_string(plan.provider_persistence_id); });
            field("driver_profile", [&]() { print_driver_profile(plan.driver_profile, 1); });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("binding_status", [&]() { print_binding_status(plan.binding_status); });
            field("operation_descriptor_identity",
                  [&]() { print_optional_string(plan.operation_descriptor_identity); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_driver_kind(durable_store_import::ProviderDriverKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::ProviderNeutralPreviewDriver,
                "provider_neutral_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::AwsObjectStorePreviewDriver,
                "aws_object_store_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::GcpObjectStorePreviewDriver,
                "gcp_object_store_preview_driver");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverKind::AzureObjectStorePreviewDriver,
                "azure_object_store_preview_driver"));
    }

    void print_capability(durable_store_import::ProviderDriverCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile,
                "load_secret_free_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent,
                "translate_write_intent");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateRetryPlaceholder,
                "translate_retry_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverCapabilityKind::TranslateResumePlaceholder,
                "translate_resume_placeholder"));
    }

    void print_operation_kind(durable_store_import::ProviderDriverOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::TranslateProviderPersistReceipt,
                "translate_provider_persist_receipt");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopSourceNotPlanned,
                "noop_source_not_planned");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch,
                "noop_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverOperationKind::NoopUnsupportedDriverCapability,
                "noop_unsupported_driver_capability"));
    }

    void print_binding_status(durable_store_import::ProviderDriverBindingStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::Bound,
                                    "bound");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::NotBound,
                                    "not_bound"));
    }

    void print_planning_status(durable_store_import::ProviderWritePlanningStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::Planned,
                                    "planned");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderWritePlanningStatus::NotPlanned,
                                    "not_planned"));
    }

    void print_failure_kind(durable_store_import::ProviderDriverBindingFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingFailureKind::
                                        SourceWriteAttemptNotPlanned,
                                    "source_write_attempt_not_planned");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch,
                "driver_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderDriverBindingFailureKind::UnsupportedDriverCapability,
                "unsupported_driver_capability"));
    }

    void print_driver_profile(const durable_store_import::ProviderDriverProfile &profile,
                              int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(profile.format_version); });
            field("driver_kind", [&]() { print_driver_kind(profile.driver_kind); });
            field("driver_profile_identity",
                  [&]() { write_string(profile.driver_profile_identity); });
            field("provider_profile_ref", [&]() { write_string(profile.provider_profile_ref); });
            field("provider_namespace", [&]() { write_string(profile.provider_namespace); });
            field("secret_free_config_ref",
                  [&]() { write_string(profile.secret_free_config_ref); });
            field("credential_free", [&]() { write_bool(profile.credential_free); });
            field("supports_secret_free_profile_load",
                  [&]() { write_bool(profile.supports_secret_free_profile_load); });
            field("supports_write_intent_translation",
                  [&]() { write_bool(profile.supports_write_intent_translation); });
            field("supports_retry_placeholder_translation",
                  [&]() { write_bool(profile.supports_retry_placeholder_translation); });
            field("supports_resume_placeholder_translation",
                  [&]() { write_bool(profile.supports_resume_placeholder_translation); });
            field("credential_reference",
                  [&]() { print_optional_string(profile.credential_reference); });
            field("endpoint_uri", [&]() { print_optional_string(profile.endpoint_uri); });
            field("endpoint_secret_reference",
                  [&]() { print_optional_string(profile.endpoint_secret_reference); });
            field("object_path", [&]() { print_optional_string(profile.object_path); });
            field("database_table", [&]() { print_optional_string(profile.database_table); });
            field("sdk_payload_schema",
                  [&]() { print_optional_string(profile.sdk_payload_schema); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderDriverBindingFailureAttribution &failure,
        int indent_level) {
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

void print_durable_store_import_provider_driver_binding_json(
    const durable_store_import::ProviderDriverBindingPlan &plan, std::ostream &out) {
    DurableStoreImportProviderDriverBindingJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_driver_binding

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_driver_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
binding_status_name(durable_store_import::ProviderDriverBindingStatus status) {
    switch (status) {
    case durable_store_import::ProviderDriverBindingStatus::Bound:
        return "bound";
    case durable_store_import::ProviderDriverBindingStatus::NotBound:
        return "not_bound";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderDriverOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderDriverOperationKind::TranslateProviderPersistReceipt:
        return "translate_provider_persist_receipt";
    case durable_store_import::ProviderDriverOperationKind::NoopSourceNotPlanned:
        return "noop_source_not_planned";
    case durable_store_import::ProviderDriverOperationKind::NoopProfileMismatch:
        return "noop_profile_mismatch";
    case durable_store_import::ProviderDriverOperationKind::NoopUnsupportedDriverCapability:
        return "noop_unsupported_driver_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderDriverReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderDriverReadinessNextActionKind::ReadyForDriverImplementation:
        return "ready_for_driver_implementation";
    case durable_store_import::ProviderDriverReadinessNextActionKind::FixProviderProfile:
        return "fix_provider_profile";
    case durable_store_import::ProviderDriverReadinessNextActionKind::WaitForDriverCapability:
        return "wait_for_driver_capability";
    case durable_store_import::ProviderDriverReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderDriverCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderDriverCapabilityKind::LoadSecretFreeProfile:
        return "load_secret_free_profile";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateWriteIntent:
        return "translate_write_intent";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateRetryPlaceholder:
        return "translate_retry_placeholder";
    case durable_store_import::ProviderDriverCapabilityKind::TranslateResumePlaceholder:
        return "translate_resume_placeholder";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderDriverBindingFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderDriverBindingFailureKind::SourceWriteAttemptNotPlanned:
        return "source_write_attempt_not_planned";
    case durable_store_import::ProviderDriverBindingFailureKind::DriverProfileMismatch:
        return "driver_profile_mismatch";
    case durable_store_import::ProviderDriverBindingFailureKind::UnsupportedDriverCapability:
        return "unsupported_driver_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderDriverBindingFailureAttribution> &failure) {
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

void print_durable_store_import_provider_driver_readiness(
    const durable_store_import::ProviderDriverReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_driver_binding_plan_format " +
             review.source_durable_store_import_provider_driver_binding_plan_format_version);
    line(out,
         0,
         "source_provider_write_attempt_format " +
             review.source_durable_store_import_provider_write_attempt_preview_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_write_attempt_identity " +
             review.durable_store_import_provider_write_attempt_identity);
    line(out,
         0,
         "durable_store_import_provider_driver_binding_identity " +
             review.durable_store_import_provider_driver_binding_identity);
    line(out, 0, "binding_status " + binding_status_name(review.binding_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "operation_descriptor_identity " + (review.operation_descriptor_identity.has_value()
                                                 ? *review.operation_descriptor_identity
                                                 : std::string("none")));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "driver_boundary " + review.driver_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_driver_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_execution_audit_event {
namespace {

class AuditEventJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AuditEventJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderExecutionAuditEvent &event) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(event.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(event.workflow_canonical_name); });
            field("session_id", [&]() { write_string(event.session_id); });
            field("run_id", [&]() { print_optional_string(event.run_id); });
            field("input_fixture", [&]() { write_string(event.input_fixture); });
            field("execution_audit_event_identity", [&]() {
                write_string(event.durable_store_import_provider_execution_audit_event_identity);
            });
            field("event_name", [&]() { write_string(event.event_name); });
            field("outcome", [&]() { print_outcome(event.outcome); });
            field("redaction_policy_version",
                  [&]() { write_string(event.redaction_policy_version); });
            field("event_summary", [&]() { write_string(event.event_summary); });
            field("secret_free", [&]() { write_bool(event.secret_free); });
            field("raw_telemetry_persisted", [&]() { write_bool(event.raw_telemetry_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_outcome(durable_store_import::ProviderAuditOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                    "retry_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                    "recovery_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_execution_audit_event_json(
    const durable_store_import::ProviderExecutionAuditEvent &event, std::ostream &out) {
    AuditEventJsonPrinter(out).print(event);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_execution_audit_event

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_failure_taxonomy_report {
namespace {

class FailureTaxonomyJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit FailureTaxonomyJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderFailureTaxonomyReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("input_fixture", [&]() { write_string(report.input_fixture); });
            field("failure_taxonomy_report_identity", [&]() {
                write_string(report.durable_store_import_provider_failure_taxonomy_report_identity);
            });
            field("failure_kind", [&]() { print_kind(report.failure_kind); });
            field("failure_category", [&]() { print_category(report.failure_category); });
            field("retryability", [&]() { print_retryability(report.retryability); });
            field("operator_action", [&]() { print_action(report.operator_action); });
            field("security_sensitivity",
                  [&]() { print_sensitivity(report.security_sensitivity); });
            field("redacted_failure_summary",
                  [&]() { write_string(report.redacted_failure_summary); });
            field("recovery_review_failure_summary",
                  [&]() { write_string(report.recovery_review_failure_summary); });
            field("secret_bearing_error_persisted",
                  [&]() { write_bool(report.secret_bearing_error_persisted); });
            field("raw_provider_error_persisted",
                  [&]() { write_bool(report.raw_provider_error_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_kind(durable_store_import::ProviderFailureKindV1 kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authentication,
                                    "authentication");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authorization,
                                    "authorization");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Network,
                                    "network");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Throttling,
                                    "throttling");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Conflict,
                                    "conflict");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::SchemaMismatch,
                                    "schema_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::ProviderInternal,
                                    "provider_internal");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Unknown,
                                    "unknown");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Blocked,
                                    "blocked"));
    }

    void print_category(durable_store_import::ProviderFailureCategoryV1 category) {
        AHFL_ARTIFACT_PRINT_ENUM(
            category,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Security,
                                    "security");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Transport,
                                    "transport");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::RateLimit,
                                    "rate_limit");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Consistency,
                                    "consistency");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Contract,
                                    "contract");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Provider,
                                    "provider");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Unknown,
                                    "unknown");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureCategoryV1::Blocked,
                                    "blocked"));
    }

    void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            retryability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::NotApplicable,
                "not_applicable");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureRetryabilityV1::Retryable,
                                    "retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::NonRetryable, "non_retryable");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired,
                "duplicate_review_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureRetryabilityV1::Blocked,
                                    "blocked"));
    }

    void print_action(durable_store_import::ProviderFailureOperatorActionV1 action) {
        AHFL_ARTIFACT_PRINT_ENUM(
            action,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderFailureOperatorActionV1::None,
                                    "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::RotateCredentials,
                "rotate_credentials");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::GrantPermission,
                "grant_permission");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::RetryLater, "retry_later");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::InspectDuplicate,
                "inspect_duplicate");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::FixSchema, "fix_schema");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::EscalateProvider,
                "escalate_provider");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureOperatorActionV1::ManualReview,
                "manual_review"));
    }

    void print_sensitivity(durable_store_import::ProviderFailureSecuritySensitivityV1 sensitivity) {
        AHFL_ARTIFACT_PRINT_ENUM(
            sensitivity,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::SecretRelated,
                "secret_related");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::PermissionRelated,
                "permission_related");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::NonSensitive,
                "non_sensitive");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderFailureSecuritySensitivityV1::Unknown, "unknown"));
    }
};

} // namespace

void print_durable_store_import_provider_failure_taxonomy_report_json(
    const durable_store_import::ProviderFailureTaxonomyReport &report, std::ostream &out) {
    FailureTaxonomyJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_failure_taxonomy_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_failure_taxonomy_review {
namespace {

void print_kind(durable_store_import::ProviderFailureKindV1 kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::None, "none");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authentication,
                                        "authentication");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Authorization,
                                        "authorization");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Network,
                                        "network");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Throttling,
                                        "throttling");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Conflict,
                                        "conflict");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::SchemaMismatch,
                                        "schema_mismatch");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureKindV1::ProviderInternal, "provider_internal");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Unknown,
                                        "unknown");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderFailureKindV1::Blocked,
                                        "blocked"));
}

void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability,
                        std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        retryability,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::NotApplicable, "not_applicable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::Retryable, "retryable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::NonRetryable, "non_retryable");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired,
            "duplicate_review_required");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderFailureRetryabilityV1::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_failure_taxonomy_review(
    const durable_store_import::ProviderFailureTaxonomyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "taxonomy_report "
        << review.durable_store_import_provider_failure_taxonomy_report_identity << '\n';
    out << "failure_kind ";
    print_kind(review.failure_kind, out);
    out << '\n';
    out << "retryability ";
    print_retryability(review.retryability, out);
    out << '\n';
    out << "summary " << review.taxonomy_review_summary << '\n';
    out << "operator_recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_failure_taxonomy_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_fixture_matrix {
namespace {

class FixtureMatrixJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit FixtureMatrixJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderFixtureMatrix &matrix) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(matrix.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(matrix.workflow_canonical_name); });
            field("session_id", [&]() { write_string(matrix.session_id); });
            field("run_id", [&]() { print_optional_string(matrix.run_id); });
            field("input_fixture", [&]() { write_string(matrix.input_fixture); });
            field("fixture_matrix_identity", [&]() {
                write_string(matrix.durable_store_import_provider_fixture_matrix_identity);
            });
            field("covers_mock_adapter", [&]() { write_bool(matrix.covers_mock_adapter); });
            field("covers_local_filesystem_alpha",
                  [&]() { write_bool(matrix.covers_local_filesystem_alpha); });
            field("covers_success_fixture", [&]() { write_bool(matrix.covers_success_fixture); });
            field("covers_timeout_fixture", [&]() { write_bool(matrix.covers_timeout_fixture); });
            field("covers_conflict_fixture", [&]() { write_bool(matrix.covers_conflict_fixture); });
            field("covers_schema_mismatch_fixture",
                  [&]() { write_bool(matrix.covers_schema_mismatch_fixture); });
            field("provider_count", [&]() { out_ << matrix.provider_count; });
            field("fixture_count", [&]() { out_ << matrix.fixture_count; });
            field("external_service_required",
                  [&]() { write_bool(matrix.external_service_required); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_fixture_matrix_json(
    const durable_store_import::ProviderFixtureMatrix &matrix, std::ostream &out) {
    FixtureMatrixJsonPrinter(out).print(matrix);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_fixture_matrix

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_host_execution {
namespace {

class DurableStoreImportProviderHostExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderHostExecutionJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderHostExecutionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("source_durable_store_import_provider_sdk_envelope_policy_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_envelope_policy_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("source_host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.source_host_handoff_descriptor_identity); });
            field("source_envelope_status",
                  [&]() { print_envelope_status(plan.source_envelope_status); });
            field("host_execution_policy", [&]() { print_policy(plan.host_execution_policy, 1); });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("execution_status", [&]() { print_execution_status(plan.execution_status); });
            field("provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(plan.provider_host_execution_descriptor_identity);
            });
            field("provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(plan.provider_host_receipt_placeholder_identity);
            });
            field("starts_host_process", [&]() { write_bool(plan.starts_host_process); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_envelope_status(durable_store_import::ProviderSdkEnvelopeStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Blocked,
                                    "blocked"));
    }

    void print_capability(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
        switch (capability) {
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanLocalHostExecutionDescriptor:
            write_string("plan_local_host_execution_descriptor");
            return;
        case durable_store_import::ProviderHostExecutionCapabilityKind::
            PlanDryRunHostReceiptPlaceholder:
            write_string("plan_dry_run_host_receipt_placeholder");
            return;
        }
    }

    void print_operation_kind(durable_store_import::ProviderHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
            write_string("plan_provider_host_execution");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
            write_string("noop_sdk_handoff_not_ready");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
            write_string("noop_host_policy_mismatch");
            return;
        case durable_store_import::ProviderHostExecutionOperationKind::
            NoopUnsupportedHostCapability:
            write_string("noop_unsupported_host_capability");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderHostExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady,
                "sdk_handoff_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch,
                "host_policy_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability,
                "unsupported_host_capability"));
    }

    void print_policy(const durable_store_import::ProviderHostExecutionPolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("host_execution_policy_identity",
                  [&]() { write_string(policy.host_execution_policy_identity); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("execution_profile_ref", [&]() { write_string(policy.execution_profile_ref); });
            field("sandbox_policy_ref", [&]() { write_string(policy.sandbox_policy_ref); });
            field("timeout_policy_ref", [&]() { write_string(policy.timeout_policy_ref); });
            field("credential_free", [&]() { write_bool(policy.credential_free); });
            field("network_free", [&]() { write_bool(policy.network_free); });
            field("supports_local_host_execution_descriptor_planning", [&]() {
                out_ << (policy.supports_local_host_execution_descriptor_planning ? "true"
                                                                                  : "false");
            });
            field("supports_dry_run_host_receipt_placeholder_planning", [&]() {
                out_ << (policy.supports_dry_run_host_receipt_placeholder_planning ? "true"
                                                                                   : "false");
            });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("host_environment_secret",
                  [&]() { print_optional_string(policy.host_environment_secret); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderHostExecutionFailureAttribution &failure,
        int indent_level) {
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

void print_durable_store_import_provider_host_execution_json(
    const durable_store_import::ProviderHostExecutionPlan &plan, std::ostream &out) {
    DurableStoreImportProviderHostExecutionJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_host_execution

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_host_execution_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
execution_status_name(durable_store_import::ProviderHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderHostExecutionStatus::Ready:
        return "ready";
    case durable_store_import::ProviderHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionOperationKind::PlanProviderHostExecution:
        return "plan_provider_host_execution";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopSdkHandoffNotReady:
        return "noop_sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopHostPolicyMismatch:
        return "noop_host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionOperationKind::NoopUnsupportedHostCapability:
        return "noop_unsupported_host_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderHostExecutionReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::
        ReadyForLocalHostExecutionHarness:
        return "ready_for_local_host_execution_harness";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::FixHostPolicy:
        return "fix_host_policy";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::WaitForHostCapability:
        return "wait_for_host_capability";
    case durable_store_import::ProviderHostExecutionReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderHostExecutionCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanLocalHostExecutionDescriptor:
        return "plan_local_host_execution_descriptor";
    case durable_store_import::ProviderHostExecutionCapabilityKind::
        PlanDryRunHostReceiptPlaceholder:
        return "plan_dry_run_host_receipt_placeholder";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderHostExecutionFailureKind::SdkHandoffNotReady:
        return "sdk_handoff_not_ready";
    case durable_store_import::ProviderHostExecutionFailureKind::HostPolicyMismatch:
        return "host_policy_mismatch";
    case durable_store_import::ProviderHostExecutionFailureKind::UnsupportedHostCapability:
        return "unsupported_host_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderHostExecutionFailureAttribution> &failure) {
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

void print_durable_store_import_provider_host_execution_readiness(
    const durable_store_import::ProviderHostExecutionReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out,
         0,
         "durable_store_import_provider_host_execution_identity " +
             review.durable_store_import_provider_host_execution_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_host_execution_descriptor_identity " +
             (review.provider_host_execution_descriptor_identity.has_value()
                  ? *review.provider_host_execution_descriptor_identity
                  : std::string("none")));
    line(out,
         0,
         "provider_host_receipt_placeholder_identity " +
             (review.provider_host_receipt_placeholder_identity.has_value()
                  ? *review.provider_host_receipt_placeholder_identity
                  : std::string("none")));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "host_execution_boundary " + review.host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_host_execution_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_plan {
namespace {

class AlphaPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AlphaPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("local_filesystem_alpha_plan_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_filesystem_alpha_plan_identity);
            });
            field("plan_status", [&]() { print_status(plan.plan_status); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("real_provider_alpha", [&]() { write_bool(plan.real_provider_alpha); });
            field("fake_adapter_default_path_preserved",
                  [&]() { write_bool(plan.fake_adapter_default_path_preserved); });
            field("opt_in_required", [&]() { write_bool(plan.opt_in_required); });
            field("opt_in_enabled", [&]() { write_bool(plan.opt_in_enabled); });
            field("target_directory", [&]() { print_optional_string(plan.target_directory); });
            field("target_object_name", [&]() { write_string(plan.target_object_name); });
            field("planned_payload_digest", [&]() { write_string(plan.planned_payload_digest); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_failure_kind(durable_store_import::ProviderLocalFilesystemAlphaFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        MockReadinessNotReady,
                                    "mock_readiness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
                "alpha_plan_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
                "opt_in_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        FilesystemWriteFailed,
                                    "filesystem_write_failed"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalFilesystemAlphaFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_local_filesystem_alpha_plan_json(
    const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan, std::ostream &out) {
    AlphaPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_readiness {
namespace {

void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status,
                  std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready ? "ready"
                                                                                      : "blocked");
}

void print_next(durable_store_import::ProviderLocalFilesystemAlphaNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::
        ReadyForIdempotencyContract:
        out << "ready_for_idempotency_contract";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::WaitForMockAdapter:
        out << "wait_for_mock_adapter";
        return;
    case durable_store_import::ProviderLocalFilesystemAlphaNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_filesystem_alpha_readiness(
    const durable_store_import::ProviderLocalFilesystemAlphaReadiness &readiness,
    std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "alpha_result "
        << readiness.durable_store_import_provider_local_filesystem_alpha_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "summary " << readiness.readiness_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_filesystem_alpha_result {
namespace {

class AlphaResultJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit AlphaResultJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("local_filesystem_alpha_result_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_local_filesystem_alpha_result_identity);
            });
            field("result_status", [&]() { print_status(result.result_status); });
            field("normalized_result", [&]() { print_result(result.normalized_result); });
            field("provider_commit_reference",
                  [&]() { write_string(result.provider_commit_reference); });
            field("provider_result_digest", [&]() { write_string(result.provider_result_digest); });
            field("wrote_local_file", [&]() { write_bool(result.wrote_local_file); });
            field("opt_in_used", [&]() { write_bool(result.opt_in_used); });
            field("failure_attribution", [&]() { print_failure(result.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_result(durable_store_import::ProviderLocalFilesystemAlphaResultKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::DryRunOnly,
                "dry_run_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::WriteFailed,
                "write_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaResultKind::Blocked, "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalFilesystemAlphaFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        MockReadinessNotReady,
                                    "mock_readiness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady,
                "alpha_plan_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalFilesystemAlphaFailureKind::OptInRequired,
                "opt_in_required");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalFilesystemAlphaFailureKind::
                                        FilesystemWriteFailed,
                                    "filesystem_write_failed"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalFilesystemAlphaFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_local_filesystem_alpha_result_json(
    const durable_store_import::ProviderLocalFilesystemAlphaResult &result, std::ostream &out) {
    AlphaResultJsonPrinter(out).print(result);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_filesystem_alpha_result

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_execution_receipt {
namespace {

class DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter final
    : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostExecutionReceipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    receipt
                        .source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_sdk_request_envelope_plan_format_version",
                [&]() {
                    write_string(
                        receipt
                            .source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(receipt.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(receipt.durable_store_import_provider_host_execution_identity);
            });
            field("source_host_execution_status",
                  [&]() { print_host_execution_status(receipt.source_host_execution_status); });
            field("source_provider_host_execution_descriptor_identity", [&]() {
                print_optional_string(receipt.source_provider_host_execution_descriptor_identity);
            });
            field("source_provider_host_receipt_placeholder_identity", [&]() {
                print_optional_string(receipt.source_provider_host_receipt_placeholder_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    receipt.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(receipt.operation_kind); });
            field("execution_status", [&]() { print_execution_status(receipt.execution_status); });
            field("provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(receipt.provider_local_host_execution_receipt_identity);
            });
            field("starts_host_process", [&]() { write_bool(receipt.starts_host_process); });
            field("reads_host_environment", [&]() { write_bool(receipt.reads_host_environment); });
            field("opens_network_connection",
                  [&]() { write_bool(receipt.opens_network_connection); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(receipt.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(receipt.invokes_provider_sdk); });
            field("writes_host_filesystem", [&]() { write_bool(receipt.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(receipt.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_host_execution_status(durable_store_import::ProviderHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            SimulateProviderLocalHostExecutionReceipt:
            write_string("simulate_provider_local_host_execution_receipt");
            return;
        case durable_store_import::ProviderLocalHostExecutionOperationKind::
            NoopHostExecutionNotReady:
            write_string("noop_host_execution_not_ready");
            return;
        }
    }

    void print_execution_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady,
                "simulated_ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady,
                "host_execution_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderLocalHostExecutionFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_json(
    const durable_store_import::ProviderLocalHostExecutionReceipt &receipt, std::ostream &out) {
    DurableStoreImportProviderLocalHostExecutionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_execution_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_execution_receipt_review {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
execution_status_name(durable_store_import::ProviderLocalHostExecutionStatus status) {
    switch (status) {
    case durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady:
        return "simulated_ready";
    case durable_store_import::ProviderLocalHostExecutionStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderLocalHostExecutionOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionOperationKind::
        SimulateProviderLocalHostExecutionReceipt:
        return "simulate_provider_local_host_execution_receipt";
    case durable_store_import::ProviderLocalHostExecutionOperationKind::NoopHostExecutionNotReady:
        return "noop_host_execution_not_ready";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(
    durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ReadyForProviderSdkAdapterPrototype:
        return "ready_for_provider_sdk_adapter_prototype";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        WaitForHostExecutionPlan:
        return "wait_for_host_execution_plan";
    case durable_store_import::ProviderLocalHostExecutionReceiptReviewNextActionKind::
        ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderLocalHostExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostExecutionFailureKind::HostExecutionNotReady:
        return "host_execution_not_ready";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderLocalHostExecutionFailureAttribution>
        &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_local_host_execution_receipt_review(
    const durable_store_import::ProviderLocalHostExecutionReceiptReview &review,
    std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_local_host_execution_receipt_format " +
             review
                 .source_durable_store_import_provider_local_host_execution_receipt_format_version);
    line(out,
         0,
         "source_provider_host_execution_plan_format " +
             review.source_durable_store_import_provider_host_execution_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out,
         0,
         "durable_store_import_provider_host_execution_identity " +
             review.durable_store_import_provider_host_execution_identity);
    line(out,
         0,
         "durable_store_import_provider_local_host_execution_receipt_identity " +
             review.durable_store_import_provider_local_host_execution_receipt_identity);
    line(out, 0, "execution_status " + execution_status_name(review.execution_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_local_host_execution_receipt_identity " +
             (review.provider_local_host_execution_receipt_identity.has_value()
                  ? *review.provider_local_host_execution_receipt_identity
                  : std::string("none")));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "local_host_execution_boundary " + review.local_host_execution_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_execution_receipt_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_record {
namespace {

class HarnessRecordJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit HarnessRecordJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(record.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(record.workflow_canonical_name); });
            field("session_id", [&]() { write_string(record.session_id); });
            field("run_id", [&]() { print_optional_string(record.run_id); });
            field("input_fixture", [&]() { write_string(record.input_fixture); });
            field("harness_request_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("harness_record_identity", [&]() {
                write_string(
                    record.durable_store_import_provider_local_host_harness_record_identity);
            });
            field("operation_kind", [&]() { print_operation(record.operation_kind); });
            field("record_status", [&]() { print_status(record.record_status); });
            field("outcome_kind", [&]() { print_outcome(record.outcome_kind); });
            field("exit_code", [&]() { out_ << record.exit_code; });
            field("timed_out", [&]() { write_bool(record.timed_out); });
            field("sandbox_denied", [&]() { write_bool(record.sandbox_denied); });
            field("captured_diagnostic_summary",
                  [&]() { write_string(record.captured_diagnostic_summary); });
            field("captured_stdout_excerpt",
                  [&]() { print_optional_string(record.captured_stdout_excerpt); });
            field("captured_stderr_excerpt",
                  [&]() { print_optional_string(record.captured_stderr_excerpt); });
            field("opens_network_connection",
                  [&]() { write_bool(record.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(record.reads_secret_material); });
            field("writes_host_filesystem", [&]() { write_bool(record.writes_host_filesystem); });
            field("failure_attribution", [&]() { print_failure(record.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded, "succeeded");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut, "timed_out");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied,
                "sandbox_denied");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked, "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
                "secret_policy_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
                "harness_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_local_host_harness_record_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record,
    std::ostream &out) {
    HarnessRecordJsonPrinter(out).print(record);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_record

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_request {
namespace {

class HarnessRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit HarnessRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("secret_response_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_secret_resolver_response_identity);
            });
            field("harness_request_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("operation_kind", [&]() { print_operation(request.operation_kind); });
            field("request_status", [&]() { print_status(request.request_status); });
            field("sandbox_policy", [&]() { print_sandbox(request.sandbox_policy, 1); });
            field("timeout_milliseconds", [&]() { out_ << request.timeout_milliseconds; });
            field("fixture_kind", [&]() { print_fixture(request.fixture_kind); });
            field("opens_network_connection",
                  [&]() { write_bool(request.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(request.reads_secret_material); });
            field("writes_host_filesystem", [&]() { write_bool(request.writes_host_filesystem); });
            field("failure_attribution", [&]() { print_failure(request.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_fixture(durable_store_import::ProviderLocalHostHarnessFixtureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFixtureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady,
                "secret_policy_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady,
                "harness_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit,
                "nonzero_exit");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied,
                "sandbox_denied"));
    }

    void print_sandbox(const durable_store_import::ProviderLocalHostHarnessSandboxPolicy &policy,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("test_only", [&]() { write_bool(policy.test_only); });
            field("allow_network", [&]() { write_bool(policy.allow_network); });
            field("allow_secret", [&]() { write_bool(policy.allow_secret); });
            field("allow_filesystem_write", [&]() { write_bool(policy.allow_filesystem_write); });
            field("allow_host_environment", [&]() { write_bool(policy.allow_host_environment); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_local_host_harness_request_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request,
    std::ostream &out) {
    HarnessRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_local_host_harness_review {
namespace {

void print_status(durable_store_import::ProviderLocalHostHarnessStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderLocalHostHarnessStatus::Ready ? "ready"
                                                                                  : "blocked");
}

void print_outcome(durable_store_import::ProviderLocalHostHarnessOutcomeKind kind,
                   std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Succeeded, "succeeded");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Failed, "failed");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::TimedOut, "timed_out");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::SandboxDenied,
            "sandbox_denied");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderLocalHostHarnessOutcomeKind::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderLocalHostHarnessNextActionKind kind,
                std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::
        ReadyForSdkPayloadMaterialization:
        out << "ready_for_sdk_payload_materialization";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::WaitForSecretPolicy:
        out << "wait_for_secret_policy";
        return;
    case durable_store_import::ProviderLocalHostHarnessNextActionKind::ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_local_host_harness_review(
    const durable_store_import::ProviderLocalHostHarnessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "harness_record "
        << review.durable_store_import_provider_local_host_harness_record_identity << '\n';
    out << "record_status ";
    print_status(review.record_status, out);
    out << '\n';
    out << "outcome ";
    print_outcome(review.outcome_kind, out);
    out << '\n';
    out << "sandbox test_only=" << (review.sandbox_policy.test_only ? "true" : "false")
        << " network=" << (review.sandbox_policy.allow_network ? "true" : "false")
        << " secret=" << (review.sandbox_policy.allow_secret ? "true" : "false")
        << " filesystem_write=" << (review.sandbox_policy.allow_filesystem_write ? "true" : "false")
        << " host_env=" << (review.sandbox_policy.allow_host_environment ? "true" : "false")
        << '\n';
    out << "summary " << review.harness_boundary_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_local_host_harness_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_operator_review_event {
namespace {

void print_outcome(durable_store_import::ProviderAuditOutcome outcome, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        outcome,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success,
                                        "success");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure,
                                        "failure");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                        "retry_pending");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                        "recovery_pending");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                        "blocked"));
}

void print_next(durable_store_import::ProviderAuditNextActionKind action, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        action,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderAuditNextActionKind::Archive,
                                        "archive");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::RetryWithIdempotency,
            "retry_with_idempotency");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::RecoveryReview, "recovery_review");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderAuditNextActionKind::OperatorReview, "operator_review"));
}

} // namespace

void print_durable_store_import_provider_operator_review_event(
    const durable_store_import::ProviderOperatorReviewEvent &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "telemetry_summary " << review.durable_store_import_provider_telemetry_summary_identity
        << '\n';
    out << "outcome ";
    print_outcome(review.outcome, out);
    out << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "summary " << review.operator_review_summary << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_operator_review_event

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_opt_in_decision_report {

void print_durable_store_import_provider_opt_in_decision_report(
    const durable_store_import::ProviderOptInDecisionReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    for (const auto &gate : report.gate_checks) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        if (gate.denial_reason.has_value()) {
            out << " reason=" << durable_store_import::to_string_view(*gate.denial_reason);
        }
        out << '\n';
    }
    if (report.scoped_override.has_value()) {
        out << "override_scope " << report.scoped_override->override_scope << '\n';
        out << "override_approver " << report.scoped_override->override_approver << '\n';
        out << "override_valid " << (report.scoped_override->is_valid ? "true" : "false") << '\n';
    }
    out << "is_real_provider_traffic_allowed "
        << (report.is_real_provider_traffic_allowed ? "true" : "false") << '\n';
    out << "decision_summary " << report.decision_summary << '\n';
    out << "denial_reason_summary " << report.denial_reason_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_opt_in_decision_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_integration_dry_run_report {

void print_durable_store_import_provider_production_integration_dry_run_report(
    const durable_store_import::ProviderProductionIntegrationDryRunReport &report,
    std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "conformance_report " << report.conformance_report_identity << '\n';
    out << "schema_compatibility_report " << report.schema_compatibility_report_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "release_evidence_archive_manifest " << report.release_evidence_archive_manifest_identity
        << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "runtime_policy_report " << report.runtime_policy_report_identity << '\n';
    out << "total_evidence_count " << report.total_evidence_count << '\n';
    out << "valid_evidence_count " << report.valid_evidence_count << '\n';
    out << "invalid_evidence_count " << report.invalid_evidence_count << '\n';
    out << "missing_evidence_count " << report.missing_evidence_count << '\n';
    for (const auto &item : report.evidence_chain) {
        out << "evidence " << item.evidence_type
            << " present=" << (item.is_present ? "true" : "false")
            << " valid=" << (item.is_valid ? "true" : "false")
            << " fresh=" << (item.is_fresh ? "true" : "false") << '\n';
    }
    out << "readiness_state " << durable_store_import::to_string_view(report.readiness_state)
        << '\n';
    out << "blocking_item_count " << report.blocking_item_count << '\n';
    for (const auto &blocker : report.blocking_items) {
        out << "blocker " << blocker.block_type << " reason=" << blocker.block_reason << '\n';
    }
    for (const auto &action : report.next_operator_actions) {
        out << "next_action priority=" << action.priority << " type=" << action.action_type << " "
            << action.action_description << '\n';
    }
    out << "is_ready_for_controlled_rollout "
        << (report.is_ready_for_controlled_rollout ? "true" : "false") << '\n';
    out << "is_non_mutating_mode " << (report.is_non_mutating_mode ? "true" : "false") << '\n';
    out << "dry_run_summary " << report.dry_run_summary << '\n';
    out << "blocking_summary " << report.blocking_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_integration_dry_run_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_evidence {
namespace {

class EvidenceJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit EvidenceJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessEvidence &evidence) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(evidence.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(evidence.workflow_canonical_name); });
            field("session_id", [&]() { write_string(evidence.session_id); });
            field("run_id", [&]() { print_optional_string(evidence.run_id); });
            field("input_fixture", [&]() { write_string(evidence.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    evidence.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("security_evidence_passed",
                  [&]() { write_bool(evidence.security_evidence_passed); });
            field("recovery_evidence_passed",
                  [&]() { write_bool(evidence.recovery_evidence_passed); });
            field("compatibility_evidence_passed",
                  [&]() { write_bool(evidence.compatibility_evidence_passed); });
            field("observability_evidence_passed",
                  [&]() { write_bool(evidence.observability_evidence_passed); });
            field("registry_evidence_passed",
                  [&]() { write_bool(evidence.registry_evidence_passed); });
            field("blocking_issue_count", [&]() { out_ << evidence.blocking_issue_count; });
            field("evidence_summary", [&]() { write_string(evidence.evidence_summary); });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_production_readiness_evidence_json(
    const durable_store_import::ProviderProductionReadinessEvidence &evidence, std::ostream &out) {
    EvidenceJsonPrinter(out).print(evidence);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_evidence

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_report {
namespace {

void print_gate(durable_store_import::ProviderProductionReleaseGate gate, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        gate,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview,
            "ready_for_production_review");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::Conditional, "conditional");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderProductionReleaseGate::Blocked, "blocked"));
}

} // namespace

void print_durable_store_import_provider_production_readiness_report(
    const durable_store_import::ProviderProductionReadinessReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    out << "production_readiness_evidence "
        << report.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "release_gate ";
    print_gate(report.release_gate, out);
    out << '\n';
    out << "summary " << report.operator_report_summary << '\n';
    out << "next_step " << report.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_production_readiness_review {
namespace {

class ReadinessReviewJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ReadinessReviewJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessReview &review) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(review.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(review.workflow_canonical_name); });
            field("session_id", [&]() { write_string(review.session_id); });
            field("run_id", [&]() { print_optional_string(review.run_id); });
            field("input_fixture", [&]() { write_string(review.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    review.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("release_gate", [&]() { print_gate(review.release_gate); });
            field("blocking_issue_count", [&]() { out_ << review.blocking_issue_count; });
            field("blocking_issue_summary", [&]() { write_string(review.blocking_issue_summary); });
            field("readiness_summary", [&]() { write_string(review.readiness_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_gate(durable_store_import::ProviderProductionReleaseGate gate) {
        AHFL_ARTIFACT_PRINT_ENUM(
            gate,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview,
                "ready_for_production_review");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderProductionReleaseGate::Conditional, "conditional");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderProductionReleaseGate::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_production_readiness_review_json(
    const durable_store_import::ProviderProductionReadinessReview &review, std::ostream &out) {
    ReadinessReviewJsonPrinter(out).print(review);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_production_readiness_review

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

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_registry {
namespace {

class RegistryJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit RegistryJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderRegistry &registry) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(registry.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(registry.workflow_canonical_name); });
            field("session_id", [&]() { write_string(registry.session_id); });
            field("run_id", [&]() { print_optional_string(registry.run_id); });
            field("input_fixture", [&]() { write_string(registry.input_fixture); });
            field("registry_identity", [&]() {
                write_string(registry.durable_store_import_provider_registry_identity);
            });
            field("primary_provider_id", [&]() { write_string(registry.primary_provider_id); });
            field("fallback_provider_id", [&]() { write_string(registry.fallback_provider_id); });
            field("mock_adapter_registered",
                  [&]() { write_bool(registry.mock_adapter_registered); });
            field("local_filesystem_alpha_registered",
                  [&]() { write_bool(registry.local_filesystem_alpha_registered); });
            field("unaudited_fallback_allowed",
                  [&]() { write_bool(registry.unaudited_fallback_allowed); });
            field("registered_provider_count",
                  [&]() { out_ << registry.registered_provider_count; });
        });
        out_ << '\n';
    }

  private:
};

} // namespace

void print_durable_store_import_provider_registry_json(
    const durable_store_import::ProviderRegistry &registry, std::ostream &out) {
    RegistryJsonPrinter(out).print(registry);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_registry

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_release_evidence_archive_manifest {

void print_durable_store_import_provider_release_evidence_archive_manifest(
    const durable_store_import::ReleaseEvidenceArchiveManifest &manifest, std::ostream &out) {
    out << manifest.format_version << '\n';
    out << "workflow " << manifest.workflow_canonical_name << '\n';
    out << "session " << manifest.session_id << '\n';
    if (manifest.run_id.has_value()) {
        out << "run_id " << *manifest.run_id << '\n';
    }
    out << "conformance_report "
        << manifest.durable_store_import_provider_conformance_report_identity << '\n';
    out << "schema_compatibility_report "
        << manifest.durable_store_import_provider_schema_compatibility_report_identity << '\n';
    out << "config_bundle_validation_report "
        << manifest.durable_store_import_provider_config_bundle_validation_report_identity << '\n';
    out << "production_readiness_evidence "
        << manifest.durable_store_import_provider_production_readiness_evidence_identity << '\n';
    out << "total_evidence_count " << manifest.total_evidence_count << '\n';
    out << "present_evidence_count " << manifest.present_evidence_count << '\n';
    out << "valid_evidence_count " << manifest.valid_evidence_count << '\n';
    out << "missing_evidence_count " << manifest.missing_evidence_count << '\n';
    out << "invalid_evidence_count " << manifest.invalid_evidence_count << '\n';
    out << "is_release_ready " << (manifest.is_release_ready ? "true" : "false") << '\n';
    out << "evidence_items " << manifest.evidence_items.size() << '\n';
    for (const auto &item : manifest.evidence_items) {
        out << "evidence " << item.evidence_type << ' ' << item.evidence_identity << ' '
            << item.format_version << ' ' << (item.is_present ? "present" : "missing") << ' '
            << (item.is_valid ? "valid" : "invalid") << '\n';
    }
    out << "archive_summary " << manifest.archive_summary << '\n';
    out << "missing_evidence_summary " << manifest.missing_evidence_summary << '\n';
    out << "stale_evidence_summary " << manifest.stale_evidence_summary << '\n';
    out << "incompatible_evidence_summary " << manifest.incompatible_evidence_summary << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_release_evidence_archive_manifest

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_policy_report {

void print_durable_store_import_provider_runtime_policy_report(
    const durable_store_import::ProviderRuntimePolicyReport &report, std::ostream &out) {
    out << report.format_version << '\n';
    out << "workflow " << report.workflow_canonical_name << '\n';
    out << "session " << report.session_id << '\n';
    if (report.run_id.has_value()) {
        out << "run_id " << *report.run_id << '\n';
    }
    out << "opt_in_decision_report " << report.opt_in_decision_report_identity << '\n';
    out << "approval_receipt " << report.approval_receipt_identity << '\n';
    out << "config_validation_report " << report.config_validation_report_identity << '\n';
    out << "registry_selection_plan " << report.registry_selection_plan_identity << '\n';
    out << "production_readiness_evidence " << report.production_readiness_evidence_identity
        << '\n';
    out << "decision " << durable_store_import::to_string_view(report.decision) << '\n';
    out << "gates_passed " << report.gates_passed << '\n';
    out << "gates_failed " << report.gates_failed << '\n';
    out << "blocking_violation_count " << report.blocking_violation_count << '\n';
    out << "warning_violation_count " << report.warning_violation_count << '\n';
    for (const auto &gate : report.policy_gates) {
        out << "gate " << gate.gate_name << " " << (gate.passed ? "passed" : "failed");
        for (const auto &v : gate.violations) {
            out << " violation=" << durable_store_import::to_string_view(v);
        }
        out << '\n';
    }
    out << "is_execution_permitted " << (report.is_execution_permitted ? "true" : "false") << '\n';
    out << "policy_summary " << report.policy_summary << '\n';
    out << "violation_summary " << report.violation_summary << '\n';
    out << "next_operator_action " << report.next_operator_action << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_policy_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_preflight {
namespace {

class DurableStoreImportProviderRuntimePreflightJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderRuntimePreflightJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderRuntimePreflightPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_driver_binding_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_driver_binding_plan_format_version);
            });
            field("source_durable_store_import_provider_driver_profile_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_driver_profile_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_driver_binding_identity", [&]() {
                write_string(plan.durable_store_import_provider_driver_binding_identity);
            });
            field("provider_persistence_id",
                  [&]() { print_optional_string(plan.provider_persistence_id); });
            field("source_binding_status",
                  [&]() { print_binding_status(plan.source_binding_status); });
            field("source_operation_descriptor_identity",
                  [&]() { print_optional_string(plan.source_operation_descriptor_identity); });
            field("runtime_profile", [&]() { print_runtime_profile(plan.runtime_profile, 1); });
            field("durable_store_import_provider_runtime_preflight_identity", [&]() {
                write_string(plan.durable_store_import_provider_runtime_preflight_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("preflight_status", [&]() { print_preflight_status(plan.preflight_status); });
            field("sdk_invocation_envelope_identity",
                  [&]() { print_optional_string(plan.sdk_invocation_envelope_identity); });
            field("loads_runtime_config", [&]() { write_bool(plan.loads_runtime_config); });
            field("resolves_secret_handles", [&]() { write_bool(plan.resolves_secret_handles); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_binding_status(durable_store_import::ProviderDriverBindingStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::Bound,
                                    "bound");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderDriverBindingStatus::NotBound,
                                    "not_bound"));
    }

    void print_capability(durable_store_import::ProviderRuntimeCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile,
                "load_secret_free_runtime_profile");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder,
                "resolve_secret_handle_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder,
                "load_config_snapshot_placeholder");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope,
                "plan_sdk_invocation_envelope"));
    }

    void print_operation_kind(durable_store_import::ProviderRuntimeOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimeOperationKind::
                                        PlanProviderSdkInvocationEnvelope,
                                    "plan_provider_sdk_invocation_envelope");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeOperationKind::NoopDriverBindingNotReady,
                "noop_driver_binding_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch,
                "noop_runtime_profile_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimeOperationKind::
                                        NoopUnsupportedRuntimeCapability,
                                    "noop_unsupported_runtime_capability"));
    }

    void print_preflight_status(durable_store_import::ProviderRuntimePreflightStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderRuntimePreflightFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderRuntimePreflightFailureKind::DriverBindingNotReady:
            write_string("driver_binding_not_ready");
            return;
        case durable_store_import::ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch:
            write_string("runtime_profile_mismatch");
            return;
        case durable_store_import::ProviderRuntimePreflightFailureKind::
            UnsupportedRuntimeCapability:
            write_string("unsupported_runtime_capability");
            return;
        }
    }

    void print_runtime_profile(const durable_store_import::ProviderRuntimeProfile &profile,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(profile.format_version); });
            field("runtime_profile_identity",
                  [&]() { write_string(profile.runtime_profile_identity); });
            field("driver_profile_ref", [&]() { write_string(profile.driver_profile_ref); });
            field("provider_namespace", [&]() { write_string(profile.provider_namespace); });
            field("secret_free_runtime_config_ref",
                  [&]() { write_string(profile.secret_free_runtime_config_ref); });
            field("secret_resolver_policy_ref",
                  [&]() { write_string(profile.secret_resolver_policy_ref); });
            field("config_snapshot_policy_ref",
                  [&]() { write_string(profile.config_snapshot_policy_ref); });
            field("credential_free", [&]() { write_bool(profile.credential_free); });
            field("supports_secret_free_runtime_profile_load",
                  [&]() { write_bool(profile.supports_secret_free_runtime_profile_load); });
            field("supports_secret_handle_placeholder_resolution",
                  [&]() { write_bool(profile.supports_secret_handle_placeholder_resolution); });
            field("supports_config_snapshot_placeholder_load",
                  [&]() { write_bool(profile.supports_config_snapshot_placeholder_load); });
            field("supports_sdk_invocation_envelope_planning",
                  [&]() { write_bool(profile.supports_sdk_invocation_envelope_planning); });
            field("credential_reference",
                  [&]() { print_optional_string(profile.credential_reference); });
            field("secret_value", [&]() { print_optional_string(profile.secret_value); });
            field("secret_manager_endpoint_uri",
                  [&]() { print_optional_string(profile.secret_manager_endpoint_uri); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(profile.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(profile.object_path); });
            field("database_table", [&]() { print_optional_string(profile.database_table); });
            field("sdk_payload_schema",
                  [&]() { print_optional_string(profile.sdk_payload_schema); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(profile.sdk_request_payload); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderRuntimePreflightFailureAttribution &failure,
        int indent_level) {
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

void print_durable_store_import_provider_runtime_preflight_json(
    const durable_store_import::ProviderRuntimePreflightPlan &plan, std::ostream &out) {
    DurableStoreImportProviderRuntimePreflightJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_preflight

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_runtime_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
preflight_status_name(durable_store_import::ProviderRuntimePreflightStatus status) {
    switch (status) {
    case durable_store_import::ProviderRuntimePreflightStatus::Ready:
        return "ready";
    case durable_store_import::ProviderRuntimePreflightStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderRuntimeOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderRuntimeOperationKind::PlanProviderSdkInvocationEnvelope:
        return "plan_provider_sdk_invocation_envelope";
    case durable_store_import::ProviderRuntimeOperationKind::NoopDriverBindingNotReady:
        return "noop_driver_binding_not_ready";
    case durable_store_import::ProviderRuntimeOperationKind::NoopRuntimeProfileMismatch:
        return "noop_runtime_profile_mismatch";
    case durable_store_import::ProviderRuntimeOperationKind::NoopUnsupportedRuntimeCapability:
        return "noop_unsupported_runtime_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderRuntimeReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::
        ReadyForSdkAdapterImplementation:
        return "ready_for_sdk_adapter_implementation";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::FixRuntimeProfile:
        return "fix_runtime_profile";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::WaitForRuntimeCapability:
        return "wait_for_runtime_capability";
    case durable_store_import::ProviderRuntimeReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderRuntimeCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderRuntimeCapabilityKind::LoadSecretFreeRuntimeProfile:
        return "load_secret_free_runtime_profile";
    case durable_store_import::ProviderRuntimeCapabilityKind::ResolveSecretHandlePlaceholder:
        return "resolve_secret_handle_placeholder";
    case durable_store_import::ProviderRuntimeCapabilityKind::LoadConfigSnapshotPlaceholder:
        return "load_config_snapshot_placeholder";
    case durable_store_import::ProviderRuntimeCapabilityKind::PlanSdkInvocationEnvelope:
        return "plan_sdk_invocation_envelope";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderRuntimePreflightFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderRuntimePreflightFailureKind::DriverBindingNotReady:
        return "driver_binding_not_ready";
    case durable_store_import::ProviderRuntimePreflightFailureKind::RuntimeProfileMismatch:
        return "runtime_profile_mismatch";
    case durable_store_import::ProviderRuntimePreflightFailureKind::UnsupportedRuntimeCapability:
        return "unsupported_runtime_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderRuntimePreflightFailureAttribution>
        &failure) {
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

void print_durable_store_import_provider_runtime_readiness(
    const durable_store_import::ProviderRuntimeReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_runtime_preflight_plan_format " +
             review.source_durable_store_import_provider_runtime_preflight_plan_format_version);
    line(out,
         0,
         "source_provider_driver_binding_plan_format " +
             review.source_durable_store_import_provider_driver_binding_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_driver_binding_identity " +
             review.durable_store_import_provider_driver_binding_identity);
    line(out,
         0,
         "durable_store_import_provider_runtime_preflight_identity " +
             review.durable_store_import_provider_runtime_preflight_identity);
    line(out, 0, "preflight_status " + preflight_status_name(review.preflight_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "sdk_invocation_envelope_identity " + (review.sdk_invocation_envelope_identity.has_value()
                                                    ? *review.sdk_invocation_envelope_identity
                                                    : std::string("none")));
    line(out,
         0,
         "loads_runtime_config " + std::string(review.loads_runtime_config ? "true" : "false"));
    line(out,
         0,
         "resolves_secret_handles " +
             std::string(review.resolves_secret_handles ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "runtime_boundary " + review.runtime_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_runtime_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_schema_compatibility_report {
namespace {

class SchemaCompatibilityReportJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SchemaCompatibilityReportJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSchemaCompatibilityReport &report) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(report.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(report.workflow_canonical_name); });
            field("session_id", [&]() { write_string(report.session_id); });
            field("run_id", [&]() { print_optional_string(report.run_id); });
            field("version_checks", [&]() { print_version_checks(report.version_checks); });
            field("source_chain_checks",
                  [&]() { print_source_chain_checks(report.source_chain_checks); });
            field("reference_checks", [&]() { print_reference_checks(report.reference_checks); });
            field("compatible_count", [&]() { out_ << report.compatible_count; });
            field("incompatible_count", [&]() { out_ << report.incompatible_count; });
            field("unknown_count", [&]() { out_ << report.unknown_count; });
            field("compatibility_summary", [&]() { write_string(report.compatibility_summary); });
            field("has_schema_drift", [&]() { write_bool(report.has_schema_drift); });
            field("drift_details", [&]() { print_optional_string(report.drift_details); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::SchemaCompatibilityStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Compatible,
                                    "compatible");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Incompatible,
                                    "incompatible");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::SchemaCompatibilityStatus::Unknown,
                                    "unknown"));
    }

    void
    print_version_checks(const std::vector<durable_store_import::ArtifactVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("artifact_type", [&]() { write_string(check.artifact_type); });
                        field("artifact_identity",
                              [&]() { write_string(check.artifact_identity); });
                        field("format_version", [&]() { write_string(check.format_version); });
                        field("status", [&]() { print_status(check.status); });
                        field("expected_format_version",
                              [&]() { print_optional_string(check.expected_format_version); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void
    print_source_chain_checks(const std::vector<durable_store_import::SourceChainCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("source_artifact_type",
                              [&]() { write_string(check.source_artifact_type); });
                        field("source_artifact_identity",
                              [&]() { write_string(check.source_artifact_identity); });
                        field("source_format_version",
                              [&]() { write_string(check.source_format_version); });
                        field("expected_source_format_version",
                              [&]() { write_string(check.expected_source_format_version); });
                        field("is_compatible", [&]() { write_bool(check.is_compatible); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }

    void
    print_reference_checks(const std::vector<durable_store_import::ReferenceVersionCheck> &checks) {
        print_array(1, [&](const auto &item) {
            for (const auto &check : checks) {
                item([&]() {
                    print_object(2, [&](const auto &field) {
                        field("reference_type", [&]() { write_string(check.reference_type); });
                        field("reference_identity",
                              [&]() { write_string(check.reference_identity); });
                        field("referenced_format_version",
                              [&]() { write_string(check.referenced_format_version); });
                        field("expected_format_version",
                              [&]() { write_string(check.expected_format_version); });
                        field("is_compatible", [&]() { write_bool(check.is_compatible); });
                        field("incompatibility_reason",
                              [&]() { print_optional_string(check.incompatibility_reason); });
                    });
                });
            }
        });
    }
};

} // namespace

void print_durable_store_import_provider_schema_compatibility_report_json(
    const durable_store_import::ProviderSchemaCompatibilityReport &report, std::ostream &out) {
    SchemaCompatibilityReportJsonPrinter(out).print(report);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_schema_compatibility_report

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_interface {
namespace {

class ProviderSdkAdapterInterfaceJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterInterfaceJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterInterfacePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("source_sdk_adapter_request_status",
                  [&]() { print_adapter_status(plan.source_sdk_adapter_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(plan.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_interface_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_interface_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("interface_status", [&]() { print_interface_status(plan.interface_status); });
            field("provider_registry_identity",
                  [&]() { write_string(plan.provider_registry_identity); });
            field("adapter_descriptor_identity",
                  [&]() { write_string(plan.adapter_descriptor_identity); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("adapter_name", [&]() { write_string(plan.adapter_name); });
            field("adapter_version", [&]() { write_string(plan.adapter_version); });
            field("interface_abi_version", [&]() { write_string(plan.interface_abi_version); });
            field("capability_descriptor_identity",
                  [&]() { write_string(plan.capability_descriptor_identity); });
            field("capability_descriptor",
                  [&]() { print_capability_descriptor(plan.capability_descriptor, 1); });
            field("error_taxonomy_version", [&]() { write_string(plan.error_taxonomy_version); });
            field("normalized_error_kind",
                  [&]() { print_normalized_error_kind(plan.normalized_error_kind); });
            field("returns_placeholder_result",
                  [&]() { write_bool(plan.returns_placeholder_result); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("credential_reference",
                  [&]() { print_optional_string(plan.credential_reference); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(plan.sdk_response_payload); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_adapter_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            PlanProviderSdkAdapterInterface:
            write_string("plan_provider_sdk_adapter_interface");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
            NoopUnsupportedCapability:
            write_string("noop_unsupported_capability");
            return;
        }
    }

    void
    print_support_status(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported,
                "supported");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported,
                "unsupported"));
    }

    void
    print_normalized_error_kind(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None, "none");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability,
                "unsupported_capability");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterNormalizedErrorKind::
                                        SdkAdapterRequestNotReady,
                                    "sdk_adapter_request_not_ready"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterInterfaceFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::
            SdkAdapterRequestNotReady:
            write_string("sdk_adapter_request_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
            write_string("unsupported_capability");
            return;
        }
    }

    void print_capability_descriptor(
        const durable_store_import::ProviderSdkAdapterCapabilityDescriptor &descriptor,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("capability_key", [&]() { write_string(descriptor.capability_key); });
            field("support_status", [&]() { print_support_status(descriptor.support_status); });
            field("input_contract_format_version",
                  [&]() { write_string(descriptor.input_contract_format_version); });
            field("output_contract_format_version",
                  [&]() { write_string(descriptor.output_contract_format_version); });
            field("supports_placeholder_response",
                  [&]() { write_bool(descriptor.supports_placeholder_response); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterInterfaceFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_json(
    const durable_store_import::ProviderSdkAdapterInterfacePlan &plan, std::ostream &out) {
    ProviderSdkAdapterInterfaceJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_interface

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_interface_review {
namespace {

void print_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status,
                  std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready, "ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked, "blocked"));
}

void print_operation(durable_store_import::ProviderSdkAdapterInterfaceOperationKind kind,
                     std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        PlanProviderSdkAdapterInterface:
        out << "plan_provider_sdk_adapter_interface";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::
        NoopSdkAdapterRequestNotReady:
        out << "noop_sdk_adapter_request_not_ready";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceOperationKind::NoopUnsupportedCapability:
        out << "noop_unsupported_capability";
        return;
    }
}

void print_support(durable_store_import::ProviderSdkAdapterCapabilitySupportStatus status,
                   std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        status,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Supported,
            "supported");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterCapabilitySupportStatus::Unsupported,
            "unsupported"));
}

void print_error(durable_store_import::ProviderSdkAdapterNormalizedErrorKind kind,
                 std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::None, "none");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::UnsupportedCapability,
            "unsupported_capability");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkAdapterNormalizedErrorKind::SdkAdapterRequestNotReady,
            "sdk_adapter_request_not_ready"));
}

void print_next_action(durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind kind,
                       std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ReadyForMockAdapterImplementation:
        out << "ready_for_mock_adapter_implementation";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        WaitForSdkAdapterRequest:
        out << "wait_for_sdk_adapter_request";
        return;
    case durable_store_import::ProviderSdkAdapterInterfaceReviewNextActionKind::
        ManualReviewRequired:
        out << "manual_review_required";
        return;
    }
}

void print_failure(const durable_store_import::ProviderSdkAdapterInterfaceReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }

    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::SdkAdapterRequestNotReady:
        out << "sdk_adapter_request_not_ready ";
        break;
    case durable_store_import::ProviderSdkAdapterInterfaceFailureKind::UnsupportedCapability:
        out << "unsupported_capability ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_sdk_adapter_interface_review(
    const durable_store_import::ProviderSdkAdapterInterfaceReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "adapter_request " << review.durable_store_import_provider_sdk_adapter_request_identity
        << '\n';
    out << "interface_artifact "
        << review.durable_store_import_provider_sdk_adapter_interface_identity << '\n';
    out << "interface_status ";
    print_status(review.interface_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "registry " << review.provider_registry_identity << '\n';
    out << "adapter_descriptor " << review.adapter_descriptor_identity << '\n';
    out << "capability_descriptor " << review.capability_descriptor_identity << '\n';
    out << "capability_support ";
    print_support(review.capability_support_status, out);
    out << '\n';
    out << "error_taxonomy " << review.error_taxonomy_version << '\n';
    out << "normalized_error ";
    print_error(review.normalized_error_kind, out);
    out << '\n';
    out << "returns_placeholder_result " << (review.returns_placeholder_result ? "true" : "false")
        << '\n';
    out << "side_effects sdk_payload="
        << (review.materializes_sdk_request_payload ? "true" : "false")
        << " sdk_call=" << (review.invokes_provider_sdk ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " secret=" << (review.reads_secret_material ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false") << '\n';
    out << "interface_boundary " << review.interface_boundary_summary << '\n';
    out << "next_action ";
    print_next_action(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_interface_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string status_name(durable_store_import::ProviderSdkAdapterStatus status) {
    switch (status) {
    case durable_store_import::ProviderSdkAdapterStatus::Ready:
        return "ready";
    case durable_store_import::ProviderSdkAdapterStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderSdkAdapterOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
        return "plan_provider_sdk_adapter_request";
    case durable_store_import::ProviderSdkAdapterOperationKind::
        PlanProviderSdkAdapterResponsePlaceholder:
        return "plan_provider_sdk_adapter_response_placeholder";
    case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
        return "noop_local_host_execution_not_ready";
    case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
        return "noop_sdk_adapter_request_not_ready";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderSdkAdapterReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::
        ReadyForProviderSdkAdapterInterface:
        return "ready_for_provider_sdk_adapter_interface";
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::
        WaitForLocalHostExecutionReceipt:
        return "wait_for_local_host_execution_receipt";
    case durable_store_import::ProviderSdkAdapterReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderSdkAdapterFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady:
        return "local_host_execution_not_ready";
    case durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady:
        return "sdk_adapter_request_not_ready";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderSdkAdapterFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_provider_sdk_adapter_readiness(
    const durable_store_import::ProviderSdkAdapterReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(
        out,
        0,
        "source_provider_sdk_adapter_response_placeholder_format " +
            review
                .source_durable_store_import_provider_sdk_adapter_response_placeholder_format_version);
    line(out,
         0,
         "source_provider_sdk_adapter_request_plan_format " +
             review.source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_sdk_adapter_request_identity " +
             review.durable_store_import_provider_sdk_adapter_request_identity);
    line(out,
         0,
         "durable_store_import_provider_sdk_adapter_response_placeholder_identity " +
             review.durable_store_import_provider_sdk_adapter_response_placeholder_identity);
    line(out, 0, "response_status " + status_name(review.response_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_sdk_adapter_response_placeholder_identity " +
             (review.provider_sdk_adapter_response_placeholder_identity.has_value()
                  ? *review.provider_sdk_adapter_response_placeholder_identity
                  : std::string("none")));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "reads_secret_material " + std::string(review.reads_secret_material ? "true" : "false"));
    line(out,
         0,
         "reads_host_environment " + std::string(review.reads_host_environment ? "true" : "false"));
    line(out,
         0,
         "writes_host_filesystem " + std::string(review.writes_host_filesystem ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "sdk_adapter_boundary " + review.sdk_adapter_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_request {
namespace {

class ProviderSdkAdapterRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterRequestPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("source_durable_store_import_provider_host_execution_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_host_execution_plan_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("durable_store_import_provider_host_execution_identity", [&]() {
                write_string(plan.durable_store_import_provider_host_execution_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_local_host_execution_status",
                  [&]() { print_local_host_status(plan.source_local_host_execution_status); });
            field("source_provider_local_host_execution_receipt_identity", [&]() {
                print_optional_string(plan.source_provider_local_host_execution_receipt_identity);
            });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("request_status", [&]() { print_status(plan.request_status); });
            field("provider_sdk_adapter_request_identity",
                  [&]() { print_optional_string(plan.provider_sdk_adapter_request_identity); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(plan.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(plan.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(plan.object_path); });
            field("database_table", [&]() { print_optional_string(plan.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(plan.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(plan.sdk_response_payload); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_local_host_status(durable_store_import::ProviderLocalHostExecutionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderLocalHostExecutionStatus::SimulatedReady,
                "simulated_ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderLocalHostExecutionStatus::Blocked,
                                    "blocked"));
    }

    void print_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
            write_string("plan_provider_sdk_adapter_request");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::
            PlanProviderSdkAdapterResponsePlaceholder:
            write_string("plan_provider_sdk_adapter_response_placeholder");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
            write_string("noop_local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady,
                "local_host_execution_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady,
                "sdk_adapter_request_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_request_json(
    const durable_store_import::ProviderSdkAdapterRequestPlan &plan, std::ostream &out) {
    ProviderSdkAdapterRequestJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_adapter_response_placeholder {
namespace {

class ProviderSdkAdapterResponsePlaceholderJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit ProviderSdkAdapterResponsePlaceholderJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field("source_durable_store_import_provider_sdk_adapter_request_plan_format_version", [&]() {
                write_string(
                    placeholder
                        .source_durable_store_import_provider_sdk_adapter_request_plan_format_version);
            });
            field(
                "source_durable_store_import_provider_local_host_execution_receipt_format_version",
                [&]() {
                    write_string(
                        placeholder
                            .source_durable_store_import_provider_local_host_execution_receipt_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_sdk_adapter_request_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_local_host_execution_receipt_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_local_host_execution_receipt_identity);
            });
            field("source_request_status",
                  [&]() { print_status(placeholder.source_request_status); });
            field("source_provider_sdk_adapter_request_identity", [&]() {
                print_optional_string(placeholder.source_provider_sdk_adapter_request_identity);
            });
            field("durable_store_import_provider_sdk_adapter_response_placeholder_identity", [&]() {
                write_string(
                    placeholder
                        .durable_store_import_provider_sdk_adapter_response_placeholder_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(placeholder.operation_kind); });
            field("response_status", [&]() { print_status(placeholder.response_status); });
            field("provider_sdk_adapter_response_placeholder_identity", [&]() {
                print_optional_string(
                    placeholder.provider_sdk_adapter_response_placeholder_identity);
            });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(placeholder.materializes_sdk_request_payload); });
            field("invokes_provider_sdk", [&]() { write_bool(placeholder.invokes_provider_sdk); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("failure_attribution", [&]() {
                print_optional(placeholder.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkAdapterStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Ready, "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkAdapterStatus::Blocked,
                                    "blocked"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkAdapterOperationKind::PlanProviderSdkAdapterRequest:
            write_string("plan_provider_sdk_adapter_request");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::
            PlanProviderSdkAdapterResponsePlaceholder:
            write_string("plan_provider_sdk_adapter_response_placeholder");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopLocalHostExecutionNotReady:
            write_string("noop_local_host_execution_not_ready");
            return;
        case durable_store_import::ProviderSdkAdapterOperationKind::NoopSdkAdapterRequestNotReady:
            write_string("noop_sdk_adapter_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::LocalHostExecutionNotReady,
                "local_host_execution_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkAdapterFailureKind::SdkAdapterRequestNotReady,
                "sdk_adapter_request_not_ready"));
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkAdapterFailureAttribution &failure,
        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_failure_kind(failure.kind); });
            field("message", [&]() { write_string(failure.message); });
        });
    }
};

} // namespace

void print_durable_store_import_provider_sdk_adapter_response_placeholder_json(
    const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder,
    std::ostream &out) {
    ProviderSdkAdapterResponsePlaceholderJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_adapter_response_placeholder

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_envelope {
namespace {

class DurableStoreImportProviderSdkEnvelopeJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportProviderSdkEnvelopeJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkRequestEnvelopePlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("source_durable_store_import_provider_runtime_preflight_plan_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_preflight_plan_format_version);
            });
            field("source_durable_store_import_provider_runtime_profile_format_version", [&]() {
                write_string(
                    plan.source_durable_store_import_provider_runtime_profile_format_version);
            });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_runtime_preflight_identity", [&]() {
                write_string(plan.durable_store_import_provider_runtime_preflight_identity);
            });
            field("source_sdk_invocation_envelope_identity",
                  [&]() { print_optional_string(plan.source_sdk_invocation_envelope_identity); });
            field("source_preflight_status",
                  [&]() { print_preflight_status(plan.source_preflight_status); });
            field("envelope_policy", [&]() { print_policy(plan.envelope_policy, 1); });
            field("durable_store_import_provider_sdk_request_envelope_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_request_envelope_identity);
            });
            field("operation_kind", [&]() { print_operation_kind(plan.operation_kind); });
            field("envelope_status", [&]() { print_envelope_status(plan.envelope_status); });
            field("provider_sdk_request_envelope_identity",
                  [&]() { print_optional_string(plan.provider_sdk_request_envelope_identity); });
            field("host_handoff_descriptor_identity",
                  [&]() { print_optional_string(plan.host_handoff_descriptor_identity); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("starts_host_process", [&]() { write_bool(plan.starts_host_process); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("invokes_provider_sdk", [&]() { write_bool(plan.invokes_provider_sdk); });
            field("failure_attribution", [&]() {
                print_optional(plan.failure_attribution,
                               [&](const auto &value) { print_failure_attribution(value, 1); });
            });
        });
        out_ << '\n';
    }

  private:
    void print_preflight_status(durable_store_import::ProviderRuntimePreflightStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderRuntimePreflightStatus::Blocked,
                                    "blocked"));
    }

    void print_capability(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeCapabilityKind::
                                        PlanSecretFreeRequestEnvelope,
                                    "plan_secret_free_request_envelope");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor,
                "plan_host_handoff_descriptor"));
    }

    void print_operation_kind(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
            write_string("plan_provider_sdk_request_envelope");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
            write_string("noop_runtime_preflight_not_ready");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
            write_string("noop_envelope_policy_mismatch");
            return;
        case durable_store_import::ProviderSdkEnvelopeOperationKind::
            NoopUnsupportedEnvelopeCapability:
            write_string("noop_unsupported_envelope_capability");
            return;
        }
    }

    void print_envelope_status(durable_store_import::ProviderSdkEnvelopeStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Ready,
                                    "ready");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSdkEnvelopeStatus::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady,
                "runtime_preflight_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch,
                "envelope_policy_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability,
                "unsupported_envelope_capability"));
    }

    void print_policy(const durable_store_import::ProviderSdkEnvelopePolicy &policy,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("format_version", [&]() { write_string(policy.format_version); });
            field("sdk_envelope_policy_identity",
                  [&]() { write_string(policy.sdk_envelope_policy_identity); });
            field("runtime_profile_ref", [&]() { write_string(policy.runtime_profile_ref); });
            field("provider_namespace", [&]() { write_string(policy.provider_namespace); });
            field("secret_free_request_schema_ref",
                  [&]() { write_string(policy.secret_free_request_schema_ref); });
            field("host_handoff_policy_ref",
                  [&]() { write_string(policy.host_handoff_policy_ref); });
            field("credential_free", [&]() { write_bool(policy.credential_free); });
            field("supports_secret_free_request_envelope_planning",
                  [&]() { write_bool(policy.supports_secret_free_request_envelope_planning); });
            field("supports_host_handoff_descriptor_planning",
                  [&]() { write_bool(policy.supports_host_handoff_descriptor_planning); });
            field("credential_reference",
                  [&]() { print_optional_string(policy.credential_reference); });
            field("secret_value", [&]() { print_optional_string(policy.secret_value); });
            field("provider_endpoint_uri",
                  [&]() { print_optional_string(policy.provider_endpoint_uri); });
            field("object_path", [&]() { print_optional_string(policy.object_path); });
            field("database_table", [&]() { print_optional_string(policy.database_table); });
            field("sdk_request_payload",
                  [&]() { print_optional_string(policy.sdk_request_payload); });
            field("sdk_response_payload",
                  [&]() { print_optional_string(policy.sdk_response_payload); });
            field("host_command", [&]() { print_optional_string(policy.host_command); });
            field("network_endpoint_uri",
                  [&]() { print_optional_string(policy.network_endpoint_uri); });
        });
    }

    void print_failure_attribution(
        const durable_store_import::ProviderSdkEnvelopeFailureAttribution &failure,
        int indent_level) {
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

void print_durable_store_import_provider_sdk_envelope_json(
    const durable_store_import::ProviderSdkRequestEnvelopePlan &plan, std::ostream &out) {
    DurableStoreImportProviderSdkEnvelopeJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_envelope

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_handoff_readiness {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
envelope_status_name(durable_store_import::ProviderSdkEnvelopeStatus status) {
    switch (status) {
    case durable_store_import::ProviderSdkEnvelopeStatus::Ready:
        return "ready";
    case durable_store_import::ProviderSdkEnvelopeStatus::Blocked:
        return "blocked";
    }

    return "invalid";
}

[[nodiscard]] std::string
operation_kind_name(durable_store_import::ProviderSdkEnvelopeOperationKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeOperationKind::PlanProviderSdkRequestEnvelope:
        return "plan_provider_sdk_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopRuntimePreflightNotReady:
        return "noop_runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopEnvelopePolicyMismatch:
        return "noop_envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeOperationKind::NoopUnsupportedEnvelopeCapability:
        return "noop_unsupported_envelope_capability";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ProviderSdkHandoffReadinessNextActionKind action) {
    switch (action) {
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::
        ReadyForHostExecutionPrototype:
        return "ready_for_host_execution_prototype";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::FixEnvelopePolicy:
        return "fix_envelope_policy";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::WaitForEnvelopeCapability:
        return "wait_for_envelope_capability";
    case durable_store_import::ProviderSdkHandoffReadinessNextActionKind::ManualReviewRequired:
        return "manual_review_required";
    }

    return "invalid";
}

[[nodiscard]] std::string
capability_name(durable_store_import::ProviderSdkEnvelopeCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanSecretFreeRequestEnvelope:
        return "plan_secret_free_request_envelope";
    case durable_store_import::ProviderSdkEnvelopeCapabilityKind::PlanHostHandoffDescriptor:
        return "plan_host_handoff_descriptor";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::ProviderSdkEnvelopeFailureKind kind) {
    switch (kind) {
    case durable_store_import::ProviderSdkEnvelopeFailureKind::RuntimePreflightNotReady:
        return "runtime_preflight_not_ready";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::EnvelopePolicyMismatch:
        return "envelope_policy_mismatch";
    case durable_store_import::ProviderSdkEnvelopeFailureKind::UnsupportedEnvelopeCapability:
        return "unsupported_envelope_capability";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::ProviderSdkEnvelopeFailureAttribution> &failure) {
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

void print_durable_store_import_provider_sdk_handoff_readiness(
    const durable_store_import::ProviderSdkHandoffReadinessReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_provider_sdk_request_envelope_plan_format " +
             review.source_durable_store_import_provider_sdk_request_envelope_plan_format_version);
    line(out,
         0,
         "source_provider_runtime_preflight_plan_format " +
             review.source_durable_store_import_provider_runtime_preflight_plan_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_provider_runtime_preflight_identity " +
             review.durable_store_import_provider_runtime_preflight_identity);
    line(out,
         0,
         "durable_store_import_provider_sdk_request_envelope_identity " +
             review.durable_store_import_provider_sdk_request_envelope_identity);
    line(out, 0, "envelope_status " + envelope_status_name(review.envelope_status));
    line(out, 0, "operation_kind " + operation_kind_name(review.operation_kind));
    line(out,
         0,
         "provider_sdk_request_envelope_identity " +
             (review.provider_sdk_request_envelope_identity.has_value()
                  ? *review.provider_sdk_request_envelope_identity
                  : std::string("none")));
    line(out,
         0,
         "host_handoff_descriptor_identity " + (review.host_handoff_descriptor_identity.has_value()
                                                    ? *review.host_handoff_descriptor_identity
                                                    : std::string("none")));
    line(out,
         0,
         "materializes_sdk_request_payload " +
             std::string(review.materializes_sdk_request_payload ? "true" : "false"));
    line(out,
         0,
         "starts_host_process " + std::string(review.starts_host_process ? "true" : "false"));
    line(out,
         0,
         "opens_network_connection " +
             std::string(review.opens_network_connection ? "true" : "false"));
    line(out,
         0,
         "invokes_provider_sdk " + std::string(review.invokes_provider_sdk ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out, 0, "sdk_boundary " + review.sdk_boundary_summary);
    line(out, 0, "next_step " + review.next_step_recommendation);
    print_failure_attribution(out, 0, review.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_handoff_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_contract {
namespace {

class MockContractJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit MockContractJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkMockAdapterContract &contract) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(contract.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(contract.workflow_canonical_name); });
            field("session_id", [&]() { write_string(contract.session_id); });
            field("run_id", [&]() { print_optional_string(contract.run_id); });
            field("input_fixture", [&]() { write_string(contract.input_fixture); });
            field("sdk_payload_plan_identity", [&]() {
                write_string(contract.durable_store_import_provider_sdk_payload_plan_identity);
            });
            field("mock_adapter_contract_identity", [&]() {
                write_string(
                    contract.durable_store_import_provider_sdk_mock_adapter_contract_identity);
            });
            field("operation_kind", [&]() { print_operation(contract.operation_kind); });
            field("contract_status", [&]() { print_status(contract.contract_status); });
            field("scenario_kind", [&]() { print_scenario(contract.scenario_kind); });
            field("provider_request_payload_schema_ref",
                  [&]() { write_string(contract.provider_request_payload_schema_ref); });
            field("payload_digest", [&]() { write_string(contract.payload_digest); });
            field("fake_provider_only", [&]() { write_bool(contract.fake_provider_only); });
            field("opens_network_connection",
                  [&]() { write_bool(contract.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(contract.reads_secret_material); });
            field("invokes_real_provider_sdk",
                  [&]() { write_bool(contract.invokes_real_provider_sdk); });
            field("failure_attribution", [&]() { print_failure(contract.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkMockAdapterStatus status) {
        write_string(status == durable_store_import::ProviderSdkMockAdapterStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkMockAdapterOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract,
                "plan_mock_adapter_contract");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::RunMockAdapter,
                "run_mock_adapter");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady,
                "noop_payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopContractNotReady,
                "noop_contract_not_ready"));
    }

    void print_scenario(durable_store_import::ProviderSdkMockAdapterScenarioKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkMockAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::PayloadNotReady,
                "payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ContractNotReady,
                "contract_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkMockAdapterFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_sdk_mock_adapter_contract_json(
    const durable_store_import::ProviderSdkMockAdapterContract &contract, std::ostream &out) {
    MockContractJsonPrinter(out).print(contract);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_contract

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_execution {
namespace {

class MockExecutionJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit MockExecutionJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkMockAdapterExecutionResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("mock_adapter_contract_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_sdk_mock_adapter_contract_identity);
            });
            field("mock_adapter_result_identity", [&]() {
                write_string(result.durable_store_import_provider_sdk_mock_adapter_result_identity);
            });
            field("operation_kind", [&]() { print_operation(result.operation_kind); });
            field("result_status", [&]() { print_status(result.result_status); });
            field("scenario_kind", [&]() { print_scenario(result.scenario_kind); });
            field("normalized_result", [&]() { print_normalized(result.normalized_result); });
            field("provider_status_code", [&]() { out_ << result.provider_status_code; });
            field("normalized_message", [&]() { write_string(result.normalized_message); });
            field("opens_network_connection",
                  [&]() { write_bool(result.opens_network_connection); });
            field("reads_secret_material", [&]() { write_bool(result.reads_secret_material); });
            field("invokes_real_provider_sdk",
                  [&]() { write_bool(result.invokes_real_provider_sdk); });
            field("failure_attribution", [&]() { print_failure(result.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkMockAdapterStatus status) {
        write_string(status == durable_store_import::ProviderSdkMockAdapterStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkMockAdapterOperationKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract,
                "plan_mock_adapter_contract");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::RunMockAdapter,
                "run_mock_adapter");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady,
                "noop_payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterOperationKind::NoopContractNotReady,
                "noop_contract_not_ready"));
    }

    void print_scenario(durable_store_import::ProviderSdkMockAdapterScenarioKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted,
                "accepted");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout,
                "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled,
                "throttled");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict,
                "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch,
                "schema_mismatch");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked,
                "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSdkMockAdapterFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::PayloadNotReady,
                "payload_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ContractNotReady,
                "contract_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::ProviderFailure,
                "provider_failure");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Timeout, "timeout");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Throttle, "throttle");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::Conflict, "conflict");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkMockAdapterFailureKind::SchemaMismatch,
                "schema_mismatch"));
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkMockAdapterFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_sdk_mock_adapter_execution_json(
    const durable_store_import::ProviderSdkMockAdapterExecutionResult &result, std::ostream &out) {
    MockExecutionJsonPrinter(out).print(result);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_execution

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_mock_adapter_readiness {
namespace {

void print_status(durable_store_import::ProviderSdkMockAdapterStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkMockAdapterStatus::Ready ? "ready"
                                                                                : "blocked");
}

void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind,
                      std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted, "accepted");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure,
            "provider_failure");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout, "timeout");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled,
            "throttled");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict, "conflict");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch,
            "schema_mismatch");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderSdkMockAdapterNextActionKind kind,
                std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::ReadyForRealAdapterParity,
            "ready_for_real_adapter_parity");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::WaitForPayload,
            "wait_for_payload");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkMockAdapterNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_sdk_mock_adapter_readiness(
    const durable_store_import::ProviderSdkMockAdapterReadiness &readiness, std::ostream &out) {
    out << readiness.format_version << '\n';
    out << "workflow " << readiness.workflow_canonical_name << '\n';
    out << "session " << readiness.session_id << '\n';
    out << "mock_adapter_result "
        << readiness.durable_store_import_provider_sdk_mock_adapter_result_identity << '\n';
    out << "result_status ";
    print_status(readiness.result_status, out);
    out << '\n';
    out << "normalized_result ";
    print_normalized(readiness.normalized_result, out);
    out << '\n';
    out << "normalization " << readiness.normalization_summary << '\n';
    out << "next_action ";
    print_next(readiness.next_action, out);
    out << '\n';
    out << "next_step " << readiness.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_mock_adapter_readiness

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_payload_audit {
namespace {

void print_status(durable_store_import::ProviderSdkPayloadStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSdkPayloadStatus::Ready ? "ready" : "blocked");
}

void print_next(durable_store_import::ProviderSdkPayloadNextActionKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::ReadyForMockAdapter,
            "ready_for_mock_adapter");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::WaitForLocalHarness,
            "wait_for_local_harness");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSdkPayloadNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

} // namespace

void print_durable_store_import_provider_sdk_payload_audit(
    const durable_store_import::ProviderSdkPayloadAuditSummary &audit, std::ostream &out) {
    out << audit.format_version << '\n';
    out << "workflow " << audit.workflow_canonical_name << '\n';
    out << "session " << audit.session_id << '\n';
    out << "sdk_payload_plan " << audit.durable_store_import_provider_sdk_payload_plan_identity
        << '\n';
    out << "payload_status ";
    print_status(audit.payload_status, out);
    out << '\n';
    out << "schema " << audit.provider_request_payload_schema_ref << '\n';
    out << "payload_digest " << audit.payload_digest << '\n';
    out << "fake_provider_only " << (audit.fake_provider_only ? "true" : "false") << '\n';
    out << "raw_payload_persisted " << (audit.raw_payload_persisted ? "true" : "false") << '\n';
    out << "redaction secret_free=" << (audit.redaction_summary.secret_free ? "true" : "false")
        << " credential_free=" << (audit.redaction_summary.credential_free ? "true" : "false")
        << " token_free=" << (audit.redaction_summary.token_free ? "true" : "false")
        << " redacted_field_count=" << audit.redaction_summary.redacted_field_count << '\n';
    out << "audit " << audit.audit_summary << '\n';
    out << "next_action ";
    print_next(audit.next_action, out);
    out << '\n';
    out << "next_step " << audit.next_step_recommendation << '\n';
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_payload_audit

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_sdk_payload_plan {
namespace {

class PayloadPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit PayloadPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkPayloadMaterializationPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("harness_record_identity", [&]() {
                write_string(plan.durable_store_import_provider_local_host_harness_record_identity);
            });
            field("sdk_payload_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_sdk_payload_plan_identity);
            });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("payload_status", [&]() { print_status(plan.payload_status); });
            field("fake_provider_only", [&]() { write_bool(plan.fake_provider_only); });
            field("provider_request_payload_schema_ref",
                  [&]() { write_string(plan.provider_request_payload_schema_ref); });
            field("payload_digest", [&]() { write_string(plan.payload_digest); });
            field("redaction_summary", [&]() { print_redaction(plan.redaction_summary, 1); });
            field("materializes_sdk_request_payload",
                  [&]() { write_bool(plan.materializes_sdk_request_payload); });
            field("persists_materialized_payload",
                  [&]() { write_bool(plan.persists_materialized_payload); });
            field("raw_payload", [&]() { print_optional_string(plan.raw_payload); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSdkPayloadStatus status) {
        write_string(status == durable_store_import::ProviderSdkPayloadStatus::Ready ? "ready"
                                                                                     : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkPayloadOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkPayloadOperationKind::
            PlanFakeProviderPayloadMaterialization:
            write_string("plan_fake_provider_payload_materialization");
            return;
        case durable_store_import::ProviderSdkPayloadOperationKind::NoopHarnessNotReady:
            write_string("noop_harness_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkPayloadFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::HarnessNotReady,
                "harness_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::ForbiddenPayloadMaterial,
                "forbidden_payload_material");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSdkPayloadFailureKind::UnsupportedProviderSchema,
                "unsupported_provider_schema"));
    }

    void print_redaction(const durable_store_import::ProviderSdkPayloadRedactionSummary &summary,
                         int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("secret_free", [&]() { write_bool(summary.secret_free); });
            field("credential_free", [&]() { write_bool(summary.credential_free); });
            field("token_free", [&]() { write_bool(summary.token_free); });
            field("redacted_field_count", [&]() { out_ << summary.redacted_field_count; });
            field("summary", [&]() { write_string(summary.summary); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkPayloadFailureAttribution> &failure,
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

void print_durable_store_import_provider_sdk_payload_plan_json(
    const durable_store_import::ProviderSdkPayloadMaterializationPlan &plan, std::ostream &out) {
    PayloadPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_sdk_payload_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_policy_review {
namespace {

void print_status(durable_store_import::ProviderSecretStatus status, std::ostream &out) {
    out << (status == durable_store_import::ProviderSecretStatus::Ready ? "ready" : "blocked");
}

void print_operation(durable_store_import::ProviderSecretOperationKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest,
            "plan_secret_resolver_request");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(durable_store_import::ProviderSecretOperationKind::
                                            PlanSecretResolverResponsePlaceholder,
                                        "plan_secret_resolver_response_placeholder");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady,
            "noop_config_snapshot_not_ready");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady,
            "noop_secret_resolver_request_not_ready"));
}

void print_lifecycle(durable_store_import::ProviderCredentialLifecycleState state,
                     std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        state,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::HandlePlanned,
            "handle_planned");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::PlaceholderPendingResolution,
            "placeholder_pending_resolution");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderCredentialLifecycleState::Blocked, "blocked"));
}

void print_next(durable_store_import::ProviderSecretPolicyNextActionKind kind, std::ostream &out) {
    AHFL_ARTIFACT_PRINT_ENUM(
        kind,
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::ReadyForLocalHostHarness,
            "ready_for_local_host_harness");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::WaitForConfigSnapshot,
            "wait_for_config_snapshot");
        AHFL_ARTIFACT_OSTREAM_ENUM_CASE(
            durable_store_import::ProviderSecretPolicyNextActionKind::ManualReviewRequired,
            "manual_review_required"));
}

void print_failure(const durable_store_import::ProviderSecretPolicyReview &review,
                   std::ostream &out) {
    if (!review.failure_attribution.has_value()) {
        out << "none\n";
        return;
    }
    switch (review.failure_attribution->kind) {
    case durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady:
        out << "config_snapshot_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady:
        out << "secret_resolver_request_not_ready ";
        break;
    case durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation:
        out << "secret_policy_violation ";
        break;
    }
    out << review.failure_attribution->message << '\n';
}

} // namespace

void print_durable_store_import_provider_secret_policy_review(
    const durable_store_import::ProviderSecretPolicyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "secret_request "
        << review.durable_store_import_provider_secret_resolver_request_identity << '\n';
    out << "secret_response "
        << review.durable_store_import_provider_secret_resolver_response_identity << '\n';
    out << "response_status ";
    print_status(review.response_status, out);
    out << '\n';
    out << "operation ";
    print_operation(review.operation_kind, out);
    out << '\n';
    out << "secret_handle " << review.secret_handle.handle_identity << '\n';
    out << "credential_lifecycle " << review.credential_lifecycle_version << ' ';
    print_lifecycle(review.credential_lifecycle_state, out);
    out << '\n';
    out << "side_effects secret_read=" << (review.reads_secret_material ? "true" : "false")
        << " secret_value=" << (review.materializes_secret_value ? "true" : "false")
        << " credential=" << (review.materializes_credential_material ? "true" : "false")
        << " token=" << (review.materializes_token_value ? "true" : "false")
        << " network=" << (review.opens_network_connection ? "true" : "false")
        << " host_env=" << (review.reads_host_environment ? "true" : "false")
        << " filesystem=" << (review.writes_host_filesystem ? "true" : "false")
        << " secret_manager=" << (review.invokes_secret_manager ? "true" : "false") << '\n';
    out << "secret_policy " << review.secret_policy_summary << '\n';
    out << "next_action ";
    print_next(review.next_action, out);
    out << '\n';
    out << "next_step " << review.next_step_recommendation << '\n';
    out << "failure ";
    print_failure(review, out);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_policy_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_resolver_request {
namespace {

class SecretRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SecretRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSecretResolverRequestPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field(
                "source_durable_store_import_provider_config_snapshot_placeholder_format_version",
                [&]() {
                    write_string(
                        plan.source_durable_store_import_provider_config_snapshot_placeholder_format_version);
                });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("durable_store_import_provider_config_snapshot_identity", [&]() {
                write_string(plan.durable_store_import_provider_config_snapshot_identity);
            });
            field("source_config_snapshot_status",
                  [&]() { print_config_status(plan.source_config_snapshot_status); });
            field("provider_config_profile_identity",
                  [&]() { write_string(plan.provider_config_profile_identity); });
            field("provider_config_snapshot_placeholder_identity",
                  [&]() { write_string(plan.provider_config_snapshot_placeholder_identity); });
            field("durable_store_import_provider_secret_resolver_request_identity", [&]() {
                write_string(plan.durable_store_import_provider_secret_resolver_request_identity);
            });
            field("operation_kind", [&]() { print_operation(plan.operation_kind); });
            field("request_status", [&]() { print_status(plan.request_status); });
            field("secret_handle", [&]() { print_handle(plan.secret_handle, 1); });
            field("provider_secret_resolver_response_placeholder_identity", [&]() {
                write_string(plan.provider_secret_resolver_response_placeholder_identity);
            });
            field("reads_secret_material", [&]() { write_bool(plan.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(plan.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(plan.materializes_credential_material); });
            field("materializes_token_value", [&]() { write_bool(plan.materializes_token_value); });
            field("opens_network_connection", [&]() { write_bool(plan.opens_network_connection); });
            field("reads_host_environment", [&]() { write_bool(plan.reads_host_environment); });
            field("writes_host_filesystem", [&]() { write_bool(plan.writes_host_filesystem); });
            field("invokes_secret_manager", [&]() { write_bool(plan.invokes_secret_manager); });
            field("secret_value", [&]() { print_optional_string(plan.secret_value); });
            field("credential_material",
                  [&]() { print_optional_string(plan.credential_material); });
            field("token_value", [&]() { print_optional_string(plan.token_value); });
            field("secret_manager_response",
                  [&]() { print_optional_string(plan.secret_manager_response); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_config_status(durable_store_import::ProviderConfigStatus status) {
        write_string(status == durable_store_import::ProviderConfigStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_status(durable_store_import::ProviderSecretStatus status) {
        write_string(status == durable_store_import::ProviderSecretStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_operation(durable_store_import::ProviderSecretOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest:
            write_string("plan_secret_resolver_request");
            return;
        case durable_store_import::ProviderSecretOperationKind::
            PlanSecretResolverResponsePlaceholder:
            write_string("plan_secret_resolver_response_placeholder");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady:
            write_string("noop_config_snapshot_not_ready");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady:
            write_string("noop_secret_resolver_request_not_ready");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSecretFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady,
                "config_snapshot_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady,
                "secret_resolver_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation,
                "secret_policy_violation"));
    }

    void print_handle(const durable_store_import::ProviderSecretHandleReference &handle,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("handle_schema_version", [&]() { write_string(handle.handle_schema_version); });
            field("handle_identity", [&]() { write_string(handle.handle_identity); });
            field("provider_key", [&]() { write_string(handle.provider_key); });
            field("profile_key", [&]() { write_string(handle.profile_key); });
            field("purpose", [&]() { write_string(handle.purpose); });
            field("contains_secret_value", [&]() { write_bool(handle.contains_secret_value); });
            field("contains_credential_material",
                  [&]() { write_bool(handle.contains_credential_material); });
            field("contains_token_value", [&]() { write_bool(handle.contains_token_value); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSecretFailureAttribution> &failure,
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

void print_durable_store_import_provider_secret_resolver_request_json(
    const durable_store_import::ProviderSecretResolverRequestPlan &plan, std::ostream &out) {
    SecretRequestJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_resolver_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_secret_resolver_response {
namespace {

class SecretResponseJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SecretResponseJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(placeholder.format_version); });
            field(
                "source_durable_store_import_provider_secret_resolver_request_plan_format_version",
                [&]() {
                    write_string(
                        placeholder
                            .source_durable_store_import_provider_secret_resolver_request_plan_format_version);
                });
            field("workflow_canonical_name",
                  [&]() { write_string(placeholder.workflow_canonical_name); });
            field("session_id", [&]() { write_string(placeholder.session_id); });
            field("run_id", [&]() { print_optional_string(placeholder.run_id); });
            field("input_fixture", [&]() { write_string(placeholder.input_fixture); });
            field("durable_store_import_provider_secret_resolver_request_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_secret_resolver_request_identity);
            });
            field("source_secret_resolver_request_status",
                  [&]() { print_status(placeholder.source_secret_resolver_request_status); });
            field("durable_store_import_provider_secret_resolver_response_identity", [&]() {
                write_string(
                    placeholder.durable_store_import_provider_secret_resolver_response_identity);
            });
            field("operation_kind", [&]() { print_operation(placeholder.operation_kind); });
            field("response_status", [&]() { print_status(placeholder.response_status); });
            field("secret_handle", [&]() { print_handle(placeholder.secret_handle, 1); });
            field("credential_lifecycle_version",
                  [&]() { write_string(placeholder.credential_lifecycle_version); });
            field("credential_lifecycle_state",
                  [&]() { print_lifecycle(placeholder.credential_lifecycle_state); });
            field("reads_secret_material",
                  [&]() { write_bool(placeholder.reads_secret_material); });
            field("materializes_secret_value",
                  [&]() { write_bool(placeholder.materializes_secret_value); });
            field("materializes_credential_material",
                  [&]() { write_bool(placeholder.materializes_credential_material); });
            field("materializes_token_value",
                  [&]() { write_bool(placeholder.materializes_token_value); });
            field("opens_network_connection",
                  [&]() { write_bool(placeholder.opens_network_connection); });
            field("reads_host_environment",
                  [&]() { write_bool(placeholder.reads_host_environment); });
            field("writes_host_filesystem",
                  [&]() { write_bool(placeholder.writes_host_filesystem); });
            field("invokes_secret_manager",
                  [&]() { write_bool(placeholder.invokes_secret_manager); });
            field("failure_attribution",
                  [&]() { print_failure(placeholder.failure_attribution, 1); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSecretStatus status) {
        write_string(status == durable_store_import::ProviderSecretStatus::Ready ? "ready"
                                                                                 : "blocked");
    }

    void print_operation(durable_store_import::ProviderSecretOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSecretOperationKind::PlanSecretResolverRequest:
            write_string("plan_secret_resolver_request");
            return;
        case durable_store_import::ProviderSecretOperationKind::
            PlanSecretResolverResponsePlaceholder:
            write_string("plan_secret_resolver_response_placeholder");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopConfigSnapshotNotReady:
            write_string("noop_config_snapshot_not_ready");
            return;
        case durable_store_import::ProviderSecretOperationKind::NoopSecretResolverRequestNotReady:
            write_string("noop_secret_resolver_request_not_ready");
            return;
        }
    }

    void print_lifecycle(durable_store_import::ProviderCredentialLifecycleState state) {
        AHFL_ARTIFACT_PRINT_ENUM(
            state,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderCredentialLifecycleState::HandlePlanned,
                "handle_planned");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCredentialLifecycleState::
                                        PlaceholderPendingResolution,
                                    "placeholder_pending_resolution");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderCredentialLifecycleState::Blocked,
                                    "blocked"));
    }

    void print_failure_kind(durable_store_import::ProviderSecretFailureKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady,
                "config_snapshot_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady,
                "secret_resolver_request_not_ready");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation,
                "secret_policy_violation"));
    }

    void print_handle(const durable_store_import::ProviderSecretHandleReference &handle,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("handle_schema_version", [&]() { write_string(handle.handle_schema_version); });
            field("handle_identity", [&]() { write_string(handle.handle_identity); });
            field("provider_key", [&]() { write_string(handle.provider_key); });
            field("profile_key", [&]() { write_string(handle.profile_key); });
            field("purpose", [&]() { write_string(handle.purpose); });
            field("contains_secret_value", [&]() { write_bool(handle.contains_secret_value); });
            field("contains_credential_material",
                  [&]() { write_bool(handle.contains_credential_material); });
            field("contains_token_value", [&]() { write_bool(handle.contains_token_value); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSecretFailureAttribution> &failure,
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

void print_durable_store_import_provider_secret_resolver_response_json(
    const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder,
    std::ostream &out) {
    SecretResponseJsonPrinter(out).print(placeholder);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_secret_resolver_response

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_selection_plan {
namespace {

class SelectionPlanJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit SelectionPlanJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderSelectionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("selection_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_selection_plan_identity);
            });
            field("selected_provider_id", [&]() { write_string(plan.selected_provider_id); });
            field("fallback_provider_id", [&]() { write_string(plan.fallback_provider_id); });
            field("selection_status", [&]() { print_status(plan.selection_status); });
            field("degradation_allowed", [&]() { write_bool(plan.degradation_allowed); });
            field("requires_operator_review", [&]() { write_bool(plan.requires_operator_review); });
            field("fallback_policy", [&]() { write_string(plan.fallback_policy); });
            field("capability_gap_summary", [&]() { write_string(plan.capability_gap_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSelectionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Selected,
                                    "selected");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::FallbackSelected,
                                    "fallback_selected");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderSelectionStatus::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_selection_plan_json(
    const durable_store_import::ProviderSelectionPlan &plan, std::ostream &out) {
    SelectionPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_selection_plan

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_provider_telemetry_summary {
namespace {

class TelemetrySummaryJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit TelemetrySummaryJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::ProviderTelemetrySummary &summary) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(summary.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(summary.workflow_canonical_name); });
            field("session_id", [&]() { write_string(summary.session_id); });
            field("run_id", [&]() { print_optional_string(summary.run_id); });
            field("input_fixture", [&]() { write_string(summary.input_fixture); });
            field("telemetry_summary_identity", [&]() {
                write_string(summary.durable_store_import_provider_telemetry_summary_identity);
            });
            field("outcome", [&]() { print_outcome(summary.outcome); });
            field("provider_write_attempted",
                  [&]() { write_bool(summary.provider_write_attempted); });
            field("provider_write_committed",
                  [&]() { write_bool(summary.provider_write_committed); });
            field("retry_recommended", [&]() { write_bool(summary.retry_recommended); });
            field("recovery_required", [&]() { write_bool(summary.recovery_required); });
            field("telemetry_summary", [&]() { write_string(summary.telemetry_summary); });
            field("secret_free", [&]() { write_bool(summary.secret_free); });
            field("raw_telemetry_persisted",
                  [&]() { write_bool(summary.raw_telemetry_persisted); });
        });
        out_ << '\n';
    }

  private:
    void print_outcome(durable_store_import::ProviderAuditOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Success, "success");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Failure, "failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RetryPending,
                                    "retry_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::RecoveryPending,
                                    "recovery_pending");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ProviderAuditOutcome::Blocked,
                                    "blocked"));
    }
};

} // namespace

void print_durable_store_import_provider_telemetry_summary_json(
    const durable_store_import::ProviderTelemetrySummary &summary, std::ostream &out) {
    TelemetrySummaryJsonPrinter(out).print(summary);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_provider_telemetry_summary

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

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt {

namespace {

class DurableStoreImportDecisionReceiptJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionReceiptJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Receipt &receipt) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(receipt.format_version); });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(receipt.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(receipt.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(receipt.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(receipt.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(receipt.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(receipt.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(receipt.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(receipt.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(receipt.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(receipt.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(receipt.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(receipt.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(receipt.workflow_canonical_name); });
            field("session_id", [&]() { write_string(receipt.session_id); });
            field("run_id", [&]() { print_optional_string(receipt.run_id); });
            field("input_fixture", [&]() { write_string(receipt.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(receipt.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(receipt.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(receipt.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(receipt.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(receipt.descriptor_status); });
            field("request_status", [&]() { print_request_status(receipt.request_status); });
            field("decision_status", [&]() { print_decision_status(receipt.decision_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(receipt.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(receipt.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(receipt.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(receipt.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(receipt.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(receipt.durable_store_import_receipt_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(receipt.planned_durable_identity); });
            field("decision_boundary_kind",
                  [&]() { print_decision_boundary_kind(receipt.decision_boundary_kind); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(receipt.receipt_boundary_kind); });
            field("receipt_status", [&]() { print_receipt_status(receipt.receipt_status); });
            field("receipt_outcome", [&]() { print_receipt_outcome(receipt.receipt_outcome); });
            field("accepted_for_receipt_archive",
                  [&]() { write_bool(receipt.accepted_for_receipt_archive); });
            field("next_required_adapter_capability", [&]() {
                print_optional(receipt.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("receipt_blocker", [&]() {
                print_optional(receipt.receipt_blocker,
                               [&](const auto &value) { print_receipt_blocker(value, 1); });
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

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_decision_boundary_kind(durable_store_import::DecisionBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::DecisionBoundaryKind::AdapterDecisionConsumable,
                "adapter_decision_consumable"));
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable,
                "adapter_receipt_consumable"));
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_receipt_outcome(durable_store_import::ReceiptOutcome outcome) {
        AHFL_ARTIFACT_PRINT_ENUM(
            outcome,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::ArchiveAcceptedDecision,
                                    "archive_accepted_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::BlockBlockedDecision,
                                    "block_blocked_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::DeferPartialDecision,
                                    "defer_partial_decision");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptOutcome::RejectFailedDecision,
                                    "reject_failed_decision"));
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_receipt_blocker(const durable_store_import::ReceiptBlocker &blocker,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::ReceiptBlockerKind::SourceDecisionBlocked:
                    write_string("source_decision_blocked");
                    return;
                case durable_store_import::ReceiptBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::ReceiptBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
                    return;
                case durable_store_import::ReceiptBlockerKind::WorkflowFailure:
                    write_string("workflow_failure");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_json(const durable_store_import::Receipt &receipt,
                                             std::ostream &out) {
    DurableStoreImportDecisionReceiptJsonPrinter(out).print(receipt);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_request {

namespace {

class DurableStoreImportDecisionReceiptPersistenceRequestJsonPrinter final
    : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionReceiptPersistenceRequestJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::PersistenceRequest &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(request.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(request.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(request.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(request.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(request.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(request.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(request.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(request.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(request.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(request.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(request.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(request.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(request.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(request.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(request.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(request.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(request.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(request.descriptor_status); });
            field("request_status", [&]() { print_request_status(request.request_status); });
            field("decision_status", [&]() { print_decision_status(request.decision_status); });
            field("receipt_status", [&]() { print_receipt_status(request.receipt_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(request.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(request.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(request.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(request.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(request.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(request.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(request.durable_store_import_receipt_persistence_request_identity);
            });
            field("planned_durable_identity",
                  [&]() { write_string(request.planned_durable_identity); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(request.receipt_boundary_kind); });
            field("receipt_persistence_boundary_kind", [&]() {
                print_receipt_persistence_boundary_kind(request.receipt_persistence_boundary_kind);
            });
            field("receipt_persistence_request_status", [&]() {
                print_receipt_persistence_request_status(
                    request.receipt_persistence_request_status);
            });
            field("receipt_persistence_request_outcome", [&]() {
                print_receipt_persistence_request_outcome(
                    request.receipt_persistence_request_outcome);
            });
            field("accepted_for_receipt_persistence",
                  [&]() { write_bool(request.accepted_for_receipt_persistence); });
            field("next_required_adapter_capability", [&]() {
                print_optional(request.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("receipt_persistence_blocker", [&]() {
                print_optional(request.receipt_persistence_blocker, [&](const auto &value) {
                    print_receipt_persistence_blocker(value, 1);
                });
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

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable,
                "adapter_receipt_consumable"));
    }

    void
    print_receipt_persistence_boundary_kind(durable_store_import::PersistenceBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable,
                "adapter_receipt_persistence_consumable"));
    }

    void print_receipt_persistence_request_status(
        durable_store_import::PersistenceRequestStatus status) {
        switch (status) {
        case durable_store_import::PersistenceRequestStatus::ReadyToPersist:
            write_string("ready_to_persist");
            return;
        case durable_store_import::PersistenceRequestStatus::Blocked:
            write_string("blocked");
            return;
        case durable_store_import::PersistenceRequestStatus::Deferred:
            write_string("deferred");
            return;
        case durable_store_import::PersistenceRequestStatus::Rejected:
            write_string("rejected");
            return;
        }
    }

    void print_receipt_persistence_request_outcome(
        durable_store_import::PersistenceRequestOutcome outcome) {
        switch (outcome) {
        case durable_store_import::PersistenceRequestOutcome::PersistReadyReceipt:
            write_string("persist_ready_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::BlockBlockedReceipt:
            write_string("block_blocked_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::DeferPartialReceipt:
            write_string("defer_partial_receipt");
            return;
        case durable_store_import::PersistenceRequestOutcome::RejectFailedReceipt:
            write_string("reject_failed_receipt");
            return;
        }
    }

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_receipt_persistence_blocker(const durable_store_import::PersistenceBlocker &blocker,
                                           int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::PersistenceBlockerKind::SourceReceiptBlocked:
                    write_string("source_receipt_blocked");
                    return;
                case durable_store_import::PersistenceBlockerKind::MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::PersistenceBlockerKind::PartialWorkflowState:
                    write_string("partial_workflow_state");
                    return;
                case durable_store_import::PersistenceBlockerKind::WorkflowFailure:
                    write_string("workflow_failure");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_persistence_request_json(
    const durable_store_import::PersistenceRequest &request, std::ostream &out) {
    DurableStoreImportDecisionReceiptPersistenceRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_request

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_response {

namespace {

class DurableStoreImportDecisionReceiptPersistenceResponseJsonPrinter final
    : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportDecisionReceiptPersistenceResponseJsonPrinter(std::ostream &out)
        : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::PersistenceResponse &response) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(response.format_version); });
            field(
                "source_durable_store_import_decision_receipt_persistence_request_format_version",
                [&]() {
                    write_string(
                        response
                            .source_durable_store_import_decision_receipt_persistence_request_format_version);
                });
            field("source_durable_store_import_decision_receipt_format_version", [&]() {
                write_string(response.source_durable_store_import_decision_receipt_format_version);
            });
            field("source_durable_store_import_decision_format_version", [&]() {
                write_string(response.source_durable_store_import_decision_format_version);
            });
            field("source_durable_store_import_request_format_version", [&]() {
                write_string(response.source_durable_store_import_request_format_version);
            });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(response.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(response.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(response.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(response.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(response.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(response.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(response.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(response.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(response.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(response.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(response.workflow_canonical_name); });
            field("session_id", [&]() { write_string(response.session_id); });
            field("run_id", [&]() { print_optional_string(response.run_id); });
            field("input_fixture", [&]() { write_string(response.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(response.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(response.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(response.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(response.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(response.descriptor_status); });
            field("request_status", [&]() { print_request_status(response.request_status); });
            field("decision_status", [&]() { print_decision_status(response.decision_status); });
            field("receipt_status", [&]() { print_receipt_status(response.receipt_status); });
            field("persistence_request_status",
                  [&]() { print_persistence_request_status(response.persistence_request_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(response.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(response.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(response.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(response.durable_store_import_request_identity); });
            field("durable_store_import_decision_identity",
                  [&]() { write_string(response.durable_store_import_decision_identity); });
            field("durable_store_import_receipt_identity",
                  [&]() { write_string(response.durable_store_import_receipt_identity); });
            field("durable_store_import_receipt_persistence_request_identity", [&]() {
                write_string(response.durable_store_import_receipt_persistence_request_identity);
            });
            field("durable_store_import_receipt_persistence_response_identity", [&]() {
                write_string(response.durable_store_import_receipt_persistence_response_identity);
            });
            field("planned_durable_identity",
                  [&]() { write_string(response.planned_durable_identity); });
            field("receipt_boundary_kind",
                  [&]() { print_receipt_boundary_kind(response.receipt_boundary_kind); });
            field("receipt_persistence_boundary_kind", [&]() {
                print_receipt_persistence_boundary_kind(response.receipt_persistence_boundary_kind);
            });
            field("receipt_persistence_response_boundary_kind", [&]() {
                print_receipt_persistence_response_boundary_kind(
                    response.receipt_persistence_response_boundary_kind);
            });
            field("response_status", [&]() { print_response_status(response.response_status); });
            field("response_outcome", [&]() { print_response_outcome(response.response_outcome); });
            field("acknowledged_for_response",
                  [&]() { write_bool(response.acknowledged_for_response); });
            field("next_required_adapter_capability", [&]() {
                print_optional(response.next_required_adapter_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
            field("response_blocker", [&]() {
                print_optional(response.response_blocker,
                               [&](const auto &value) { print_response_blocker(value, 1); });
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

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_decision_status(durable_store_import::DecisionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Accepted, "accepted");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::DecisionStatus::Rejected, "rejected"));
    }

    void print_receipt_status(durable_store_import::ReceiptStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::ReadyForArchive,
                                    "ready_for_archive");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Deferred, "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptStatus::Rejected, "rejected"));
    }

    void print_persistence_request_status(durable_store_import::PersistenceRequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Deferred,
                                    "deferred");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::PersistenceRequestStatus::Rejected,
                                    "rejected"));
    }

    void print_receipt_boundary_kind(durable_store_import::ReceiptBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::ReceiptBoundaryKind::LocalContractOnly,
                                    "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable,
                "adapter_receipt_consumable"));
    }

    void
    print_receipt_persistence_boundary_kind(durable_store_import::PersistenceBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::LocalContractOnly,
                "local_contract_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable,
                "adapter_receipt_persistence_consumable"));
    }

    void print_receipt_persistence_response_boundary_kind(
        durable_store_import::PersistenceResponseBoundaryKind kind) {
        switch (kind) {
        case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
            write_string("local_contract_only");
            return;
        case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
            write_string("adapter_response_consumable");
            return;
        }
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

    void print_adapter_capability(durable_store_import::AdapterCapabilityKind capability) {
        AHFL_ARTIFACT_PRINT_ENUM(
            capability,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor,
                "consume_store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeExportManifest,
                "consume_export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor,
                "consume_persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext,
                "consume_human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord,
                "consume_checkpoint_record");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState,
                "preserve_partial_workflow_state"));
    }

    void print_response_blocker(const durable_store_import::PersistenceResponseBlocker &blocker,
                                int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (blocker.kind) {
                case durable_store_import::PersistenceResponseBlockerKind::
                    SourcePersistenceRequestBlocked:
                    write_string("source_persistence_request_blocked");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::
                    MissingRequiredAdapterCapability:
                    write_string("missing_required_adapter_capability");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::PartialPersistenceState:
                    write_string("partial_persistence_state");
                    return;
                case durable_store_import::PersistenceResponseBlockerKind::
                    PersistenceWorkflowFailure:
                    write_string("persistence_workflow_failure");
                    return;
                }
            });
            field("message", [&]() { write_string(blocker.message); });
            field("required_capability", [&]() {
                print_optional(blocker.required_capability,
                               [&](const auto &value) { print_adapter_capability(value); });
            });
        });
    }
};

} // namespace

void print_durable_store_import_receipt_persistence_response_json(
    const durable_store_import::PersistenceResponse &response, std::ostream &out) {
    DurableStoreImportDecisionReceiptPersistenceResponseJsonPrinter(out).print(response);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_response

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_response_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
response_status_name(durable_store_import::PersistenceResponseStatus status) {
    switch (status) {
    case durable_store_import::PersistenceResponseStatus::Accepted:
        return "accepted";
    case durable_store_import::PersistenceResponseStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceResponseStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceResponseStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_outcome_name(durable_store_import::PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accept_persistence_request";
    case durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest:
        return "block_blocked_request";
    case durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest:
        return "defer_deferred_request";
    case durable_store_import::PersistenceResponseOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_boundary_kind_name(durable_store_import::PersistenceResponseBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::PersistenceResponseBoundaryKind::AdapterResponseConsumable:
        return "adapter_response_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::PersistenceResponseReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::PersistenceResponseReviewNextActionKind::AcknowledgeResponse:
        return "acknowledge_response";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::PersistenceResponseReviewNextActionKind::WaitforCapability:
        return "wait_for_capability";
    case durable_store_import::PersistenceResponseReviewNextActionKind::ReviewFailure:
        return "review_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string
blocker_kind_name(durable_store_import::PersistenceResponseBlockerKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceResponseBlockerKind::SourcePersistenceRequestBlocked:
        return "source_persistence_request_blocked";
    case durable_store_import::PersistenceResponseBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::PersistenceResponseBlockerKind::PartialPersistenceState:
        return "partial_persistence_state";
    case durable_store_import::PersistenceResponseBlockerKind::PersistenceWorkflowFailure:
        return "persistence_workflow_failure";
    }

    return "invalid";
}

void print_response_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::PersistenceResponseBlocker> &blocker) {
    line(out, indent_level, "response_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_persistence_response_review(
    const durable_store_import::PersistenceResponseReviewSummary &review, std::ostream &out) {
    out << review.format_version << '\n';
    line(out,
         0,
         "source_response_format " +
             review
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(
        out,
        0,
        "source_persistence_request_format " +
            review.source_durable_store_import_decision_receipt_persistence_request_format_version);
    line(out,
         0,
         "source_receipt_format " +
             review.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + review.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + review.source_durable_store_import_request_format_version);
    line(out, 0, "workflow " + review.workflow_canonical_name);
    line(out, 0, "session " + review.session_id);
    line(out, 0, "run_id " + (review.run_id.has_value() ? *review.run_id : "none"));
    line(out, 0, "input_fixture " + review.input_fixture);
    line(out,
         0,
         "durable_store_import_decision_identity " + review.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + review.durable_store_import_receipt_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_request_identity " +
             review.durable_store_import_receipt_persistence_request_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             review.durable_store_import_receipt_persistence_response_identity);
    line(out, 0, "response_status " + response_status_name(review.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(review.response_outcome));
    line(out,
         0,
         "response_boundary_kind " + response_boundary_kind_name(review.response_boundary_kind));
    line(out,
         0,
         std::string("acknowledged_for_response ") +
             (review.acknowledged_for_response ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(review.next_action));
    line(out,
         0,
         "next_required_adapter_capability " +
             (review.next_required_adapter_capability.has_value()
                  ? capability_name(*review.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_response_contract " + review.adapter_response_contract_summary);
    line(out, 0, "response_preview " + review.response_preview.response_identity);
    line(out, 0, "next_step " + review.next_step_recommendation);

    print_response_blocker(out, 0, review.response_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_response_review

namespace ahfl::durable_store_import_artifacts_detail::
    durable_store_import_receipt_persistence_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status) {
    switch (status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        return "ready_to_import";
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        return "blocked";
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string request_status_name(durable_store_import::RequestStatus status) {
    switch (status) {
    case durable_store_import::RequestStatus::ReadyForAdapter:
        return "ready_for_adapter";
    case durable_store_import::RequestStatus::Blocked:
        return "blocked";
    case durable_store_import::RequestStatus::TerminalCompleted:
        return "terminal_completed";
    case durable_store_import::RequestStatus::TerminalFailed:
        return "terminal_failed";
    case durable_store_import::RequestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string decision_status_name(durable_store_import::DecisionStatus status) {
    switch (status) {
    case durable_store_import::DecisionStatus::Accepted:
        return "accepted";
    case durable_store_import::DecisionStatus::Blocked:
        return "blocked";
    case durable_store_import::DecisionStatus::Deferred:
        return "deferred";
    case durable_store_import::DecisionStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_status_name(durable_store_import::ReceiptStatus status) {
    switch (status) {
    case durable_store_import::ReceiptStatus::ReadyForArchive:
        return "ready_for_archive";
    case durable_store_import::ReceiptStatus::Blocked:
        return "blocked";
    case durable_store_import::ReceiptStatus::Deferred:
        return "deferred";
    case durable_store_import::ReceiptStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_request_status_name(durable_store_import::PersistenceRequestStatus status) {
    switch (status) {
    case durable_store_import::PersistenceRequestStatus::ReadyToPersist:
        return "ready_to_persist";
    case durable_store_import::PersistenceRequestStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceRequestStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceRequestStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_boundary_kind_name(durable_store_import::ReceiptBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable:
        return "adapter_receipt_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_boundary_kind_name(durable_store_import::PersistenceBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::PersistenceBoundaryKind::AdapterReceiptPersistenceConsumable:
        return "adapter_receipt_persistence_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_persistence_request_outcome_name(durable_store_import::PersistenceRequestOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceRequestOutcome::PersistReadyReceipt:
        return "persist_ready_receipt";
    case durable_store_import::PersistenceRequestOutcome::BlockBlockedReceipt:
        return "block_blocked_receipt";
    case durable_store_import::PersistenceRequestOutcome::DeferPartialReceipt:
        return "defer_partial_receipt";
    case durable_store_import::PersistenceRequestOutcome::RejectFailedReceipt:
        return "reject_failed_receipt";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::PersistenceReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::PersistenceReviewNextActionKind::
        HandoffDurableStoreImportDecisionReceiptPersistenceRequest:
        return "handoff_durable_store_import_decision_receipt_persistence_request";
    case durable_store_import::PersistenceReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::PersistenceReviewNextActionKind::
        PersistCompletedDurableStoreImportDecisionReceipt:
        return "persist_completed_durable_store_import_decision_receipt";
    case durable_store_import::PersistenceReviewNextActionKind::
        PreservePartialDurableStoreImportDecisionReceipt:
        return "preserve_partial_durable_store_import_decision_receipt";
    case durable_store_import::PersistenceReviewNextActionKind::
        InvestigateDurableStoreImportDecisionReceiptPersistenceRejection:
        return "investigate_durable_store_import_decision_receipt_persistence_rejection";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::PersistenceBlockerKind kind) {
    switch (kind) {
    case durable_store_import::PersistenceBlockerKind::SourceReceiptBlocked:
        return "source_receipt_blocked";
    case durable_store_import::PersistenceBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::PersistenceBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
    case durable_store_import::PersistenceBlockerKind::WorkflowFailure:
        return "workflow_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_receipt_persistence_blocker(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::PersistenceBlocker> &blocker) {
    line(out, indent_level, "receipt_persistence_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_persistence_review(
    const durable_store_import::PersistenceReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_persistence_request_format " +
             summary
                 .source_durable_store_import_decision_receipt_persistence_request_format_version);
    line(out,
         0,
         "source_receipt_format " +
             summary.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + summary.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "manifest_status " + manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "request_status " + request_status_name(summary.request_status));
    line(out, 0, "decision_status " + decision_status_name(summary.decision_status));
    line(out, 0, "receipt_status " + receipt_status_name(summary.receipt_status));
    line(out,
         0,
         "receipt_persistence_request_status " +
             receipt_persistence_request_status_name(summary.receipt_persistence_request_status));
    line(out,
         0,
         "receipt_boundary_kind " + receipt_boundary_kind_name(summary.receipt_boundary_kind));
    line(out,
         0,
         "receipt_persistence_boundary_kind " +
             receipt_persistence_boundary_kind_name(summary.receipt_persistence_boundary_kind));
    line(out,
         0,
         "receipt_persistence_request_outcome " +
             receipt_persistence_request_outcome_name(summary.receipt_persistence_request_outcome));
    line(out,
         0,
         std::string("accepted_for_receipt_persistence ") +
             (summary.accepted_for_receipt_persistence ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out,
         0,
         "durable_store_import_decision_identity " +
             summary.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + summary.durable_store_import_receipt_identity);
    line(out,
         0,
         "durable_store_import_receipt_persistence_request_identity " +
             summary.durable_store_import_receipt_persistence_request_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out,
         0,
         "adapter_receipt_persistence_contract " +
             summary.adapter_receipt_persistence_contract_summary);
    line(out, 0, "persistence_preview " + summary.persistence_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_receipt_persistence_blocker(out, 0, summary.receipt_persistence_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_persistence_review

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status) {
    switch (status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        return "ready_to_import";
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        return "blocked";
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string request_status_name(durable_store_import::RequestStatus status) {
    switch (status) {
    case durable_store_import::RequestStatus::ReadyForAdapter:
        return "ready_for_adapter";
    case durable_store_import::RequestStatus::Blocked:
        return "blocked";
    case durable_store_import::RequestStatus::TerminalCompleted:
        return "terminal_completed";
    case durable_store_import::RequestStatus::TerminalFailed:
        return "terminal_failed";
    case durable_store_import::RequestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string decision_status_name(durable_store_import::DecisionStatus status) {
    switch (status) {
    case durable_store_import::DecisionStatus::Accepted:
        return "accepted";
    case durable_store_import::DecisionStatus::Blocked:
        return "blocked";
    case durable_store_import::DecisionStatus::Deferred:
        return "deferred";
    case durable_store_import::DecisionStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_status_name(durable_store_import::ReceiptStatus status) {
    switch (status) {
    case durable_store_import::ReceiptStatus::ReadyForArchive:
        return "ready_for_archive";
    case durable_store_import::ReceiptStatus::Blocked:
        return "blocked";
    case durable_store_import::ReceiptStatus::Deferred:
        return "deferred";
    case durable_store_import::ReceiptStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
receipt_boundary_kind_name(durable_store_import::ReceiptBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBoundaryKind::LocalContractOnly:
        return "local_contract_only";
    case durable_store_import::ReceiptBoundaryKind::AdapterReceiptConsumable:
        return "adapter_receipt_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string receipt_outcome_name(durable_store_import::ReceiptOutcome outcome) {
    switch (outcome) {
    case durable_store_import::ReceiptOutcome::ArchiveAcceptedDecision:
        return "archive_accepted_decision";
    case durable_store_import::ReceiptOutcome::BlockBlockedDecision:
        return "block_blocked_decision";
    case durable_store_import::ReceiptOutcome::DeferPartialDecision:
        return "defer_partial_decision";
    case durable_store_import::ReceiptOutcome::RejectFailedDecision:
        return "reject_failed_decision";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::ReceiptReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ReceiptReviewNextActionKind::
        HandoffDurableStoreImportDecisionReceipt:
        return "handoff_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::ResolveRequiredAdapterCapability:
        return "resolve_required_adapter_capability";
    case durable_store_import::ReceiptReviewNextActionKind::
        ArchiveCompletedDurableStoreImportDecisionReceipt:
        return "archive_completed_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::
        PreservePartialDurableStoreImportDecisionReceipt:
        return "preserve_partial_durable_store_import_decision_receipt";
    case durable_store_import::ReceiptReviewNextActionKind::
        InvestigateDurableStoreImportDecisionReceiptRejection:
        return "investigate_durable_store_import_decision_receipt_rejection";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::ReceiptBlockerKind kind) {
    switch (kind) {
    case durable_store_import::ReceiptBlockerKind::SourceDecisionBlocked:
        return "source_decision_blocked";
    case durable_store_import::ReceiptBlockerKind::MissingRequiredAdapterCapability:
        return "missing_required_adapter_capability";
    case durable_store_import::ReceiptBlockerKind::PartialWorkflowState:
        return "partial_workflow_state";
    case durable_store_import::ReceiptBlockerKind::WorkflowFailure:
        return "workflow_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_receipt_blocker(std::ostream &out,
                           int indent_level,
                           const std::optional<durable_store_import::ReceiptBlocker> &blocker) {
    line(out, indent_level, "receipt_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (blocker->required_capability.has_value()
                                       ? capability_name(*blocker->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_receipt_review(
    const durable_store_import::ReceiptReviewSummary &summary, std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_receipt_format " +
             summary.source_durable_store_import_decision_receipt_format_version);
    line(out,
         0,
         "source_decision_format " + summary.source_durable_store_import_decision_format_version);
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "manifest_status " + manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "request_status " + request_status_name(summary.request_status));
    line(out, 0, "decision_status " + decision_status_name(summary.decision_status));
    line(out, 0, "receipt_status " + receipt_status_name(summary.receipt_status));
    line(out,
         0,
         "receipt_boundary_kind " + receipt_boundary_kind_name(summary.receipt_boundary_kind));
    line(out, 0, "receipt_outcome " + receipt_outcome_name(summary.receipt_outcome));
    line(out,
         0,
         std::string("accepted_for_receipt_archive ") +
             (summary.accepted_for_receipt_archive ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out,
         0,
         "durable_store_import_decision_identity " +
             summary.durable_store_import_decision_identity);
    line(out,
         0,
         "durable_store_import_receipt_identity " + summary.durable_store_import_receipt_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "next_required_adapter_capability " +
             (summary.next_required_adapter_capability.has_value()
                  ? capability_name(*summary.next_required_adapter_capability)
                  : std::string("none")));
    line(out, 0, "adapter_receipt_contract " + summary.adapter_receipt_contract_summary);
    line(out, 0, "receipt_preview " + summary.receipt_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_receipt_blocker(out, 0, summary.receipt_blocker);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_receipt_review

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_recovery_preview {
namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string
response_status_name(durable_store_import::PersistenceResponseStatus status) {
    switch (status) {
    case durable_store_import::PersistenceResponseStatus::Accepted:
        return "accepted";
    case durable_store_import::PersistenceResponseStatus::Blocked:
        return "blocked";
    case durable_store_import::PersistenceResponseStatus::Deferred:
        return "deferred";
    case durable_store_import::PersistenceResponseStatus::Rejected:
        return "rejected";
    }

    return "invalid";
}

[[nodiscard]] std::string
response_outcome_name(durable_store_import::PersistenceResponseOutcome outcome) {
    switch (outcome) {
    case durable_store_import::PersistenceResponseOutcome::AcceptPersistenceRequest:
        return "accept_persistence_request";
    case durable_store_import::PersistenceResponseOutcome::BlockBlockedRequest:
        return "block_blocked_request";
    case durable_store_import::PersistenceResponseOutcome::DeferDeferredRequest:
        return "defer_deferred_request";
    case durable_store_import::PersistenceResponseOutcome::RejectFailedRequest:
        return "reject_failed_request";
    }

    return "invalid";
}

[[nodiscard]] std::string mutation_status_name(durable_store_import::StoreMutationStatus status) {
    switch (status) {
    case durable_store_import::StoreMutationStatus::Persisted:
        return "persisted";
    case durable_store_import::StoreMutationStatus::NotMutated:
        return "not_mutated";
    }

    return "invalid";
}

[[nodiscard]] std::string
next_action_name(durable_store_import::RecoveryPreviewNextActionKind action) {
    switch (action) {
    case durable_store_import::RecoveryPreviewNextActionKind::NoRecoveryRequired:
        return "no_recovery_required";
    case durable_store_import::RecoveryPreviewNextActionKind::ResolveBlocker:
        return "resolve_blocker";
    case durable_store_import::RecoveryPreviewNextActionKind::WaitForCapability:
        return "wait_for_capability";
    case durable_store_import::RecoveryPreviewNextActionKind::ReviewFailure:
        return "review_failure";
    }

    return "invalid";
}

[[nodiscard]] std::string capability_name(durable_store_import::AdapterCapabilityKind capability) {
    switch (capability) {
    case durable_store_import::AdapterCapabilityKind::ConsumeStoreImportDescriptor:
        return "consume_store_import_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeExportManifest:
        return "consume_export_manifest";
    case durable_store_import::AdapterCapabilityKind::ConsumePersistenceDescriptor:
        return "consume_persistence_descriptor";
    case durable_store_import::AdapterCapabilityKind::ConsumeHumanReviewContext:
        return "consume_human_review_context";
    case durable_store_import::AdapterCapabilityKind::ConsumeCheckpointRecord:
        return "consume_checkpoint_record";
    case durable_store_import::AdapterCapabilityKind::PreservePartialWorkflowState:
        return "preserve_partial_workflow_state";
    }

    return "invalid";
}

[[nodiscard]] std::string
failure_kind_name(durable_store_import::AdapterExecutionFailureKind kind) {
    switch (kind) {
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseBlocked:
        return "source_response_blocked";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseDeferred:
        return "source_response_deferred";
    case durable_store_import::AdapterExecutionFailureKind::SourceResponseRejected:
        return "source_response_rejected";
    }

    return "invalid";
}

void print_failure_attribution(
    std::ostream &out,
    int indent_level,
    const std::optional<durable_store_import::AdapterExecutionFailureAttribution> &failure) {
    line(out, indent_level, "failure_attribution {");
    if (!failure.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(failure->kind));
    line(out,
         indent_level + 1,
         "required_capability " + (failure->required_capability.has_value()
                                       ? capability_name(*failure->required_capability)
                                       : std::string("none")));
    line(out, indent_level + 1, "message " + failure->message);
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_recovery_preview(
    const durable_store_import::RecoveryCommandPreview &preview, std::ostream &out) {
    out << preview.format_version << '\n';
    line(out,
         0,
         "source_adapter_execution_format " +
             preview.source_durable_store_import_adapter_execution_format_version);
    line(out,
         0,
         "source_response_format " +
             preview
                 .source_durable_store_import_decision_receipt_persistence_response_format_version);
    line(out, 0, "workflow " + preview.workflow_canonical_name);
    line(out, 0, "session " + preview.session_id);
    line(out, 0, "run_id " + (preview.run_id.has_value() ? *preview.run_id : "none"));
    line(out, 0, "input_fixture " + preview.input_fixture);
    line(out,
         0,
         "durable_store_import_receipt_persistence_response_identity " +
             preview.durable_store_import_receipt_persistence_response_identity);
    line(out,
         0,
         "durable_store_import_adapter_execution_identity " +
             preview.durable_store_import_adapter_execution_identity);
    line(out, 0, "response_status " + response_status_name(preview.response_status));
    line(out, 0, "response_outcome " + response_outcome_name(preview.response_outcome));
    line(out, 0, "mutation_status " + mutation_status_name(preview.mutation_status));
    line(out,
         0,
         "persistence_id " +
             (preview.persistence_id.has_value() ? *preview.persistence_id : std::string("none")));
    line(out, 0, "next_action " + next_action_name(preview.next_action));
    line(out, 0, "recovery_boundary " + preview.recovery_boundary_summary);
    line(out, 0, "next_step " + preview.next_step_recommendation);
    line(out, 0, "execution_summary {");
    line(out,
         1,
         "adapter_execution_identity " + preview.execution_summary.adapter_execution_identity);
    line(out,
         1,
         "mutation_status " + mutation_status_name(preview.execution_summary.mutation_status));
    line(out,
         1,
         "persistence_id " + (preview.execution_summary.persistence_id.has_value()
                                  ? *preview.execution_summary.persistence_id
                                  : std::string("none")));
    line(out, 0, "}");
    print_failure_attribution(out, 0, preview.failure_attribution);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_recovery_preview

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_request {

namespace {

class DurableStoreImportRequestJsonPrinter final : private ArtifactJsonWriter {
  public:
    explicit DurableStoreImportRequestJsonPrinter(std::ostream &out) : ArtifactJsonWriter(out) {}

    void print(const durable_store_import::Request &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("source_store_import_descriptor_format_version",
                  [&]() { write_string(request.source_store_import_descriptor_format_version); });
            field("source_execution_plan_format_version",
                  [&]() { write_string(request.source_execution_plan_format_version); });
            field("source_runtime_session_format_version",
                  [&]() { write_string(request.source_runtime_session_format_version); });
            field("source_execution_journal_format_version",
                  [&]() { write_string(request.source_execution_journal_format_version); });
            field("source_replay_view_format_version",
                  [&]() { write_string(request.source_replay_view_format_version); });
            field("source_scheduler_snapshot_format_version",
                  [&]() { write_string(request.source_scheduler_snapshot_format_version); });
            field("source_checkpoint_record_format_version",
                  [&]() { write_string(request.source_checkpoint_record_format_version); });
            field("source_persistence_descriptor_format_version",
                  [&]() { write_string(request.source_persistence_descriptor_format_version); });
            field("source_export_manifest_format_version",
                  [&]() { write_string(request.source_export_manifest_format_version); });
            field("source_package_identity", [&]() {
                print_optional(request.source_package_identity,
                               [&](const auto &value) { print_package_identity(value, 1); });
            });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("workflow_status", [&]() { print_workflow_status(request.workflow_status); });
            field("checkpoint_status",
                  [&]() { print_checkpoint_status(request.checkpoint_status); });
            field("persistence_status",
                  [&]() { print_persistence_status(request.persistence_status); });
            field("manifest_status", [&]() { print_manifest_status(request.manifest_status); });
            field("descriptor_status",
                  [&]() { print_descriptor_status(request.descriptor_status); });
            field("workflow_failure_summary", [&]() {
                print_optional(request.workflow_failure_summary,
                               [&](const auto &value) { print_failure_summary(value, 1); });
            });
            field("export_package_identity",
                  [&]() { write_string(request.export_package_identity); });
            field("store_import_candidate_identity",
                  [&]() { write_string(request.store_import_candidate_identity); });
            field("durable_store_import_request_identity",
                  [&]() { write_string(request.durable_store_import_request_identity); });
            field("planned_durable_identity",
                  [&]() { write_string(request.planned_durable_identity); });
            field("request_boundary_kind",
                  [&]() { print_request_boundary_kind(request.request_boundary_kind); });
            field("requested_artifact_set",
                  [&]() { print_requested_artifact_set(request.requested_artifact_set, 1); });
            field("adapter_ready", [&]() { write_bool(request.adapter_ready); });
            field("next_required_adapter_artifact_kind", [&]() {
                print_optional(request.next_required_adapter_artifact_kind,
                               [&](const auto &value) { print_requested_artifact_kind(value); });
            });
            field("request_status", [&]() { print_request_status(request.request_status); });
            field("adapter_blocker", [&]() {
                print_optional(request.adapter_blocker,
                               [&](const auto &value) { print_request_blocker(value, 1); });
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

    void print_failure_summary(const runtime_session::RuntimeFailureSummary &summary,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() {
                switch (summary.kind) {
                case runtime_session::RuntimeFailureKind::MockMissing:
                    write_string("mock_missing");
                    return;
                case runtime_session::RuntimeFailureKind::NodeFailed:
                    write_string("node_failed");
                    return;
                case runtime_session::RuntimeFailureKind::WorkflowFailed:
                    write_string("workflow_failed");
                    return;
                }
            });
            field("node_name", [&]() { print_optional_string(summary.node_name); });
            field("message", [&]() { write_string(summary.message); });
        });
    }

    void print_workflow_status(runtime_session::WorkflowSessionStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Completed, "completed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Failed, "failed");
            AHFL_ARTIFACT_ENUM_CASE(runtime_session::WorkflowSessionStatus::Partial, "partial"));
    }

    void print_checkpoint_status(checkpoint_record::CheckpointRecordStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::ReadyToPersist,
                                    "ready_to_persist");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(checkpoint_record::CheckpointRecordStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_persistence_status(persistence_descriptor::PersistenceDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport,
                "ready_to_export");
            AHFL_ARTIFACT_ENUM_CASE(persistence_descriptor::PersistenceDescriptorStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_manifest_status(persistence_export::PersistenceExportManifestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::ReadyToImport,
                "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(persistence_export::PersistenceExportManifestStatus::Blocked,
                                    "blocked");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalCompleted,
                "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalFailed,
                "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(
                persistence_export::PersistenceExportManifestStatus::TerminalPartial,
                "terminal_partial"));
    }

    void print_descriptor_status(store_import::StoreImportDescriptorStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::ReadyToImport,
                                    "ready_to_import");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(store_import::StoreImportDescriptorStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_boundary_kind(durable_store_import::RequestBoundaryKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBoundaryKind::LocalIntentOnly,
                                    "local_intent_only");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBoundaryKind::AdapterContractConsumable,
                "adapter_contract_consumable"));
    }

    void print_requested_artifact_kind(durable_store_import::RequestedArtifactKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactKind::StoreImportDescriptor,
                "store_import_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::ExportManifest,
                                    "export_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactKind::PersistenceDescriptor,
                "persistence_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::PersistenceReview,
                                    "persistence_review");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestedArtifactKind::CheckpointRecord,
                                    "checkpoint_record"));
    }

    void print_adapter_role(durable_store_import::RequestedArtifactAdapterRole role) {
        AHFL_ARTIFACT_PRINT_ENUM(
            role,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::RequestAnchor,
                "request_anchor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::ImportManifest,
                "import_manifest");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::DurableStateDescriptor,
                "durable_state_descriptor");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::HumanReviewContext,
                "human_review_context");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestedArtifactAdapterRole::CheckpointPayload,
                "checkpoint_payload"));
    }

    void print_request_status(durable_store_import::RequestStatus status) {
        AHFL_ARTIFACT_PRINT_ENUM(
            status,
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::ReadyForAdapter,
                                    "ready_for_adapter");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::Blocked, "blocked");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalCompleted,
                                    "terminal_completed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalFailed,
                                    "terminal_failed");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestStatus::TerminalPartial,
                                    "terminal_partial"));
    }

    void print_request_blocker_kind(durable_store_import::RequestBlockerKind kind) {
        AHFL_ARTIFACT_PRINT_ENUM(
            kind,
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::WaitingOnRequestedArtifact,
                "waiting_on_requested_artifact");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::MissingRequestIdentity,
                "missing_durable_store_import_request_identity");
            AHFL_ARTIFACT_ENUM_CASE(
                durable_store_import::RequestBlockerKind::MissingRequestedArtifactSet,
                "missing_requested_artifact_set");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBlockerKind::WorkflowFailure,
                                    "workflow_failure");
            AHFL_ARTIFACT_ENUM_CASE(durable_store_import::RequestBlockerKind::WorkflowPartial,
                                    "workflow_partial"));
    }

    void print_requested_artifact_entry(const durable_store_import::RequestedArtifactEntry &entry,
                                        int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("artifact_kind", [&]() { print_requested_artifact_kind(entry.artifact_kind); });
            field("logical_artifact_name", [&]() { write_string(entry.logical_artifact_name); });
            field("source_format_version", [&]() { write_string(entry.source_format_version); });
            field("required_for_import", [&]() { write_bool(entry.required_for_import); });
            field("adapter_role", [&]() { print_adapter_role(entry.adapter_role); });
        });
    }

    void
    print_requested_artifact_set(const durable_store_import::RequestedArtifactSet &artifact_set,
                                 int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("entry_count", [&]() { out_ << artifact_set.entry_count; });
            field("entries", [&]() {
                print_array(indent_level + 1, [&](const auto &item) {
                    for (const auto &entry : artifact_set.entries) {
                        item([&]() { print_requested_artifact_entry(entry, indent_level + 2); });
                    }
                });
            });
        });
    }

    void print_request_blocker(const durable_store_import::RequestBlocker &blocker,
                               int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("kind", [&]() { print_request_blocker_kind(blocker.kind); });
            field("logical_artifact_name",
                  [&]() { print_optional_string(blocker.logical_artifact_name); });
            field("message", [&]() { write_string(blocker.message); });
        });
    }
};

} // namespace

void print_durable_store_import_request_json(const durable_store_import::Request &request,
                                             std::ostream &out) {
    DurableStoreImportRequestJsonPrinter(out).print(request);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_request

namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_review {

namespace {

void line(std::ostream &out, int indent_level, std::string_view text) {
    out << std::string(static_cast<std::size_t>(indent_level) * 2, ' ') << text << '\n';
}

[[nodiscard]] std::string workflow_status_name(runtime_session::WorkflowSessionStatus status) {
    switch (status) {
    case runtime_session::WorkflowSessionStatus::Completed:
        return "completed";
    case runtime_session::WorkflowSessionStatus::Failed:
        return "failed";
    case runtime_session::WorkflowSessionStatus::Partial:
        return "partial";
    }

    return "invalid";
}

[[nodiscard]] std::string checkpoint_status_name(checkpoint_record::CheckpointRecordStatus status) {
    switch (status) {
    case checkpoint_record::CheckpointRecordStatus::ReadyToPersist:
        return "ready_to_persist";
    case checkpoint_record::CheckpointRecordStatus::Blocked:
        return "blocked";
    case checkpoint_record::CheckpointRecordStatus::TerminalCompleted:
        return "terminal_completed";
    case checkpoint_record::CheckpointRecordStatus::TerminalFailed:
        return "terminal_failed";
    case checkpoint_record::CheckpointRecordStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
persistence_status_name(persistence_descriptor::PersistenceDescriptorStatus status) {
    switch (status) {
    case persistence_descriptor::PersistenceDescriptorStatus::ReadyToExport:
        return "ready_to_export";
    case persistence_descriptor::PersistenceDescriptorStatus::Blocked:
        return "blocked";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_descriptor::PersistenceDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
manifest_status_name(persistence_export::PersistenceExportManifestStatus status) {
    switch (status) {
    case persistence_export::PersistenceExportManifestStatus::ReadyToImport:
        return "ready_to_import";
    case persistence_export::PersistenceExportManifestStatus::Blocked:
        return "blocked";
    case persistence_export::PersistenceExportManifestStatus::TerminalCompleted:
        return "terminal_completed";
    case persistence_export::PersistenceExportManifestStatus::TerminalFailed:
        return "terminal_failed";
    case persistence_export::PersistenceExportManifestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string descriptor_status_name(store_import::StoreImportDescriptorStatus status) {
    switch (status) {
    case store_import::StoreImportDescriptorStatus::ReadyToImport:
        return "ready_to_import";
    case store_import::StoreImportDescriptorStatus::Blocked:
        return "blocked";
    case store_import::StoreImportDescriptorStatus::TerminalCompleted:
        return "terminal_completed";
    case store_import::StoreImportDescriptorStatus::TerminalFailed:
        return "terminal_failed";
    case store_import::StoreImportDescriptorStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string request_status_name(durable_store_import::RequestStatus status) {
    switch (status) {
    case durable_store_import::RequestStatus::ReadyForAdapter:
        return "ready_for_adapter";
    case durable_store_import::RequestStatus::Blocked:
        return "blocked";
    case durable_store_import::RequestStatus::TerminalCompleted:
        return "terminal_completed";
    case durable_store_import::RequestStatus::TerminalFailed:
        return "terminal_failed";
    case durable_store_import::RequestStatus::TerminalPartial:
        return "terminal_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string
request_boundary_kind_name(durable_store_import::RequestBoundaryKind kind) {
    switch (kind) {
    case durable_store_import::RequestBoundaryKind::LocalIntentOnly:
        return "local_intent_only";
    case durable_store_import::RequestBoundaryKind::AdapterContractConsumable:
        return "adapter_contract_consumable";
    }

    return "invalid";
}

[[nodiscard]] std::string artifact_kind_name(durable_store_import::RequestedArtifactKind kind) {
    switch (kind) {
    case durable_store_import::RequestedArtifactKind::StoreImportDescriptor:
        return "store_import_descriptor";
    case durable_store_import::RequestedArtifactKind::ExportManifest:
        return "export_manifest";
    case durable_store_import::RequestedArtifactKind::PersistenceDescriptor:
        return "persistence_descriptor";
    case durable_store_import::RequestedArtifactKind::PersistenceReview:
        return "persistence_review";
    case durable_store_import::RequestedArtifactKind::CheckpointRecord:
        return "checkpoint_record";
    }

    return "invalid";
}

[[nodiscard]] std::string blocker_kind_name(durable_store_import::RequestBlockerKind kind) {
    switch (kind) {
    case durable_store_import::RequestBlockerKind::WaitingOnRequestedArtifact:
        return "waiting_on_requested_artifact";
    case durable_store_import::RequestBlockerKind::MissingRequestIdentity:
        return "missing_durable_store_import_request_identity";
    case durable_store_import::RequestBlockerKind::MissingRequestedArtifactSet:
        return "missing_requested_artifact_set";
    case durable_store_import::RequestBlockerKind::WorkflowFailure:
        return "workflow_failure";
    case durable_store_import::RequestBlockerKind::WorkflowPartial:
        return "workflow_partial";
    }

    return "invalid";
}

[[nodiscard]] std::string next_action_name(durable_store_import::ReviewNextActionKind action) {
    switch (action) {
    case durable_store_import::ReviewNextActionKind::HandoffDurableStoreImportRequest:
        return "handoff_durable_store_import_request";
    case durable_store_import::ReviewNextActionKind::AwaitAdapterReadiness:
        return "await_adapter_readiness";
    case durable_store_import::ReviewNextActionKind::ArchiveCompletedDurableStoreImportState:
        return "archive_completed_durable_store_import_state";
    case durable_store_import::ReviewNextActionKind::InvestigateDurableStoreImportFailure:
        return "investigate_durable_store_import_failure";
    case durable_store_import::ReviewNextActionKind::PreservePartialDurableStoreImportState:
        return "preserve_partial_durable_store_import_state";
    }

    return "invalid";
}

[[nodiscard]] std::string failure_kind_name(runtime_session::RuntimeFailureKind kind) {
    switch (kind) {
    case runtime_session::RuntimeFailureKind::MockMissing:
        return "mock_missing";
    case runtime_session::RuntimeFailureKind::NodeFailed:
        return "node_failed";
    case runtime_session::RuntimeFailureKind::WorkflowFailed:
        return "workflow_failed";
    }

    return "invalid";
}

void print_failure_summary(std::ostream &out,
                           int indent_level,
                           std::string_view label,
                           const std::optional<runtime_session::RuntimeFailureSummary> &summary) {
    line(out, indent_level, std::string(label) + " {");
    if (!summary.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + failure_kind_name(summary->kind));
    line(out,
         indent_level + 1,
         "node_name " + (summary->node_name.has_value() ? *summary->node_name : "none"));
    line(out, indent_level + 1, "message " + summary->message);
    line(out, indent_level, "}");
}

void print_adapter_blocker(std::ostream &out,
                           int indent_level,
                           const std::optional<durable_store_import::RequestBlocker> &blocker) {
    line(out, indent_level, "adapter_blocker {");
    if (!blocker.has_value()) {
        line(out, indent_level + 1, "value none");
        line(out, indent_level, "}");
        return;
    }

    line(out, indent_level + 1, "kind " + blocker_kind_name(blocker->kind));
    line(out,
         indent_level + 1,
         "logical_artifact_name " + (blocker->logical_artifact_name.has_value()
                                         ? *blocker->logical_artifact_name
                                         : "none"));
    line(out, indent_level + 1, "message " + blocker->message);
    line(out, indent_level, "}");
}

void print_string_list(std::ostream &out,
                       int indent_level,
                       std::string_view label,
                       const std::vector<std::string> &values) {
    line(out, indent_level, std::string(label) + " {");
    line(out, indent_level + 1, "count " + std::to_string(values.size()));
    if (values.empty()) {
        line(out, indent_level + 1, "- none");
    } else {
        for (const auto &value : values) {
            line(out, indent_level + 1, "- " + value);
        }
    }
    line(out, indent_level, "}");
}

} // namespace

void print_durable_store_import_review(const durable_store_import::ReviewSummary &summary,
                                       std::ostream &out) {
    out << summary.format_version << '\n';
    line(out,
         0,
         "source_request_format " + summary.source_durable_store_import_request_format_version);
    line(out,
         0,
         "source_descriptor_format " + summary.source_store_import_descriptor_format_version);
    line(out, 0, "source_manifest_format " + summary.source_export_manifest_format_version);
    line(out, 0, "workflow " + summary.workflow_canonical_name);
    line(out, 0, "session " + summary.session_id);
    line(out, 0, "run_id " + (summary.run_id.has_value() ? *summary.run_id : "none"));
    line(out, 0, "input_fixture " + summary.input_fixture);
    line(out, 0, "workflow_status " + workflow_status_name(summary.workflow_status));
    line(out, 0, "checkpoint_status " + checkpoint_status_name(summary.checkpoint_status));
    line(out, 0, "persistence_status " + persistence_status_name(summary.persistence_status));
    line(out, 0, "manifest_status " + manifest_status_name(summary.manifest_status));
    line(out, 0, "descriptor_status " + descriptor_status_name(summary.descriptor_status));
    line(out, 0, "request_status " + request_status_name(summary.request_status));
    line(out, 0, "export_package_identity " + summary.export_package_identity);
    line(out, 0, "store_import_candidate_identity " + summary.store_import_candidate_identity);
    line(out,
         0,
         "durable_store_import_request_identity " + summary.durable_store_import_request_identity);
    line(out, 0, "planned_durable_identity " + summary.planned_durable_identity);
    line(out,
         0,
         "request_boundary_kind " + request_boundary_kind_name(summary.request_boundary_kind));
    line(out, 0, std::string("adapter_ready ") + (summary.adapter_ready ? "true" : "false"));
    line(out, 0, "next_action " + next_action_name(summary.next_action));
    line(out, 0, "adapter_boundary " + summary.adapter_boundary_summary);
    line(out, 0, "request_preview " + summary.request_preview);
    line(out, 0, "next_step " + summary.next_step_recommendation);
    line(out,
         0,
         "next_required_adapter_artifact " +
             (summary.next_required_adapter_artifact_kind.has_value()
                  ? artifact_kind_name(*summary.next_required_adapter_artifact_kind)
                  : "none"));

    print_failure_summary(out, 0, "workflow_failure_summary", summary.workflow_failure_summary);
    print_adapter_blocker(out, 0, summary.adapter_blocker);
    print_string_list(out, 0, "requested_artifact_preview", summary.requested_artifact_preview);
}

} // namespace ahfl::durable_store_import_artifacts_detail::durable_store_import_review

namespace ahfl {

namespace {

constexpr DurableStoreImportArtifactPrinterDescriptor kDurableStoreImportArtifactPrinters[] = {
#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    {#public_name,                                                                                 \
     #artifact_type,                                                                               \
     #detail_namespace,                                                                            \
     #detail_name,                                                                                 \
     artifact_id,                                                                                  \
     output_format,                                                                                \
     domain},
#include "ahfl/durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER
};

} // namespace

std::string_view
durable_store_import_artifact_output_format_name(DurableStoreImportArtifactOutputFormat format) {
    switch (format) {
    case DurableStoreImportArtifactOutputFormat::Json:
        return "json";
    case DurableStoreImportArtifactOutputFormat::Text:
        return "text";
    }
    return "unknown";
}

std::string_view
durable_store_import_artifact_domain_name(DurableStoreImportArtifactDomain domain) {
    switch (domain) {
    case DurableStoreImportArtifactDomain::CoreWorkflow:
        return "core_workflow";
    case DurableStoreImportArtifactDomain::Persistence:
        return "persistence";
    case DurableStoreImportArtifactDomain::ProviderConfiguration:
        return "provider_configuration";
    case DurableStoreImportArtifactDomain::ProviderGovernance:
        return "provider_governance";
    case DurableStoreImportArtifactDomain::ProviderHostExecution:
        return "provider_host_execution";
    case DurableStoreImportArtifactDomain::ProviderReliability:
        return "provider_reliability";
    case DurableStoreImportArtifactDomain::ProviderRuntime:
        return "provider_runtime";
    case DurableStoreImportArtifactDomain::ProviderSdk:
        return "provider_sdk";
    }
    return "unknown";
}

std::span<const DurableStoreImportArtifactPrinterDescriptor>
durable_store_import_artifact_printers() {
    return std::span<const DurableStoreImportArtifactPrinterDescriptor>{
        kDurableStoreImportArtifactPrinters};
}

#define AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER(public_name,                                    \
                                                   artifact_type,                                  \
                                                   parameter_name,                                 \
                                                   detail_namespace,                               \
                                                   detail_name,                                    \
                                                   output_format,                                  \
                                                   domain,                                         \
                                                   artifact_id)                                    \
    void public_name(const durable_store_import::artifact_type &parameter_name,                    \
                     std::ostream &out) {                                                          \
        durable_store_import_artifacts_detail::detail_namespace::detail_name(parameter_name, out); \
    }
#include "ahfl/durable_store_import/artifact_printers.def"
#undef AHFL_DURABLE_STORE_IMPORT_ARTIFACT_PRINTER

} // namespace ahfl
