#pragma once

#include <ostream>

#include "ahfl/scheduler_snapshot/snapshot.hpp"

namespace ahfl {

void print_scheduler_snapshot_json(const scheduler_snapshot::SchedulerSnapshot &snapshot,
                                   std::ostream &out);

} // namespace ahfl
