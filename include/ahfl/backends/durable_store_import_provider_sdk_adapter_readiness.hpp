#pragma once

#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_sdk_adapter_readiness(
    const durable_store_import::ProviderSdkAdapterReadinessReview &review, std::ostream &out);

} // namespace ahfl
