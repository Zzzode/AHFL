#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/receipt_persistence.hpp"

namespace ahfl {

void print_durable_store_import_receipt_persistence_request_json(
    const durable_store_import::PersistenceRequest &request, std::ostream &out);

} // namespace ahfl
