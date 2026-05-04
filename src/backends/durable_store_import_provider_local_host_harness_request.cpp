#include "ahfl/backends/durable_store_import_provider_local_host_harness_request.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class HarnessRequestJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit HarnessRequestJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(request.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(request.workflow_canonical_name); });
            field("session_id", [&]() { write_string(request.session_id); });
            field("run_id", [&]() { print_optional_string(request.run_id); });
            field("input_fixture", [&]() { write_string(request.input_fixture); });
            field("secret_response_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_secret_resolver_response_identity);
            });
            field("harness_request_identity", [&]() {
                write_string(
                    request.durable_store_import_provider_local_host_harness_request_identity);
            });
            field("operation_kind", [&]() { print_operation(request.operation_kind); });
            field("request_status", [&]() { print_status(request.request_status); });
            field("sandbox_policy", [&]() { print_sandbox(request.sandbox_policy, 1); });
            field("timeout_milliseconds", [&]() { out_ << request.timeout_milliseconds; });
            field("fixture_kind", [&]() { print_fixture(request.fixture_kind); });
            field("opens_network_connection",
                  [&]() { out_ << (request.opens_network_connection ? "true" : "false"); });
            field("reads_secret_material",
                  [&]() { out_ << (request.reads_secret_material ? "true" : "false"); });
            field("writes_host_filesystem",
                  [&]() { out_ << (request.writes_host_filesystem ? "true" : "false"); });
            field("failure_attribution", [&]() { print_failure(request.failure_attribution, 1); });
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

    void print_status(durable_store_import::ProviderLocalHostHarnessStatus status) {
        write_string(status == durable_store_import::ProviderLocalHostHarnessStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_operation(durable_store_import::ProviderLocalHostHarnessOperationKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessOperationKind::PlanHarnessRequest:
            write_string("plan_harness_request");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::RunTestHarness:
            write_string("run_test_harness");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::NoopSecretPolicyNotReady:
            write_string("noop_secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessOperationKind::
            NoopHarnessRequestNotReady:
            write_string("noop_harness_request_not_ready");
            return;
        }
    }

    void print_fixture(durable_store_import::ProviderLocalHostHarnessFixtureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessFixtureKind::Success:
            write_string("success");
            return;
        case durable_store_import::ProviderLocalHostHarnessFixtureKind::NonzeroExit:
            write_string("nonzero_exit");
            return;
        case durable_store_import::ProviderLocalHostHarnessFixtureKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderLocalHostHarnessFixtureKind::SandboxDenied:
            write_string("sandbox_denied");
            return;
        }
    }

    void print_failure_kind(durable_store_import::ProviderLocalHostHarnessFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalHostHarnessFailureKind::SecretPolicyNotReady:
            write_string("secret_policy_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::HarnessRequestNotReady:
            write_string("harness_request_not_ready");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::NonzeroExit:
            write_string("nonzero_exit");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::Timeout:
            write_string("timeout");
            return;
        case durable_store_import::ProviderLocalHostHarnessFailureKind::SandboxDenied:
            write_string("sandbox_denied");
            return;
        }
    }

    void print_sandbox(const durable_store_import::ProviderLocalHostHarnessSandboxPolicy &policy,
                       int indent_level) {
        print_object(indent_level, [&](const auto &field) {
            field("test_only", [&]() { out_ << (policy.test_only ? "true" : "false"); });
            field("allow_network", [&]() { out_ << (policy.allow_network ? "true" : "false"); });
            field("allow_secret", [&]() { out_ << (policy.allow_secret ? "true" : "false"); });
            field("allow_filesystem_write",
                  [&]() { out_ << (policy.allow_filesystem_write ? "true" : "false"); });
            field("allow_host_environment",
                  [&]() { out_ << (policy.allow_host_environment ? "true" : "false"); });
        });
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalHostHarnessFailureAttribution>
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

void print_durable_store_import_provider_local_host_harness_request_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRequest &request,
    std::ostream &out) {
    HarnessRequestJsonPrinter(out).print(request);
}

} // namespace ahfl
