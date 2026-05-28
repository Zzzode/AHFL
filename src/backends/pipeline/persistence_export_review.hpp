#pragma once

#include <ostream>

#include "persistence_export/review.hpp"

namespace ahfl {

void print_persistence_export_review(
    const persistence_export::PersistenceExportReviewSummary &summary, std::ostream &out);

} // namespace ahfl
