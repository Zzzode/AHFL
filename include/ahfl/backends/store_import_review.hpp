#pragma once

#include <ostream>

#include "ahfl/store_import/review.hpp"

namespace ahfl {

void print_store_import_review(const store_import::StoreImportReviewSummary &summary,
                               std::ostream &out);

} // namespace ahfl
