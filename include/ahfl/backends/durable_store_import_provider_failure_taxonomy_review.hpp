#pragma once

#include "ahfl/durable_store_import/provider_failure_taxonomy.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_failure_taxonomy_review(
    const durable_store_import::ProviderFailureTaxonomyReview &review, std::ostream &out);

} // namespace ahfl
