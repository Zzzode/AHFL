#pragma once

#include <ostream>

#include "ahfl/store_import/descriptor.hpp"

namespace ahfl {

void print_store_import_descriptor_json(
    const store_import::StoreImportDescriptor &descriptor,
    std::ostream &out);

} // namespace ahfl
