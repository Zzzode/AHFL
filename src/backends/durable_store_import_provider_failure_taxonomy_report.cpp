#include "ahfl/backends/durable_store_import_provider_failure_taxonomy_report.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class FailureTaxonomyJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit FailureTaxonomyJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                  [&]() { out_ << (report.secret_bearing_error_persisted ? "true" : "false"); });
            field("raw_provider_error_persisted",
                  [&]() { out_ << (report.raw_provider_error_persisted ? "true" : "false"); });
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

    void print_kind(durable_store_import::ProviderFailureKindV1 kind) {
        switch (kind) {
        case durable_store_import::ProviderFailureKindV1::None:
            write_string("none");
            return;
        case durable_store_import::ProviderFailureKindV1::Authentication:
            write_string("authentication");
            return;
        case durable_store_import::ProviderFailureKindV1::Authorization:
            write_string("authorization");
            return;
        case durable_store_import::ProviderFailureKindV1::Network:
            write_string("network");
            return;
        case durable_store_import::ProviderFailureKindV1::Throttling:
            write_string("throttling");
            return;
        case durable_store_import::ProviderFailureKindV1::Conflict:
            write_string("conflict");
            return;
        case durable_store_import::ProviderFailureKindV1::SchemaMismatch:
            write_string("schema_mismatch");
            return;
        case durable_store_import::ProviderFailureKindV1::ProviderInternal:
            write_string("provider_internal");
            return;
        case durable_store_import::ProviderFailureKindV1::Unknown:
            write_string("unknown");
            return;
        case durable_store_import::ProviderFailureKindV1::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_category(durable_store_import::ProviderFailureCategoryV1 category) {
        switch (category) {
        case durable_store_import::ProviderFailureCategoryV1::None:
            write_string("none");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Security:
            write_string("security");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Transport:
            write_string("transport");
            return;
        case durable_store_import::ProviderFailureCategoryV1::RateLimit:
            write_string("rate_limit");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Consistency:
            write_string("consistency");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Contract:
            write_string("contract");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Provider:
            write_string("provider");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Unknown:
            write_string("unknown");
            return;
        case durable_store_import::ProviderFailureCategoryV1::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability) {
        switch (retryability) {
        case durable_store_import::ProviderFailureRetryabilityV1::NotApplicable:
            write_string("not_applicable");
            return;
        case durable_store_import::ProviderFailureRetryabilityV1::Retryable:
            write_string("retryable");
            return;
        case durable_store_import::ProviderFailureRetryabilityV1::NonRetryable:
            write_string("non_retryable");
            return;
        case durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired:
            write_string("duplicate_review_required");
            return;
        case durable_store_import::ProviderFailureRetryabilityV1::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_action(durable_store_import::ProviderFailureOperatorActionV1 action) {
        switch (action) {
        case durable_store_import::ProviderFailureOperatorActionV1::None:
            write_string("none");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::RotateCredentials:
            write_string("rotate_credentials");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::GrantPermission:
            write_string("grant_permission");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::RetryLater:
            write_string("retry_later");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::InspectDuplicate:
            write_string("inspect_duplicate");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::FixSchema:
            write_string("fix_schema");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::EscalateProvider:
            write_string("escalate_provider");
            return;
        case durable_store_import::ProviderFailureOperatorActionV1::ManualReview:
            write_string("manual_review");
            return;
        }
    }

    void print_sensitivity(durable_store_import::ProviderFailureSecuritySensitivityV1 sensitivity) {
        switch (sensitivity) {
        case durable_store_import::ProviderFailureSecuritySensitivityV1::None:
            write_string("none");
            return;
        case durable_store_import::ProviderFailureSecuritySensitivityV1::SecretRelated:
            write_string("secret_related");
            return;
        case durable_store_import::ProviderFailureSecuritySensitivityV1::PermissionRelated:
            write_string("permission_related");
            return;
        case durable_store_import::ProviderFailureSecuritySensitivityV1::NonSensitive:
            write_string("non_sensitive");
            return;
        case durable_store_import::ProviderFailureSecuritySensitivityV1::Unknown:
            write_string("unknown");
            return;
        }
    }
};

} // namespace

void print_durable_store_import_provider_failure_taxonomy_report_json(
    const durable_store_import::ProviderFailureTaxonomyReport &report, std::ostream &out) {
    FailureTaxonomyJsonPrinter(out).print(report);
}

} // namespace ahfl
