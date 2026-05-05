#pragma once

#include "ahfl/durable_store_import/provider_opt_in_guard.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_opt_in_decision_report(
    const durable_store_import::ProviderOptInDecisionReport &report, std::ostream &out);

} // namespace ahfl
