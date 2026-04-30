#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/decision_review.hpp"

namespace ahfl {

void print_durable_store_import_decision_review(
    const durable_store_import::DecisionReviewSummary &summary, std::ostream &out);

} // namespace ahfl
