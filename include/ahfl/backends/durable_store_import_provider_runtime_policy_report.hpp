#pragma once

#include "ahfl/durable_store_import/provider_runtime_policy.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_runtime_policy_report(
    const durable_store_import::ProviderRuntimePolicyReport &report, std::ostream &out);

} // namespace ahfl
