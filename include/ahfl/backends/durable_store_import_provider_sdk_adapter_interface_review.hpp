#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_sdk_interface.hpp"

namespace ahfl {

void print_durable_store_import_provider_sdk_adapter_interface_review(
    const durable_store_import::ProviderSdkAdapterInterfaceReview &review, std::ostream &out);

} // namespace ahfl
