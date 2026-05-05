#pragma once

#include "ahfl/durable_store_import/provider_approval_workflow.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_approval_receipt_json(
    const durable_store_import::ApprovalReceipt &receipt, std::ostream &out);

} // namespace ahfl
