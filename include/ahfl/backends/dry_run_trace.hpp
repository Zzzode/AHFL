#pragma once

#include <ostream>

#include "ahfl/dry_run/runner.hpp"

namespace ahfl {

void print_dry_run_trace_json(const dry_run::DryRunTrace &trace, std::ostream &out);

} // namespace ahfl
