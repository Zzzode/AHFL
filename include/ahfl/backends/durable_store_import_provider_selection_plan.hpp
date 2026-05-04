#pragma once

#include "ahfl/durable_store_import/provider_registry.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_selection_plan_json(
    const durable_store_import::ProviderSelectionPlan &plan, std::ostream &out);

} // namespace ahfl
