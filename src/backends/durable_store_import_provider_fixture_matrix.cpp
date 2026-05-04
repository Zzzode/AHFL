#include "ahfl/backends/durable_store_import_provider_fixture_matrix.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class FixtureMatrixJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit FixtureMatrixJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderFixtureMatrix &matrix) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(matrix.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(matrix.workflow_canonical_name); });
            field("session_id", [&]() { write_string(matrix.session_id); });
            field("run_id", [&]() { print_optional_string(matrix.run_id); });
            field("input_fixture", [&]() { write_string(matrix.input_fixture); });
            field("fixture_matrix_identity", [&]() {
                write_string(matrix.durable_store_import_provider_fixture_matrix_identity);
            });
            field("covers_mock_adapter",
                  [&]() { out_ << (matrix.covers_mock_adapter ? "true" : "false"); });
            field("covers_local_filesystem_alpha",
                  [&]() { out_ << (matrix.covers_local_filesystem_alpha ? "true" : "false"); });
            field("covers_success_fixture",
                  [&]() { out_ << (matrix.covers_success_fixture ? "true" : "false"); });
            field("covers_timeout_fixture",
                  [&]() { out_ << (matrix.covers_timeout_fixture ? "true" : "false"); });
            field("covers_conflict_fixture",
                  [&]() { out_ << (matrix.covers_conflict_fixture ? "true" : "false"); });
            field("covers_schema_mismatch_fixture",
                  [&]() { out_ << (matrix.covers_schema_mismatch_fixture ? "true" : "false"); });
            field("provider_count", [&]() { out_ << matrix.provider_count; });
            field("fixture_count", [&]() { out_ << matrix.fixture_count; });
            field("external_service_required",
                  [&]() { out_ << (matrix.external_service_required ? "true" : "false"); });
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
};

} // namespace

void print_durable_store_import_provider_fixture_matrix_json(
    const durable_store_import::ProviderFixtureMatrix &matrix, std::ostream &out) {
    FixtureMatrixJsonPrinter(out).print(matrix);
}

} // namespace ahfl
