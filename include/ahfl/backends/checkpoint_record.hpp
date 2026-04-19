#pragma once

#include <ostream>

#include "ahfl/checkpoint_record/record.hpp"

namespace ahfl {

void print_checkpoint_record_json(const checkpoint_record::CheckpointRecord &record,
                                  std::ostream &out);

} // namespace ahfl
