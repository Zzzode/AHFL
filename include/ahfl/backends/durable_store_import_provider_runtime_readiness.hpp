#pragma once

#include "ahfl/durable_store_import/provider_runtime.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_runtime_readiness(
    const durable_store_import::ProviderRuntimeReadinessReview &review, std::ostream &out);

} // namespace ahfl
