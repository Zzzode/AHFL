#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/receipt.hpp"

namespace ahfl {

void print_durable_store_import_receipt_json(
    const durable_store_import::DurableStoreImportDecisionReceipt &receipt,
    std::ostream &out);

} // namespace ahfl
