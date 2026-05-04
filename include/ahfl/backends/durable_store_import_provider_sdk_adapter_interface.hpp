#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_sdk_interface.hpp"

namespace ahfl {

void print_durable_store_import_provider_sdk_adapter_interface_json(
    const durable_store_import::ProviderSdkAdapterInterfacePlan &plan, std::ostream &out);

} // namespace ahfl
