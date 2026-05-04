#pragma once

#include "ahfl/durable_store_import/provider_commit.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_write_commit_receipt_json(
    const durable_store_import::ProviderWriteCommitReceipt &receipt, std::ostream &out);

} // namespace ahfl
