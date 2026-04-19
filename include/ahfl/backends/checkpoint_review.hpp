#pragma once

#include <ostream>

#include "ahfl/checkpoint_record/review.hpp"

namespace ahfl {

void print_checkpoint_review(const checkpoint_record::CheckpointReviewSummary &summary,
                             std::ostream &out);

} // namespace ahfl
