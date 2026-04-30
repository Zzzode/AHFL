#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/receipt_persistence_review.hpp"

namespace ahfl {

void print_durable_store_import_receipt_persistence_review(
    const durable_store_import::PersistenceReviewSummary &summary, std::ostream &out);

} // namespace ahfl
