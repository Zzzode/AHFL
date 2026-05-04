#pragma once

#include "ahfl/durable_store_import/provider_local_host_harness.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_local_host_harness_record_json(
    const durable_store_import::ProviderLocalHostHarnessExecutionRecord &record, std::ostream &out);

} // namespace ahfl
