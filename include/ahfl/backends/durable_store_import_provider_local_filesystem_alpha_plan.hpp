#pragma once

#include "ahfl/durable_store_import/provider_local_filesystem_alpha.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_local_filesystem_alpha_plan_json(
    const durable_store_import::ProviderLocalFilesystemAlphaPlan &plan, std::ostream &out);

} // namespace ahfl
