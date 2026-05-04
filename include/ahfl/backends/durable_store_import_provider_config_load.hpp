#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_config.hpp"

namespace ahfl {

void print_durable_store_import_provider_config_load_json(
    const durable_store_import::ProviderConfigLoadPlan &plan, std::ostream &out);

} // namespace ahfl
