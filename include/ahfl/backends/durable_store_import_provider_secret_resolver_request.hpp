#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_secret.hpp"

namespace ahfl {

void print_durable_store_import_provider_secret_resolver_request_json(
    const durable_store_import::ProviderSecretResolverRequestPlan &plan, std::ostream &out);

} // namespace ahfl
