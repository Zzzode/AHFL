#pragma once

// RFC 0026 KR6.8 WH-2: the P6-frame packer — the native equivalent of the JS
// oracle's packValue/packSlots/packP6Input. Packs a host `runtime::Value`
// into the fixed P4-D regions of a 64 KiB page, using the admitted layout
// section + input wire binding as the physical and logical authorities.
//
// Discipline (mirrors tests/conformance/node_embedded_host.mjs):
//   * zero the reserved named regions [1024,16384) first, so padding words
//     are 0 (the packer never touches [0,1024): the zero page + the read-only
//     rodata region the module's Data section initialized);
//   * scalars at slot offsets; struct fields at field_offsets; enum tag@0 +
//     payload@payload_offset; option None=0 / Some=1; String as (ptr,len)
//     with bytes bump-allocated in the declared payload arena; collections
//     with inline (ptr,len) headers and elements at their declared placements;
//   * FAIL CLOSED on a closure (or any value with no CoreLayout wire shape)
//     and on every bounds/arity/arithmetic violation, with a typed
//     FramePackError (no echo of page bytes, names, or values).

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "runtime/value/value.hpp"
#include "runtime/wasm_host/frame_walk.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace ahfl::runtime::wasm_host {

/// Typed fail-closed error family for the P6-frame packer. Every error is a
/// fixed enum code; no diagnostic echoes page bytes, decoded names, or values.
enum class FramePackError {
    NodeIdOutOfRange,
    LayoutIdOutOfRange,
    ShapeMismatch,
    ValueNotWireEncodable,
    IntOutOfSchemaBounds,
    IntDoesNotFitI32,
    StringLengthOutOfBounds,
    ArenaExhausted,
    CollectionExceedsCapacity,
    CollectionOverrunsPlacement,
    CollectionHasNoPlacement,
    StructMissingField,
    EnumUnknownVariant,
    PageBoundsExceeded,
    ArithmeticOverflow,
};

/// The stable enum-code name of a FramePackError, for surfacing the exact
/// rejection reason in session errors and test pins (WH-5c.7).
[[nodiscard]] std::string_view frame_pack_error_name(FramePackError e) noexcept;

/// Pack a host Value into the fixed P6-frame input regions of a 64 KiB page.
/// `page` must be the whole fixed single page (65536 bytes). The admitted
/// `section` is the physical-layout authority; `input_binding` names the
/// wire-schema root and table. Fails closed with a typed error on any
/// violation; on failure the page is left in an unspecified (partially
/// written) state and must not be driven.
[[nodiscard]] std::expected<void, FramePackError>
pack_p6_input(std::span<std::uint8_t> page,
              const ahfl::ir::core::CoreFrameLayoutSection &section,
              const ahfl::ir::core::VerifiedWireSchemaBinding &input_binding,
              const Value &input);

/// The generic P4-D value packer: write a host `runtime::Value` into the
/// fixed-layout binary encoding at `addr` in `page`, walked against the wire
/// node `wId` and layout root `lId`. String payload bytes are bump-allocated
/// in the caller-specified arena `[arena_base, +arena_capacity)` via the
/// in/out `arena_cursor` (initialized to `arena_base` by the caller).
///
/// Exposed (WH-3) so the capability-import bridge executor can pack a bridge
/// result at the call site's disjoint result placement (its own
/// result_payload arena, NOT the P6 input-payload arena). Every write is
/// bounds- and schema-checked; a violation fails closed with a typed
/// FramePackError (no echo of page bytes, names, or values).
[[nodiscard]] std::expected<void, FramePackError>
pack_value_at(FrameWalkContext &ctx, std::span<std::uint8_t> page,
              ahfl::ir::core::CoreWireSchemaNodeId wId,
              ahfl::ir::core::CoreLayoutId lId, const Value &value,
              std::uint32_t addr, std::uint32_t &arena_cursor,
              std::uint32_t arena_base, std::uint32_t arena_capacity);

} // namespace ahfl::runtime::wasm_host
