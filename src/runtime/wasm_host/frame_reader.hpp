#pragma once

// RFC 0026 KR6.8 WH-2: the P6-frame reader — the native equivalent of the JS
// oracle's readValue/encodeP6Output. Walks the admitted P4-D layout + wire
// schema over a 64 KiB page to reconstruct a host `runtime::Value`, then
// renders its canonical value_to_json observation.
//
// Discipline (mirrors tests/conformance/node_embedded_host.mjs):
//   * every guest-encoded pointer/len/tag/length is untrusted evidence:
//     bounds AND schema validity are both checked (tag in range, bool word
//     in {0,1}, int repr width honored, collection len <= capacity and
//     placement extent, string len within the schema bounds);
//   * String region authorization is EXACTLY the JS oracle's: accepted String
//     bytes must lie in the union of the input payload-arena span, the rodata
//     [rodata_base,+rodata_extent) span, and the declared bridge-result
//     placements; anything else (scratch, heap gaps, OOB, arena-outside,
//     rodata-outside) fails closed;
//   * runv root authorization: a computed final's value_ptr must equal the
//     output base (12288), an identity final's the input base (1024); the
//     module-kind discriminator comes from the admitted descriptor/frame
//     section, never inferred from the constants.

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "runtime/value/value.hpp"
#include "runtime/wasm_host/frame_walk.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace ahfl::runtime::wasm_host {

/// The P6-frame final-kind discriminator for runv root authorization. Sourced
/// by the driver from the codegen C++ descriptor's frame lane (decision doc
/// section 11.2), never inferred from layout ids or constants.
enum class P6FinalKind { Identity, Computed };

/// Typed fail-closed error family for the P6-frame reader + output encoding.
/// Every error is a fixed enum code; no diagnostic echoes page bytes, decoded
/// names, or values.
enum class FrameReadError {
    NodeIdOutOfRange,
    LayoutIdOutOfRange,
    ShapeMismatch,
    BoolWordInvalid,
    IntOutOfSchemaBounds,
    StringLengthOutOfBounds,
    StringOutsideAuthorizedRegion,
    EnumTagOutOfRange,
    OptionTagInvalid,
    CollectionLengthOutOfRange,
    CollectionOverrunsPlacement,
    SetDuplicateElement,
    RunvRootUnauthorized,
    PageBoundsExceeded,
    ArithmeticOverflow,
};

/// Authorize the runv root and encode the output frame as canonical value
/// JSON. `value_ptr` is the second runv result (the frame base the module
/// named); `final_kind` selects the authorized base (identity -> input base,
/// computed -> output base). Fails closed on any bounds/schema/region/root
/// violation.
[[nodiscard]] std::expected<std::string, FrameReadError>
encode_p6_output(std::span<const std::uint8_t> page,
                 const ahfl::ir::core::CoreFrameLayoutSection &section,
                 const ahfl::ir::core::VerifiedWireSchemaBinding &output_binding,
                 std::uint32_t value_ptr, P6FinalKind final_kind);

/// The generic P4-D value reader: reconstruct a host `runtime::Value` from the
/// fixed-layout binary encoding at `addr` in `page`, walked against the wire
/// node `wId` and layout root `lId`. `string_regions` authorizes every
/// module-written String PtrLen's payload bytes (the caller builds the set
/// appropriate for the walk: the P6 output walk uses the input-payload arena +
/// rodata + bridge result placements; the bridge-arg walk uses the
/// input-payload arena + rodata + every OTHER call site's result placement).
///
/// Exposed (WH-3) so the capability-import bridge executor can walk bridge
/// argument spans without re-implementing the P4-D reader. Every read is
/// bounds- and schema-checked; a violation fails closed with a typed
/// FrameReadError (no echo of page bytes, names, or values).
[[nodiscard]] std::expected<Value, FrameReadError>
read_value_at(FrameWalkContext &ctx, std::span<const std::uint8_t> page,
              ahfl::ir::core::CoreWireSchemaNodeId wId,
              ahfl::ir::core::CoreLayoutId lId, std::uint32_t addr,
              const std::vector<StringRegion> &string_regions);

} // namespace ahfl::runtime::wasm_host
