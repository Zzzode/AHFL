#pragma once

#include "ahfl/durable_store_import/provider_registry.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_capability_negotiation_review(
    const durable_store_import::ProviderCapabilityNegotiationReview &review, std::ostream &out);

} // namespace ahfl
