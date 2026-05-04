#include "ahfl/backends/durable_store_import_provider_write_recovery_checkpoint.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class RecoveryCheckpointJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit RecoveryCheckpointJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }

    void print_status(durable_store_import::ProviderWriteRecoveryCheckpointStatus status) {
        switch (status) {
        case durable_store_import::ProviderWriteRecoveryCheckpointStatus::StableCommitted:
            write_string("stable_committed");
            return;
        case durable_store_import::ProviderWriteRecoveryCheckpointStatus::DuplicateReview:
            write_string("duplicate_review");
            return;
        case durable_store_import::ProviderWriteRecoveryCheckpointStatus::PartialWrite:
            write_string("partial_write");
            return;
        case durable_store_import::ProviderWriteRecoveryCheckpointStatus::FailedWrite:
            write_string("failed_write");
            return;
        case durable_store_import::ProviderWriteRecoveryCheckpointStatus::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_eligibility(durable_store_import::ProviderWriteRecoveryEligibility eligibility) {
        switch (eligibility) {
        case durable_store_import::ProviderWriteRecoveryEligibility::NotRequired:
            write_string("not_required");
            return;
        case durable_store_import::ProviderWriteRecoveryEligibility::ResumeRequired:
            write_string("resume_required");
            return;
        case durable_store_import::ProviderWriteRecoveryEligibility::DuplicateLookupRequired:
            write_string("duplicate_lookup_required");
            return;
        case durable_store_import::ProviderWriteRecoveryEligibility::ManualRecoveryRequired:
            write_string("manual_recovery_required");
            return;
        case durable_store_import::ProviderWriteRecoveryEligibility::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderWriteRecoveryFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderWriteRecoveryFailureKind::CommitReceiptNotReady:
            write_string("commit_receipt_not_ready");
            return;
        case durable_store_import::ProviderWriteRecoveryFailureKind::PartialWrite:
            write_string("partial_write");
            return;
        case durable_store_import::ProviderWriteRecoveryFailureKind::DuplicateCommitUnresolved:
            write_string("duplicate_commit_unresolved");
            return;
        case durable_store_import::ProviderWriteRecoveryFailureKind::ProviderFailure:
            write_string("provider_failure");
            return;
        case durable_store_import::ProviderWriteRecoveryFailureKind::Blocked:
            write_string("blocked");
            return;
        }
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

} // namespace ahfl
