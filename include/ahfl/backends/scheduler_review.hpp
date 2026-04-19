#pragma once

#include <ostream>

#include "ahfl/scheduler_snapshot/review.hpp"

namespace ahfl {

void print_scheduler_review(const scheduler_snapshot::SchedulerDecisionSummary &summary,
                            std::ostream &out);

} // namespace ahfl
