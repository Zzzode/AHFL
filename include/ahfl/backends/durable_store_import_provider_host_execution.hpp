#pragma once

#include "ahfl/durable_store_import/provider_host_execution.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_host_execution_json(
    const durable_store_import::ProviderHostExecutionPlan &plan, std::ostream &out);

} // namespace ahfl
