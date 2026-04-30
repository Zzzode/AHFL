#pragma once

#include <ostream>

#include "ahfl/persistence_descriptor/descriptor.hpp"

namespace ahfl {

void print_persistence_descriptor_json(
    const persistence_descriptor::CheckpointPersistenceDescriptor &descriptor, std::ostream &out);

} // namespace ahfl
