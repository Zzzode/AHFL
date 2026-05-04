#include "ahfl/backends/durable_store_import_provider_production_readiness_review.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class ReadinessReviewJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit ReadinessReviewJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderProductionReadinessReview &review) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(review.format_version); });
            field("workflow_canonical_name",
                  [&]() { write_string(review.workflow_canonical_name); });
            field("session_id", [&]() { write_string(review.session_id); });
            field("run_id", [&]() { print_optional_string(review.run_id); });
            field("input_fixture", [&]() { write_string(review.input_fixture); });
            field("production_readiness_evidence_identity", [&]() {
                write_string(
                    review.durable_store_import_provider_production_readiness_evidence_identity);
            });
            field("release_gate", [&]() { print_gate(review.release_gate); });
            field("blocking_issue_count", [&]() { out_ << review.blocking_issue_count; });
            field("blocking_issue_summary", [&]() { write_string(review.blocking_issue_summary); });
            field("readiness_summary", [&]() { write_string(review.readiness_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_gate(durable_store_import::ProviderProductionReleaseGate gate) {
        switch (gate) {
        case durable_store_import::ProviderProductionReleaseGate::ReadyForProductionReview:
            write_string("ready_for_production_review");
            return;
        case durable_store_import::ProviderProductionReleaseGate::Conditional:
            write_string("conditional");
            return;
        case durable_store_import::ProviderProductionReleaseGate::Blocked:
            write_string("blocked");
            return;
        }
    }

    void print_optional_string(const std::optional<std::string> &value) {
        if (value.has_value()) {
            write_string(*value);
            return;
        }
        out_ << "null";
    }
};

} // namespace

void print_durable_store_import_provider_production_readiness_review_json(
    const durable_store_import::ProviderProductionReadinessReview &review, std::ostream &out) {
    ReadinessReviewJsonPrinter(out).print(review);
}

} // namespace ahfl
