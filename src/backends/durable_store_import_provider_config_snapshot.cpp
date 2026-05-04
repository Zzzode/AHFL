#include "ahfl/backends/durable_store_import_provider_config_snapshot.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ProviderConfigSnapshotJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ProviderConfigSnapshotJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
                  [&]() { out_ << (placeholder.reads_secret_material ? "true" : "false"); });
            field("materializes_secret_value",
                  [&]() { out_ << (placeholder.materializes_secret_value ? "true" : "false"); });
            field("materializes_credential_material", [&]() {
                out_ << (placeholder.materializes_credential_material ? "true" : "false");
            });
            field("opens_network_connection",
                  [&]() { out_ << (placeholder.opens_network_connection ? "true" : "false"); });
            field("reads_host_environment",
                  [&]() { out_ << (placeholder.reads_host_environment ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (placeholder.writes_host_filesystem ? "true" : "false"); });
            field("materializes_sdk_request_payload", [&]() {
                out_ << (placeholder.materializes_sdk_request_payload ? "true" : "false");
            });
            field("invokes_provider_sdk",
                  [&]() { out_ << (placeholder.invokes_provider_sdk ? "true" : "false"); });
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

    void print_status(durable_store_import::ProviderConfigStatus status) {
        switch (status) {
        case durable_store_import::ProviderConfigStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderConfigStatus::Blocked:
            write_string("blocked");
            return;
        }
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
        switch (kind) {
        case durable_store_import::ProviderConfigFailureKind::AdapterInterfaceNotReady:
            write_string("adapter_interface_not_ready");
            return;
        case durable_store_import::ProviderConfigFailureKind::ConfigLoadNotReady:
            write_string("config_load_not_ready");
            return;
        case durable_store_import::ProviderConfigFailureKind::MissingConfigProfile:
            write_string("missing_config_profile");
            return;
        case durable_store_import::ProviderConfigFailureKind::IncompatibleConfigSchema:
            write_string("incompatible_config_schema");
            return;
        case durable_store_import::ProviderConfigFailureKind::UnsupportedConfigProfile:
            write_string("unsupported_config_profile");
            return;
        }
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

} // namespace ahfl
