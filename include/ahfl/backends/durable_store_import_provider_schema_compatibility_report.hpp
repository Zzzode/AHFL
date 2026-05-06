#pragma once

#include "ahfl/durable_store_import/provider_schema_compatibility.hpp"

#include <iosfwd>

namespace ahfl {

// 打印 ProviderSchemaCompatibilityReport 为 JSON 格式
void print_durable_store_import_provider_schema_compatibility_report_json(
    const durable_store_import::ProviderSchemaCompatibilityReport &report, std::ostream &out);

} // namespace ahfl
