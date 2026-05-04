#include "ahfl/backends/durable_store_import_provider_selection_plan.hpp"

#include "ahfl/support/json.hpp"

#include <optional>
#include <ostream>
#include <string>

namespace ahfl {
namespace {

class SelectionPlanJsonPrinter final : private PrettyJsonWriter {
  public:
    explicit SelectionPlanJsonPrinter(std::ostream &out) : PrettyJsonWriter(out) {}

    void print(const durable_store_import::ProviderSelectionPlan &plan) {
        print_object(0, [&](const auto &field) {
            field("format_version", [&]() { write_string(plan.format_version); });
            field("workflow_canonical_name", [&]() { write_string(plan.workflow_canonical_name); });
            field("session_id", [&]() { write_string(plan.session_id); });
            field("run_id", [&]() { print_optional_string(plan.run_id); });
            field("input_fixture", [&]() { write_string(plan.input_fixture); });
            field("selection_plan_identity", [&]() {
                write_string(plan.durable_store_import_provider_selection_plan_identity);
            });
            field("selected_provider_id", [&]() { write_string(plan.selected_provider_id); });
            field("fallback_provider_id", [&]() { write_string(plan.fallback_provider_id); });
            field("selection_status", [&]() { print_status(plan.selection_status); });
            field("degradation_allowed",
                  [&]() { out_ << (plan.degradation_allowed ? "true" : "false"); });
            field("requires_operator_review",
                  [&]() { out_ << (plan.requires_operator_review ? "true" : "false"); });
            field("fallback_policy", [&]() { write_string(plan.fallback_policy); });
            field("capability_gap_summary", [&]() { write_string(plan.capability_gap_summary); });
        });
        out_ << '\n';
    }

  private:
    void print_status(durable_store_import::ProviderSelectionStatus status) {
        switch (status) {
        case durable_store_import::ProviderSelectionStatus::Selected:
            write_string("selected");
            return;
        case durable_store_import::ProviderSelectionStatus::FallbackSelected:
            write_string("fallback_selected");
            return;
        case durable_store_import::ProviderSelectionStatus::Blocked:
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

void print_durable_store_import_provider_selection_plan_json(
    const durable_store_import::ProviderSelectionPlan &plan, std::ostream &out) {
    SelectionPlanJsonPrinter(out).print(plan);
}

} // namespace ahfl
