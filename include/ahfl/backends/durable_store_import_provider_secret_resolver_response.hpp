#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_secret.hpp"

namespace ahfl {

void print_durable_store_import_provider_secret_resolver_response_json(
    const durable_store_import::ProviderSecretResolverResponsePlaceholder &placeholder,
    std::ostream &out);

} // namespace ahfl
