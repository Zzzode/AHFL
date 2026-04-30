#pragma once

#include "ahfl/durable_store_import/receipt_persistence_response_review.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_receipt_persistence_response_review(
    const durable_store_import::PersistenceResponseReviewSummary &review, std::ostream &out);

} // namespace ahfl
