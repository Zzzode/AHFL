#pragma once

#include "ahfl/durable_store_import/provider_retry.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_write_retry_decision_json(
    const durable_store_import::ProviderWriteRetryDecision &decision, std::ostream &out);

} // namespace ahfl
