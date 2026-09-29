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

#include <algorithm>
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

// --- String region authorization --------------------------------------------

// One authorized String-byte region [lo, hi). A module-written PtrLen is
// untrusted evidence; its bytes must lie entirely inside one of these.
// Shared by the P6-frame reader (input-payload arena + rodata + bridge result
// placements) and the capability-import bridge executor (input-payload arena
// + rodata + every OTHER call site's result placement).
struct StringRegion {
    std::uint32_t lo{0};
    std::uint32_t hi{0};
};

// Whether [ptr, ptr+len) lies entirely inside [lo, hi). Checked so a
// wrap-around or a past-end pointer fails closed (the JS oracle's
// `ptr >= lo && len <= hi - ptr` with signed subtraction).
[[nodiscard]] inline bool
region_contains(const StringRegion &region, std::uint32_t ptr,
                std::uint32_t len) noexcept {
    if (ptr < region.lo || ptr > region.hi) {
        return false;
    }
    const auto end = checked_add_u32(ptr, len);
    return end.has_value() && *end <= region.hi;
}

[[nodiscard]] inline bool
string_in_regions(const std::vector<StringRegion> &regions, std::uint32_t ptr,
                  std::uint32_t len) noexcept {
    return std::any_of(regions.begin(), regions.end(),
                       [ptr, len](const StringRegion &r) {
                           return region_contains(r, ptr, len);
                       });
}

} // namespace ahfl::runtime::wasm_host
