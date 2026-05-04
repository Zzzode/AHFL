#include "ahfl/backends/durable_store_import_provider_local_filesystem_alpha_result.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class AlphaResultJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit AlphaResultJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderLocalFilesystemAlphaResult &result) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(result.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(result.workflow_canonical_name); });
            field("session_id", [&]() { write_string(result.session_id); });
            field("run_id", [&]() { print_optional_string(result.run_id); });
            field("input_fixture", [&]() { write_string(result.input_fixture); });
            field("local_filesystem_alpha_result_identity", [&]() {
                write_string(
                    result.durable_store_import_provider_local_filesystem_alpha_result_identity);
            });
            field("result_status", [&]() { print_status(result.result_status); });
            field("normalized_result", [&]() { print_result(result.normalized_result); });
            field("provider_commit_reference",
                  [&]() { write_string(result.provider_commit_reference); });
            field("provider_result_digest", [&]() { write_string(result.provider_result_digest); });
            field("wrote_local_file",
                  [&]() { out_ << (result.wrote_local_file ? "true" : "false"); });
            field("opt_in_used", [&]() { out_ << (result.opt_in_used ? "true" : "false"); });
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

    void print_status(durable_store_import::ProviderLocalFilesystemAlphaStatus status) {
        write_string(status == durable_store_import::ProviderLocalFilesystemAlphaStatus::Ready
                         ? "ready"
                         : "blocked");
    }

    void print_result(durable_store_import::ProviderLocalFilesystemAlphaResultKind kind) {
        switch (kind) {
        case durable_store_import::ProviderLocalFilesystemAlphaResultKind::Accepted:
            write_string("accepted");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaResultKind::DryRunOnly:
            write_string("dry_run_only");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaResultKind::WriteFailed:
            write_string("write_failed");
            return;
        case durable_store_import::ProviderLocalFilesystemAlphaResultKind::Blocked:
            write_string("blocked");
            return;
        }
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

void print_durable_store_import_provider_local_filesystem_alpha_result_json(
    const durable_store_import::ProviderLocalFilesystemAlphaResult &result, std::ostream &out) {
    AlphaResultJsonPrinter(out).print(result);
}

} // namespace ahfl
