#include "ahfl/backends/durable_store_import_provider_secret_resolver_response.hpp"
#include "ahfl/backends/durable_store_import_provider_secret_resolver_request.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class SecretResponseJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit SecretResponseJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                  [&]() { out_ << (placeholder.reads_secret_material ? "true" : "false"); });
            field("materializes_secret_value",
                  [&]() { out_ << (placeholder.materializes_secret_value ? "true" : "false"); });
            field("materializes_credential_material", [&]() {
                out_ << (placeholder.materializes_credential_material ? "true" : "false");
            });
            field("materializes_token_value",
                  [&]() { out_ << (placeholder.materializes_token_value ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (placeholder.opens_network_connection ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (placeholder.reads_host_environment ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (placeholder.writes_host_filesystem ? "true" : "false"); });
            field("invokes_secret_manager",
                  [&]() { out_ << (placeholder.invokes_secret_manager ? "true" : "false"); });
            field("failure_attribution",
                  [&]() { print_failure(placeholder.failure_attribution, 1); });
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
        switch (state) {
        case durable_store_import::ProviderCredentialLifecycleState::HandlePlanned:
            write_string("handle_planned");
            return;
        case durable_store_import::ProviderCredentialLifecycleState::PlaceholderPendingResolution:
            write_string("placeholder_pending_resolution");
            return;
        case durable_store_import::ProviderCredentialLifecycleState::Blocked:
            write_string("blocked");
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

void print_durable_store_import_provider_secret_resolver_response_json(
    const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder,
    std::ostream &out) {
    SecretResponseJsonPrinter(out).print(placeholder);
}

} // namespace ahfl
