#pragma once

// RFC 0026 KR6.8 WH-2: shared walk context for the P6-frame packer and reader.
// Both walk (wire-schema node, P4-D layout id) pairs against the admitted
// layout section + wire table; this header holds the shared context, the
// per-layout backing-placement map, and the checked-arithmetic helpers the two
// walks reuse. The packer's write helpers and the reader's read helpers live
// in their own TUs (only one side touches the page in each direction), so this
// header carries no page-access code.

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ahfl::runtime::wasm_host {

namespace ir = ::ahfl::ir;

// The shared walk context. Borrows the admitted section + wire table (both
// must outlive the walk) and builds the per-layout backing-placement map once.
// Layout ids are dense (index into table.layouts), so the map is a flat vector
// indexed by id value (Principle 2: index-based identity, never a string key).
struct FrameWalkContext {
    const ir::core::CoreFrameLayoutSection &section;
    const ir::core::CoreWireSchemaTable &wire;
    // Indexed by CoreLayoutId.value; nullptr when the layout has no placement.
    std::vector<const ir::core::CoreFrameBackingPlacement *> backing_by_layout;

    FrameWalkContext(const ir::core::CoreFrameLayoutSection &s,
                     const ir::core::CoreWireSchemaTable &w)
        : section(s), wire(w) {
        backing_by_layout.assign(s.table.layouts.size(), nullptr);
        for (const auto &placement : s.placements) {
            if (placement.container_layout.value < backing_by_layout.size()) {
                backing_by_layout[placement.container_layout.value] = &placement;
            }
        }
    }

    // Bounds-checked lookups. Return nullptr when the id is out of range.
    [[nodiscard]] const ir::core::CoreWireSchemaNode *
    wire_node(ir::core::CoreWireSchemaNodeId id) const noexcept {
        if (id.value >= wire.nodes.size()) {
            return nullptr;
        }
        return &wire.nodes[id.value];
    }

    [[nodiscard]] const ir::core::CoreLayout *
    layout(ir::core::CoreLayoutId id) const noexcept {
        if (id.value >= section.table.layouts.size()) {
            return nullptr;
        }
        return &section.table.layouts[id.value];
    }

    [[nodiscard]] const ir::core::CoreFrameBackingPlacement *
    placement_for(ir::core::CoreLayoutId id) const noexcept {
        if (id.value >= backing_by_layout.size()) {
            return nullptr;
        }
        return backing_by_layout[id.value];
    }
};

// --- checked arithmetic -----------------------------------------------------

// a + b, or nullopt when the sum does not fit u32.
[[nodiscard]] inline std::optional<std::uint32_t>
checked_add_u32(std::uint64_t a, std::uint64_t b) noexcept {
    if (a > UINT32_MAX - b) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(a + b);
}

// a * b, or nullopt when the product does not fit u32.
[[nodiscard]] inline std::optional<std::uint32_t>
checked_mul_u32(std::uint64_t a, std::uint64_t b) noexcept {
    if (b != 0 && a > UINT32_MAX / b) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(a * b);
}

} // namespace ahfl::runtime::wasm_host
