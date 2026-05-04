#include "ahfl/backends/durable_store_import_provider_write_recovery_plan.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class RecoveryPlanJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit RecoveryPlanJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                  [&]() { out_ << (plan.requires_provider_commit_lookup ? "true" : "false"); });
            field("requires_operator_approval",
                  [&]() { out_ << (plan.requires_operator_approval ? "true" : "false"); });
            field("secret_free", [&]() { out_ << (plan.secret_free ? "true" : "false"); });
            field("failure_attribution", [&]() { print_failure(plan.failure_attribution, 1); });
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

    void print_action(durable_store_import::ProviderWriteRecoveryPlanAction action) {
        switch (action) {
        case durable_store_import::ProviderWriteRecoveryPlanAction::NoopCommitted:
            write_string("noop_committed");
            return;
        case durable_store_import::ProviderWriteRecoveryPlanAction::LookupDuplicateCommit:
            write_string("lookup_duplicate_commit");
            return;
        case durable_store_import::ProviderWriteRecoveryPlanAction::ResumeWithIdempotencyKey:
            write_string("resume_with_idempotency_key");
            return;
        case durable_store_import::ProviderWriteRecoveryPlanAction::ManualRemediation:
            write_string("manual_remediation");
            return;
        case durable_store_import::ProviderWriteRecoveryPlanAction::WaitForCommitReceipt:
            write_string("wait_for_commit_receipt");
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

void print_durable_store_import_provider_write_recovery_plan_json(
    const durable_store_import::ProviderWriteRecoveryPlan &plan, std::ostream &out) {
    RecoveryPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl
