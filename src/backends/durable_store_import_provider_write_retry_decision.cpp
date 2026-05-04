#include "ahfl/backends/durable_store_import_provider_write_retry_decision.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class RetryDecisionJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit RetryDecisionJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
            field("retry_allowed", [&]() { out_ << (decision.retry_allowed ? "true" : "false"); });
            field("duplicate_write_possible",
                  [&]() { out_ << (decision.duplicate_write_possible ? "true" : "false"); });
            field("duplicate_detection_summary",
                  [&]() { write_string(decision.duplicate_detection_summary); });
            field("retry_decision_summary",
                  [&]() { write_string(decision.retry_decision_summary); });
            field("failure_attribution", [&]() { print_failure(decision.failure_attribution, 1); });
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

    void print_eligibility(durable_store_import::ProviderWriteRetryEligibility eligibility) {
        switch (eligibility) {
        case durable_store_import::ProviderWriteRetryEligibility::Retryable:
            write_string("retryable");
            return;
        case durable_store_import::ProviderWriteRetryEligibility::NonRetryable:
            write_string("non_retryable");
            return;
        case durable_store_import::ProviderWriteRetryEligibility::DuplicateReviewRequired:
            write_string("duplicate_review_required");
            return;
        case durable_store_import::ProviderWriteRetryEligibility::NotApplicable:
            write_string("not_applicable");
            return;
        case durable_store_import::ProviderWriteRetryEligibility::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderWriteRetryFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderWriteRetryFailureKind::AdapterResultNotReady:
            write_string("adapter_result_not_ready");
            return;
        case durable_store_import::ProviderWriteRetryFailureKind::RetryableProviderFailure:
            write_string("retryable_provider_failure");
            return;
        case durable_store_import::ProviderWriteRetryFailureKind::NonRetryableProviderFailure:
            write_string("non_retryable_provider_failure");
            return;
        case durable_store_import::ProviderWriteRetryFailureKind::DuplicateWriteSuspected:
            write_string("duplicate_write_suspected");
            return;
        }
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

} // namespace ahfl
