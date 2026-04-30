#pragma once

#include "ahfl/durable_store_import/provider_sdk.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_sdk_envelope_json(
    const durable_store_import::ProviderSdkRequestEnvelopePlan &plan, std::ostream &out);

} // namespace ahfl
