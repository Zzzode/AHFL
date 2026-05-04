#pragma once

#include "ahfl/durable_store_import/provider_production_readiness.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_production_readiness_report(
    const durable_store_import::ProviderProductionReadinessReport &report, std::ostream &out);

} // namespace ahfl
