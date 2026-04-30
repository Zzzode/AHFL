#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/decision.hpp"

namespace ahfl {

void print_durable_store_import_decision_json(const durable_store_import::Decision &decision,
                                              std::ostream &out);

} // namespace ahfl
