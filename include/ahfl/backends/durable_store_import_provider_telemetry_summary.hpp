#pragma once

#include "ahfl/durable_store_import/provider_audit.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_telemetry_summary_json(
    const durable_store_import::ProviderTelemetrySummary &summary, std::ostream &out);

} // namespace ahfl
