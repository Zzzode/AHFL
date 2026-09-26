#pragma once

// RFC 0026 P6-7: the canonical wasm32 P4-D aggregate GEOMETRY, as a single
// side-effect-free SSOT. The layout builder derives offsets/strides/extents
// with exactly these rules (`core_layout.cpp`), the producer LayoutVerifier
// re-derives them to reject a corrupted finalized table, and the host-side
// frame-layout admission (`core_frame_layout.cpp`) re-derives them a second
// time over a DECODED, UNTRUSTED transported table so a tampered section cannot
// ship struct field offsets, enum payload placement, or container backing
// extents that disagree with the child layouts they name.
//
// Keeping one implementation is load-bearing: a host that trusted the shipped
// `backing_size`/`extent` pair admitted a section whose placement spanned one
// slot while the header still advertised a four-element capacity. Every
// function here fails closed (returns std::nullopt) on non-power-of-two
// alignment or uint64 overflow and never follows a dangling edge (the caller
// must have range-checked child ids first).

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"

namespace ahfl::ir::core::layout_geometry {

[[nodiscard]] inline bool is_power_of_two(std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] inline std::optional<std::uint64_t> checked_add(std::uint64_t lhs,
                                                              std::uint64_t rhs) noexcept {
    if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs) {
        return std::nullopt;
    }
    return lhs + rhs;
}

[[nodiscard]] inline std::optional<std::uint64_t> checked_mul(std::uint64_t lhs,
                                                              std::uint64_t rhs) noexcept {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        return std::nullopt;
    }
    return lhs * rhs;
}

[[nodiscard]] inline std::optional<std::uint64_t> checked_align_up(std::uint64_t value,
                                                                   std::uint32_t align) noexcept {
    if (!is_power_of_two(align)) {
        return std::nullopt;
    }
    const std::uint64_t mask = static_cast<std::uint64_t>(align) - 1U;
    const auto sum = checked_add(value, mask);
    if (!sum.has_value()) {
        return std::nullopt;
    }
    return *sum & ~mask;
}

/// The canonical field offsets, padded size, and alignment of a struct laid out
/// over its inline child layouts in declaration order. Caller guarantees every
/// `field_layouts[i]` is in range of `table`.
struct StructGeometry {
    std::vector<std::uint64_t> field_offsets;
    std::uint64_t size{0};
    std::uint32_t align{1};
};

[[nodiscard]] inline std::optional<StructGeometry> expected_struct(const CoreLayoutTable &table,
                                                                   const CoreLayoutStruct &shape) {
    if (shape.field_offsets.size() != shape.field_layouts.size()) {
        return std::nullopt;
    }
    StructGeometry geometry;
    geometry.field_offsets.reserve(shape.field_layouts.size());
    std::uint64_t cursor = 0;
    std::uint32_t align = 1;
    for (const CoreLayoutId child_id : shape.field_layouts) {
        if (child_id.value >= table.layouts.size()) {
            return std::nullopt;
        }
        const CoreLayout &child = table.layouts[child_id.value];
        const auto offset = checked_align_up(cursor, child.align);
        const auto next = offset.has_value() ? checked_add(*offset, child.size) : std::nullopt;
        if (!offset.has_value() || !next.has_value()) {
            return std::nullopt;
        }
        geometry.field_offsets.push_back(*offset);
        cursor = *next;
        align = std::max(align, child.align);
    }
    const auto padded = checked_align_up(cursor, align);
    if (!padded.has_value()) {
        return std::nullopt;
    }
    geometry.size = *padded;
    geometry.align = align;
    return geometry;
}

/// The canonical tag-to-payload gap, padded size, and alignment of an enum:
/// a 4-byte tag followed by the union of the per-variant payload aggregates,
/// with the whole record at least 4-aligned. Caller guarantees every
/// `variant_payload_layouts[i]` is in range of `table`.
struct EnumGeometry {
    std::uint64_t payload_offset{0};
    std::uint64_t size{0};
    std::uint32_t align{4};
};

[[nodiscard]] inline std::optional<EnumGeometry> expected_enum(const CoreLayoutTable &table,
                                                               const CoreLayoutEnum &shape) {
    if (shape.tag_size != 4 ||
        shape.variant_payload_layouts.size() != shape.variant_payload_sizes.size()) {
        return std::nullopt;
    }
    std::uint64_t payload_size = 0;
    std::uint32_t payload_align = 1;
    for (std::size_t i = 0; i < shape.variant_payload_layouts.size(); ++i) {
        const CoreLayoutId payload_id = shape.variant_payload_layouts[i];
        if (payload_id.value >= table.layouts.size()) {
            return std::nullopt;
        }
        const CoreLayout &payload = table.layouts[payload_id.value];
        if (shape.variant_payload_sizes[i] != payload.size) {
            return std::nullopt;
        }
        payload_size = std::max(payload_size, payload.size);
        payload_align = std::max(payload_align, payload.align);
    }
    const auto payload_offset = checked_align_up(4, payload_align);
    const auto raw =
        payload_offset.has_value() ? checked_add(*payload_offset, payload_size) : std::nullopt;
    const std::uint32_t align = std::max<std::uint32_t>(4, payload_align);
    const auto size = raw.has_value() ? checked_align_up(*raw, align) : std::nullopt;
    if (!payload_offset.has_value() || !raw.has_value() || !size.has_value()) {
        return std::nullopt;
    }
    return EnumGeometry{*payload_offset, *size, align};
}

/// The canonical backing geometry of a bounded container: the map-value slot
/// offset (0 for a sequence), the per-entry stride, and the total
/// `stride * capacity` backing extent. Caller guarantees the element (and map
/// value, when present) edges are in range of `table`.
struct ContainerGeometry {
    std::uint64_t value_offset{0};
    std::uint64_t stride{0};
    std::uint64_t backing_size{0};
};

[[nodiscard]] inline std::optional<ContainerGeometry>
expected_container(const CoreLayoutTable &table, const CoreLayoutContainer &shape) {
    if (shape.element.value >= table.layouts.size()) {
        return std::nullopt;
    }
    const CoreLayout &element = table.layouts[shape.element.value];
    std::uint64_t raw_size = element.size;
    std::uint32_t align = element.align;
    std::uint64_t value_offset = 0;
    if (shape.value.has_value()) {
        if (shape.value->value >= table.layouts.size()) {
            return std::nullopt;
        }
        const CoreLayout &value = table.layouts[shape.value->value];
        const auto offset = checked_align_up(element.size, value.align);
        const auto raw = offset.has_value() ? checked_add(*offset, value.size) : std::nullopt;
        if (!offset.has_value() || !raw.has_value()) {
            return std::nullopt;
        }
        value_offset = *offset;
        raw_size = *raw;
        align = std::max(align, value.align);
    }
    const auto stride = checked_align_up(raw_size, align);
    const auto backing = stride.has_value() ? checked_mul(*stride, shape.capacity) : std::nullopt;
    if (!stride.has_value() || !backing.has_value()) {
        return std::nullopt;
    }
    return ContainerGeometry{value_offset, *stride, *backing};
}

} // namespace ahfl::ir::core::layout_geometry
