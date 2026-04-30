#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/receipt_review.hpp"

namespace ahfl {

void print_durable_store_import_receipt_review(
    const durable_store_import::ReceiptReviewSummary &summary, std::ostream &out);

} // namespace ahfl
