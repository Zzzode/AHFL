#pragma once

#include "ahfl/durable_store_import/provider_sdk_payload.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_sdk_payload_audit(
    const durable_store_import::ProviderSdkPayloadAuditSummary &audit, std::ostream &out);

} // namespace ahfl
