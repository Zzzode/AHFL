#pragma once

#include <ostream>

#include "persistence_descriptor/review.hpp"

namespace ahfl {

void print_persistence_review(const persistence_descriptor::PersistenceReviewSummary &summary,
                              std::ostream &out);

} // namespace ahfl
