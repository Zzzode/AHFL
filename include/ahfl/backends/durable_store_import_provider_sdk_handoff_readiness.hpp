#pragma once

#include "ahfl/durable_store_import/provider_sdk.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_sdk_handoff_readiness(
    const durable_store_import::ProviderSdkHandoffReadinessReview &review, std::ostream &out);

} // namespace ahfl
