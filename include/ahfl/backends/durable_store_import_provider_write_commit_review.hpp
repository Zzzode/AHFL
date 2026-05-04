#pragma once

#include "ahfl/durable_store_import/provider_commit.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_write_commit_review(
    const durable_store_import::ProviderWriteCommitReview &review, std::ostream &out);

} // namespace ahfl
