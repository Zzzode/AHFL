#include "ahfl/backends/durable_store_import_provider_sdk_mock_adapter_execution.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class MockExecutionJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit MockExecutionJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSdkMockAdapterExecutionResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("mock_adapter_contract_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_sdk_mock_adapter_contract_identity);
            });
            field("mock_adapter_result_identity", [&]() {
                write_string(result.durable_store_import_provider_sdk_mock_adapter_result_identity);
            });
            field("operation_kind", [&]() { print_operation(result.operation_kind); });
            field("result_status", [&]() { print_status(result.result_status); });
            field("scenario_kind", [&]() { print_scenario(result.scenario_kind); });
            field("normalized_result", [&]() { print_normalized(result.normalized_result); });
            field("provider_status_code", [&]() { out_ << result.provider_status_code; });
            field("normalized_message", [&]() { write_string(result.normalized_message); });
            field("opens_network_connection",
                  [&]() { out_ << (result.opens_network_connection ? "true" : "false"); });
            field("reads_secret_material",
                  [&]() { out_ << (result.reads_secret_material ? "true" : "false"); });
            field("invokes_real_provider_sdk",
                  [&]() { out_ << (result.invokes_real_provider_sdk ? "true" : "false"); });
            field("failure_attribution", [&]() { print_failure(result.failure_attribution, 1); });
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

    void print_status(durable_store_import::ProviderSdkMockAdapterStatus status) {
        write_string(status == durable_store_import::ProviderSdkMockAdapterStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderSdkMockAdapterOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkMockAdapterOperationKind::PlanMockAdapterContract:
            write_string("plan_mock_adapter_contract");
            return;
        case durable_store_import::ProviderSdkMockAdapterOperationKind::RunMockAdapter:
            write_string("run_mock_adapter");
            return;
        case durable_store_import::ProviderSdkMockAdapterOperationKind::NoopPayloadNotReady:
            write_string("noop_payload_not_ready");
            return;
        case durable_store_import::ProviderSdkMockAdapterOperationKind::NoopContractNotReady:
            write_string("noop_contract_not_ready");
            return;
        }
    }

    void print_scenario(durable_store_import::ProviderSdkMockAdapterScenarioKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::Success:
            write_string("success");
            return;
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::Failure:
            write_string("failure");
            return;
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::Throttle:
            write_string("throttle");
            return;
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::Conflict:
            write_string("conflict");
            return;
        case durable_store_import::ProviderSdkMockAdapterScenarioKind::SchemaMismatch:
            write_string("schema_mismatch");
            return;
        }
    }

    void print_normalized(durable_store_import::ProviderSdkMockAdapterNormalizedResultKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Accepted:
            write_string("accepted");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::ProviderFailure:
            write_string("provider_failure");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Throttled:
            write_string("throttled");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Conflict:
            write_string("conflict");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::SchemaMismatch:
            write_string("schema_mismatch");
            return;
        case durable_store_import::ProviderSdkMockAdapterNormalizedResultKind::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderSdkMockAdapterFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderSdkMockAdapterFailureKind::PayloadNotReady:
            write_string("payload_not_ready");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::ContractNotReady:
            write_string("contract_not_ready");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::ProviderFailure:
            write_string("provider_failure");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::Throttle:
            write_string("throttle");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::Conflict:
            write_string("conflict");
            return;
        case durable_store_import::ProviderSdkMockAdapterFailureKind::SchemaMismatch:
            write_string("schema_mismatch");
            return;
        }
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderSdkMockAdapterFailureAttribution>
            &failure,
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

void print_durable_store_import_provider_sdk_mock_adapter_execution_json(
    const durable_store_import::ProviderSdkMockAdapterExecutionResult &result, std::ostream &out) {
    MockExecutionJsonPrinter(out).print(result);
}

} // namespace ahfl
