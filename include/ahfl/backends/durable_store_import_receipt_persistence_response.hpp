#pragma once

#include "ahfl/durable_store_import/receipt_persistence_response.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_receipt_persistence_response_json(
    const durable_store_import::PersistenceResponse &response,
    std::ostream &out);

} // namespace ahfl
