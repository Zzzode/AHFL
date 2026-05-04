#include "ahfl/backends/durable_store_import_provider_production_readiness_evidence.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class EvidenceJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit EvidenceJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessEvidence &evidence) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(evidence.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(evidence.workflow_canonical_name); });
            field("session_id", [&]() { write_string(evidence.session_id); });
            field("run_id", [&]() { print_optional_string(evidence.run_id); });
            field("input_fixture", [&]() { write_string(evidence.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    evidence.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("security_evidence_passed",
                  [&]() { out_ << (evidence.security_evidence_passed ? "true" : "false"); });
            field("recovery_evidence_passed",
                  [&]() { out_ << (evidence.recovery_evidence_passed ? "true" : "false"); });
            field("compatibility_evidence_passed",
                  [&]() { out_ << (evidence.compatibility_evidence_passed ? "true" : "false"); });
            field("observability_evidence_passed",
                  [&]() { out_ << (evidence.observability_evidence_passed ? "true" : "false"); });
            field("registry_evidence_passed",
                  [&]() { out_ << (evidence.registry_evidence_passed ? "true" : "false"); });
            field("blocking_issue_count", [&]() { out_ << evidence.blocking_issue_count; });
            field("evidence_summary", [&]() { write_string(evidence.evidence_summary); });
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

void print_durable_store_import_provider_production_readiness_evidence_json(
    const durable_store_import::ProviderProductionReadinessEvidence &evidence, std::ostream &out) {
    EvidenceJsonPrinter(out).print(evidence);
}

} // namespace ahfl
