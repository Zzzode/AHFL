#pragma once

#include "ahfl/durable_store_import/provider_compatibility.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_compatibility_test_manifest_json(
    const durable_store_import::ProviderCompatibilityTestManifest &manifest, std::ostream &out);

} // namespace ahfl
