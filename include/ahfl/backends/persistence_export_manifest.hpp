#pragma once

#include <ostream>

#include "ahfl/persistence_export/manifest.hpp"

namespace ahfl {

void print_persistence_export_manifest_json(
    const persistence_export::PersistenceExportManifest &manifest, std::ostream &out);

} // namespace ahfl
