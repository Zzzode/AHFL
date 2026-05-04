#include "ahfl/backends/durable_store_import_provider_capability_negotiation_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_selection(durable_store_import::ProviderSelectionStatus status, std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderSelectionStatus::Selected:
        out << "selected";
        return;
    case durable_store_import::ProviderSelectionStatus::FallbackSelected:
        out << "fallback_selected";
        return;
    case durable_store_import::ProviderSelectionStatus::Blocked:
        out << "blocked";
        return;
    }
}

void print_negotiation(durable_store_import::ProviderCapabilityNegotiationStatus status,
                       std::ostream &out) {
    switch (status) {
    case durable_store_import::ProviderCapabilityNegotiationStatus::Compatible:
        out << "compatible";
        return;
    case durable_store_import::ProviderCapabilityNegotiationStatus::Degraded:
        out << "degraded";
        return;
    case durable_store_import::ProviderCapabilityNegotiationStatus::Blocked:
        out << "blocked";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_capability_negotiation_review(
    const durable_store_import::ProviderCapabilityNegotiationReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "selection_plan " << review.durable_store_import_provider_selection_plan_identity
        << '\n';
    out << "selected_provider " << review.selected_provider_id << '\n';
    out << "selection_status ";
    print_selection(review.selection_status, out);
    out << '\n';
    out << "negotiation_status ";
    print_negotiation(review.negotiation_status, out);
    out << '\n';
    out << "summary " << review.negotiation_summary << '\n';
    out << "recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl
