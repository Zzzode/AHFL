#pragma once

#include "ahfl/durable_store_import/provider_runtime.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_runtime_preflight_json(
    const durable_store_import::ProviderRuntimePreflightPlan &plan, std::ostream &out);

} // namespace ahfl
