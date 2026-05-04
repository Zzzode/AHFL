#pragma once

#include "ahfl/durable_store_import/provider_compatibility.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_fixture_matrix_json(
    const durable_store_import::ProviderFixtureMatrix &matrix, std::ostream &out);

} // namespace ahfl
