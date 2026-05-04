#pragma once

#include <iosfwd>

#include "ahfl/durable_store_import/provider_secret.hpp"

namespace ahfl {

void print_durable_store_import_provider_secret_policy_review(
    const durable_store_import::ProviderSecretPolicyReview &review, std::ostream &out);

} // namespace ahfl
