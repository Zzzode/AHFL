#include "ahfl/backends/durable_store_import_provider_secret_resolver_request.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class SecretRequestJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit SecretRequestJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
            field("reads_secret_material",
                  [&]() { out_ << (plan.reads_secret_material ? "true" : "false"); });
            field("materializes_secret_value",
                  [&]() { out_ << (plan.materializes_secret_value ? "true" : "false"); });
            field("materializes_credential_material",
                  [&]() { out_ << (plan.materializes_credential_material ? "true" : "false"); });
            field("materializes_token_value",
                  [&]() { out_ << (plan.materializes_token_value ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (plan.opens_network_connection ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (plan.reads_host_environment ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (plan.writes_host_filesystem ? "true" : "false"); });
            field("invokes_secret_manager",
                  [&]() { out_ << (plan.invokes_secret_manager ? "true" : "false"); });
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
    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }

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
        switch (kind) {
        case durable_store_import::ProviderSecretFailureKind::ConfigSnapshotNotReady:
            write_string("config_snapshot_not_ready");
            return;
        case durable_store_import::ProviderSecretFailureKind::SecretResolverRequestNotReady:
            write_string("secret_resolver_request_not_ready");
            return;
        case durable_store_import::ProviderSecretFailureKind::SecretPolicyViolation:
            write_string("secret_policy_violation");
            return;
        }
    }

    void print_handle(const durable_store_import::ProviderSecretHandleReference &handle,
                      int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("handle_schema_version", [&]() { write_string(handle.handle_schema_version); });
            field("handle_identity", [&]() { write_string(handle.handle_identity); });
            field("provider_key", [&]() { write_string(handle.provider_key); });
            field("profile_key", [&]() { write_string(handle.profile_key); });
            field("purpose", [&]() { write_string(handle.purpose); });
            field("contains_secret_value",
                  [&]() { out_ << (handle.contains_secret_value ? "true" : "false"); });
            field("contains_credential_material",
                  [&]() { out_ << (handle.contains_credential_material ? "true" : "false"); });
            field("contains_token_value",
                  [&]() { out_ << (handle.contains_token_value ? "true" : "false"); });
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

} // namespace ahfl
