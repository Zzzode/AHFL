#pragma once

#include "ahfl/durable_store_import/provider_driver.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_driver_binding_json(
    const durable_store_import::ProviderDriverBindingPlan &plan, std::ostream &out);

} // namespace ahfl
