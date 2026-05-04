#pragma once

#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_failure_taxonomy_report_json(
    const durable_store_import::ProviderFailureTaxonomyReport &report, std::ostream &out);

} // namespace ahfl
