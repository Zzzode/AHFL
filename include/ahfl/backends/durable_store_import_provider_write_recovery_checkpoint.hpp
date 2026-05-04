#pragma once

#include "ahfl/durable_store_import/provider_recovery.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_write_recovery_checkpoint_json(
    const durable_store_import::ProviderWriteRecoveryCheckpoint &checkpoint, std::ostream &out);

} // namespace ahfl
