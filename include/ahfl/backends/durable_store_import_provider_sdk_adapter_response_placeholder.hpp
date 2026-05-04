#pragma once

#include "ahfl/durable_store_import/provider_sdk_adapter.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_sdk_adapter_response_placeholder_json(
    const durable_store_import::ProviderSdkAdapterResponsePlaceholder &placeholder,
    std::ostream &out);

} // namespace ahfl
