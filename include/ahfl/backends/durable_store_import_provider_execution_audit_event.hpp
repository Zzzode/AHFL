#pragma once

#include "ahfl/durable_store_import/provider_audit.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_execution_audit_event_json(
    const durable_store_import::ProviderExecutionAuditEvent &event, std::ostream &out);

} // namespace ahfl
