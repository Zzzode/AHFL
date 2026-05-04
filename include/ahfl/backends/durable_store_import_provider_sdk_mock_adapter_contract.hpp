#pragma once

#include "ahfl/durable_store_import/provider_sdk_mock_adapter.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_sdk_mock_adapter_contract_json(
    const durable_store_import::ProviderSdkMockAdapterContract &contract, std::ostream &out);

} // namespace ahfl
