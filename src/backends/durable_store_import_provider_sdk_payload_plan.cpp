#include "ahfl/backends/durable_store_import_provider_sdk_payload_plan.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class PayloadPlanJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit PayloadPlanJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
            field("fake_provider_only",
                  [&]() { out_ << (plan.fake_provider_only ? "true" : "false"); });
            field("provider_request_payload_schema_ref",
                  [&]() { write_string(plan.provider_request_payload_schema_ref); });
            field("payload_digest", [&]() { write_string(plan.payload_digest); });
            field("redaction_summary", [&]() { print_redaction(plan.redaction_summary, 1); });
            field("materializes_sdk_request_payload",
                  [&]() { out_ << (plan.materializes_sdk_request_payload ? "true" : "false"); });
            field("persists_materialized_payload",
                  [&]() { out_ << (plan.persists_materialized_payload ? "true" : "false"); });
            field("raw_payload", [&]() { print_optional_string(plan.raw_payload); });
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
        switch (kind) {
        case durable_store_import::ProviderSdkPayloadFailureKind::HarnessNotReady:
            write_string("harness_not_ready");
            return;
        case durable_store_import::ProviderSdkPayloadFailureKind::ForbiddenPayloadMaterial:
            write_string("forbidden_payload_material");
            return;
        case durable_store_import::ProviderSdkPayloadFailureKind::UnsupportedProviderSchema:
            write_string("unsupported_provider_schema");
            return;
        }
    }

    void print_redaction(const durable_store_import::ProviderSdkPayloadRedactionSummary &summary,
                         int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("secret_free", [&]() { out_ << (summary.secret_free ? "true" : "false"); });
            field("credential_free",
                  [&]() { out_ << (summary.credential_free ? "true" : "false"); });
            field("token_free", [&]() { out_ << (summary.token_free ? "true" : "false"); });
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

} // namespace ahfl
