#pragma once

#include "ahfl/durable_store_import/provider_registry.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_registry_json(
    const durable_store_import::ProviderRegistry &registry, std::ostream &out);

} // namespace ahfl
