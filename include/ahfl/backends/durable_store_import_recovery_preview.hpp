#pragma once

#include "ahfl/durable_store_import/recovery_preview.hpp"

#include <ostream>

namespace ahfl {

void print_durable_store_import_recovery_preview(
    const durable_store_import::RecoveryCommandPreview &preview, std::ostream &out);

} // namespace ahfl
