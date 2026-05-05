#pragma once

#include "ahfl/durable_store_import/provider_release_evidence_archive.hpp"

#include <iosfwd>

namespace ahfl {

void print_durable_store_import_provider_release_evidence_archive_manifest(
    const durable_store_import::ReleaseEvidenceArchiveManifest &manifest, std::ostream &out);

} // namespace ahfl
