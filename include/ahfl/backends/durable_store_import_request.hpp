#pragma once

#include <ostream>

#include "ahfl/durable_store_import/request.hpp"

namespace ahfl {

void print_durable_store_import_request_json(const durable_store_import::Request &request,
                                             std::ostream &out);

} // namespace ahfl
