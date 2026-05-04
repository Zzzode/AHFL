#include "ahfl/backends/durable_store_import_provider_write_commit_receipt.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class CommitReceiptJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit CommitReceiptJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
            field("secret_free", [&]() { out_ << (receipt.secret_free ? "true" : "false"); });
            field("raw_provider_payload_persisted",
                  [&]() { out_ << (receipt.raw_provider_payload_persisted ? "true" : "false"); });
            field("failure_attribution", [&]() { print_failure(receipt.failure_attribution, 1); });
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

    void print_status(durable_store_import::ProviderWriteCommitStatus status) {
        switch (status) {
        case durable_store_import::ProviderWriteCommitStatus::Committed:
            write_string("committed");
            return;
        case durable_store_import::ProviderWriteCommitStatus::Duplicate:
            write_string("duplicate");
            return;
        case durable_store_import::ProviderWriteCommitStatus::Partial:
            write_string("partial");
            return;
        case durable_store_import::ProviderWriteCommitStatus::Failed:
            write_string("failed");
            return;
        case durable_store_import::ProviderWriteCommitStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderWriteCommitFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderWriteCommitFailureKind::AdapterResultNotReady:
            write_string("adapter_result_not_ready");
            return;
        case durable_store_import::ProviderWriteCommitFailureKind::RetryDecisionNotReady:
            write_string("retry_decision_not_ready");
            return;
        case durable_store_import::ProviderWriteCommitFailureKind::ProviderFailure:
            write_string("provider_failure");
            return;
        case durable_store_import::ProviderWriteCommitFailureKind::DuplicateDetected:
            write_string("duplicate_detected");
            return;
        case durable_store_import::ProviderWriteCommitFailureKind::PartialCommit:
            write_string("partial_commit");
            return;
        }
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

} // namespace ahfl
