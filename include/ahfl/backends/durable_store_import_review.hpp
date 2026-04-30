#pragma once

#include <ostream>

#include "ahfl/durable_store_import/review.hpp"

namespace ahfl {

void print_durable_store_import_review(
    const durable_store_import::ReviewSummary &summary,
    std::ostream &out);

} // namespace ahfl
