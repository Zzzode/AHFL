#pragma once

#include "ahfl/durable_store_import/provider_adapter.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_recovery_handoff(
    const durable_store_import::ProviderRecoveryHandoffPreview &preview, std::ostream &out);

} // namespace ahfl
