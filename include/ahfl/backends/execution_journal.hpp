#pragma once

#include <ostream>

#include "ahfl/execution_journal/journal.hpp"

namespace ahfl {

void print_execution_journal_json(const execution_journal::ExecutionJournal &journal,
                                  std::ostream &out);

} // namespace ahfl
