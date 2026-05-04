#include "ahfl/backends/durable_store_import_provider_config_load.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ProviderConfigLoadJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ProviderConfigLoadJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

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
    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }

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
        field("reads_secret_material",
              [&]() { out_ << (reads_secret_material ? "true" : "false"); });
        field("materializes_secret_value",
              [&]() { out_ << (materializes_secret_value ? "true" : "false"); });
        field("materializes_credential_material",
              [&]() { out_ << (materializes_credential_material ? "true" : "false"); });
        field("opens_network_connection",
              [&]() { out_ << (opens_network_connection ? "true" : "false"); });
        field("reads_host_environment",
              [&]() { out_ << (reads_host_environment ? "true" : "false"); });
        field("writes_host_filesystem",
              [&]() { out_ << (writes_host_filesystem ? "true" : "false"); });
        field("materializes_sdk_request_payload",
              [&]() { out_ << (materializes_sdk_request_payload ? "true" : "false"); });
        field("invokes_provider_sdk", [&]() { out_ << (invokes_provider_sdk ? "true" : "false"); });
    }

    void print_interface_status(durable_store_import::ProviderSdkAdapterInterfaceStatus status) {
        switch (status) {
        case durable_store_import::ProviderSdkAdapterInterfaceStatus::Ready:
            write_string("ready");
            return;
        case durable_store_import::ProviderSdkAdapterInterfaceStatus::Blocked:
            write_string("blocked");
            return;
        }
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

    void print_profile(const durable_store_import::ProviderConfigProfileDescriptor &profile,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("profile_key", [&]() { write_string(profile.profile_key); });
            field("provider_key", [&]() { write_string(profile.provider_key); });
            field("config_schema_version", [&]() { write_string(profile.config_schema_version); });
            field("requires_secret_handle",
                  [&]() { out_ << (profile.requires_secret_handle ? "true" : "false"); });
            field("materializes_secret_value",
                  [&]() { out_ << (profile.materializes_secret_value ? "true" : "false"); });
            field("contains_endpoint_uri",
                  [&]() { out_ << (profile.contains_endpoint_uri ? "true" : "false"); });
            field("opens_network_connection",
                  [&]() { out_ << (profile.opens_network_connection ? "true" : "false"); });
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

} // namespace ahfl
