#pragma once

#include "ahfl/durable_store_import/provider_production_integration_dry_run.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_production_integration_dry_run_report(
    const durable_store_import::ProviderProductionIntegrationDryRunReport &report,
    std::ostream &out);

} // namespace ahfl
