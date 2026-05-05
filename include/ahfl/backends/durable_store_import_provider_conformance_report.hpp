#pragma once

#include "ahfl/durable_store_import/provider_conformance.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_conformance_report(
    const durable_store_import::ProviderConformanceReport &report, std::ostream &out);

} // namespace ahfl
