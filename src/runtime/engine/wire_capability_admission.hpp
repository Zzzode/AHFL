#pragma once

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "runtime/engine/capability_bridge.hpp"

#include <expected>
#include <memory>
#include <optional>
#include <string>

// RFC 0026 KR6.5 E4-B0-C2b G4a: the SINGLE admission authority that resolves a
// capability's response wire schema for BOTH the standalone factory
// (make_http_capability / make_grpc_json_transcoding_capability) and the CLI
// Phase-A preflight (a later G4 slice). It folds the four config states plus the
// TextPlain root gate into one function so there is never a second, weaker matcher:
//
//   1. neither legacy `response_schema` nor `response_wire_binding`  -> no schema
//      (engaged nullopt): the caller keeps the historical no-schema behavior.
//   2. `response_wire_binding` only (the projected authority)        -> that binding.
//   3. legacy `response_schema: ir::TypeRef` only                    -> migrated
//      through an EMPTY-Program env (declaration-free / closed / fully-parameterized
//      shapes only); any user nominal / open shape fails closed.
//   4. BOTH present                                                  -> conflict,
//      fail closed (no silent precedence).
//
// After a binding is resolved, a `TextPlain` response format additionally requires
// the binding's verified ROOT to be a String (or BoundedString) node — a text body
// can only decode under a String-rooted schema.
//
// The result is `std::expected<std::optional<VerifiedWireSchemaBinding>, error>`:
// a value carries the resolved binding (or engaged nullopt = state 1), while
// `unexpected` carries a schema-only error string (never any response body / payload
// bytes). The three-field {ok,binding,error} shape is deliberately avoided so an
// illegal combination (e.g. ok=false with a binding) cannot be constructed.

namespace ahfl::runtime {

using WireResponseAdmissionResult =
    std::expected<std::optional<ir::core::VerifiedWireSchemaBinding>, std::string>;

[[nodiscard]] WireResponseAdmissionResult prepare_wire_response_schema(
    CapabilityResponseFormat format,
    const std::shared_ptr<const ir::TypeRef> &legacy_schema,
    const std::optional<ir::core::VerifiedWireSchemaBinding> &response_wire_binding);

} // namespace ahfl::runtime
