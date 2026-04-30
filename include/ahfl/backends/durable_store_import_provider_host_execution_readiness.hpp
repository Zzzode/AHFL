#pragma once

#include "ahfl/durable_store_import/provider_host_execution.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_host_execution_readiness(
    const durable_store_import::ProviderHostExecutionReadinessReview &review, std::ostream &out);

} // namespace ahfl
