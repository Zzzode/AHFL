#include "ahfl/backends/durable_store_import_provider_failure_taxonomy_review.hpp"

#include <ostream>

namespace ahfl {
namespace {

void print_kind(durable_store_import::ProviderFailureKindV1 kind, std::ostream &out) {
    switch (kind) {
    case durable_store_import::ProviderFailureKindV1::None:
        out << "none";
        return;
    case durable_store_import::ProviderFailureKindV1::Authentication:
        out << "authentication";
        return;
    case durable_store_import::ProviderFailureKindV1::Authorization:
        out << "authorization";
        return;
    case durable_store_import::ProviderFailureKindV1::Network:
        out << "network";
        return;
    case durable_store_import::ProviderFailureKindV1::Throttling:
        out << "throttling";
        return;
    case durable_store_import::ProviderFailureKindV1::Conflict:
        out << "conflict";
        return;
    case durable_store_import::ProviderFailureKindV1::SchemaMismatch:
        out << "schema_mismatch";
        return;
    case durable_store_import::ProviderFailureKindV1::ProviderInternal:
        out << "provider_internal";
        return;
    case durable_store_import::ProviderFailureKindV1::Unknown:
        out << "unknown";
        return;
    case durable_store_import::ProviderFailureKindV1::Blocked:
        out << "blocked";
        return;
    }
}

void print_retryability(durable_store_import::ProviderFailureRetryabilityV1 retryability,
                        std::ostream &out) {
    switch (retryability) {
    case durable_store_import::ProviderFailureRetryabilityV1::NotApplicable:
        out << "not_applicable";
        return;
    case durable_store_import::ProviderFailureRetryabilityV1::Retryable:
        out << "retryable";
        return;
    case durable_store_import::ProviderFailureRetryabilityV1::NonRetryable:
        out << "non_retryable";
        return;
    case durable_store_import::ProviderFailureRetryabilityV1::DuplicateReviewRequired:
        out << "duplicate_review_required";
        return;
    case durable_store_import::ProviderFailureRetryabilityV1::Blocked:
        out << "blocked";
        return;
    }
}

} // namespace

void print_durable_store_import_provider_failure_taxonomy_review(
    const durable_store_import::ProviderFailureTaxonomyReview &review, std::ostream &out) {
    out << review.format_version << '\n';
    out << "workflow " << review.workflow_canonical_name << '\n';
    out << "session " << review.session_id << '\n';
    out << "taxonomy_report "
        << review.durable_store_import_provider_failure_taxonomy_report_identity << '\n';
    out << "failure_kind ";
    print_kind(review.failure_kind, out);
    out << '\n';
    out << "retryability ";
    print_retryability(review.retryability, out);
    out << '\n';
    out << "summary " << review.taxonomy_review_summary << '\n';
    out << "operator_recommendation " << review.operator_recommendation << '\n';
}

} // namespace ahfl
