#pragma once

#include "ahfl/compiler/ir/core_wire_projection.hpp"
#include "runtime/engine/capability_bridge.hpp"

#include <expected>
#include <optional>
#include <string>

// Admission shared by transport factories and CLI preflight. The projected
// verified binding is the only schema input. TextPlain requires a String root;
// an absent binding retains schema-free decoding.

namespace ahfl::runtime {

using WireResponseAdmissionResult =
    std::expected<std::optional<ir::core::VerifiedWireSchemaBinding>, std::string>;

[[nodiscard]] WireResponseAdmissionResult prepare_wire_response_schema(
    CapabilityResponseFormat format,
    const std::optional<ir::core::VerifiedWireSchemaBinding> &response_wire_binding);

} // namespace ahfl::runtime
