#pragma once

#include "pipeline/persistence/durable_store_import/receipt.hpp"
#include "pipeline/persistence/durable_store_import/receipt_persistence_response.hpp"

namespace ahfl::durable_store_import {

// Canonical receipt persistence state transition module.
//
// PersistenceRequest and PersistenceResponse remain stable wire projections for
// CLI/golden compatibility. Their transition rules live here so status,
// boundary, blocker, and identity mapping are not reimplemented per projection.
[[nodiscard]] PersistenceRequest
build_receipt_persistence_request_projection(const Receipt &receipt);

[[nodiscard]] PersistenceResponse
build_receipt_persistence_response_projection(const PersistenceRequest &request);

} // namespace ahfl::durable_store_import
