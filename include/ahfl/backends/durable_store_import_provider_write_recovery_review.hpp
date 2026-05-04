#pragma once

#include "ahfl/durable_store_import/provider_recovery.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_write_recovery_review(
    const durable_store_import::ProviderWriteRecoveryReview &review, std::ostream &out);

} // namespace ahfl
