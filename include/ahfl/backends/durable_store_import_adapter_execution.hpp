#pragma once

#include "ahfl/durable_store_import/adapter_execution.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_adapter_execution_json(
    const durable_store_import::AdapterExecutionReceipt &receipt, std::ostream &out);

} // namespace ahfl
