#pragma once

#include "ahfl/durable_store_import/provider_config_bundle_validation.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_config_bundle_validation_report(
    const durable_store_import::ProviderConfigBundleValidationReport &report, std::ostream &out);

} // namespace ahfl
