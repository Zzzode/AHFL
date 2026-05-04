#pragma once

#include "ahfl/durable_store_import/provider_audit.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_operator_review_event(
    const durable_store_import::ProviderOperatorReviewEvent &review, std::ostream &out);

} // namespace ahfl
