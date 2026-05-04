#pragma once

#include "ahfl/durable_store_import/provider_production_readiness.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_production_readiness_review_json(
    const durable_store_import::ProviderProductionReadinessReview &review, std::ostream &out);

} // namespace ahfl
