#pragma once

#include "ahfl/durable_store_import/provider_driver.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_driver_readiness(
    const durable_store_import::ProviderDriverReadinessReview &review, std::ostream &out);

} // namespace ahfl
