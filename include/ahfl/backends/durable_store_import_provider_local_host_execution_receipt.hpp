#pragma once

#include "ahfl/durable_store_import/provider_local_host_execution.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_provider_local_host_execution_receipt_json(
    const durable_store_import::ProviderLocalHostExecutionReceipt &receipt, std::ostream &out);

} // namespace ahfl
