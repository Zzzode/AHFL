#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_plan.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class AlphaPlanJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit AlphaPlanJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("local_filesystem_alpha_plan_identity", [&]() {
                write_string(
                    plan.durable_store_import_provider_local_filesystem_alpha_plan_identity);
            });
            field("plan_status", [&]() { print_status(plan.plan_status); });
            field("provider_key", [&]() { write_string(plan.provider_key); });
            field("real_provider_alpha",
                  [&]() { out_ << (plan.real_provider_alpha ? "true" : "false"); });
            field("fake_adapter_default_path_preserved",
                  [&]() { out_ << (plan.fake_adapter_default_path_preserved ? "true" : "false"); });
            field("opt_in_required", [&]() { out_ << (plan.opt_in_required ? "true" : "false"); });
            field("opt_in_enabled", [&]() { out_ << (plan.opt_in_enabled ? "true" : "false"); });
            field("target_directory", [&]() { print_optional_string(plan.target_directory); });
            field("target_object_name", [&]() { write_string(plan.target_object_name); });
            field("planned_payload_digest", [&]() { write_string(plan.planned_payload_digest); });
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

    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_failure_kind(durable_store_import::ProviderLocalFilesystemAlphaFailureKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalFilesystemAlphaFailureKind::MockReadinessNotReady:
            write_string("mock_readiness_not_ready");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaFailureKind::AlphaPlanNotReady:
            write_string("alpha_plan_not_ready");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaFailureKind::OptInRequired:
            write_string("opt_in_required");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaFailureKind::FilesystemWriteFailed:
            write_string("filesystem_write_failed");
            return;
        }
    }

    void print_failure(
        const std::optional<durable_store_import::ProviderLocalFilesystemAlphaFailureAttribution>
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

void print_durable_store_import_provider_local_filesystem_alpha_plan_json(
    const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan, std::ostream &out) {
    AlphaPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl
