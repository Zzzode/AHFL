#include "runtime/wasm_host/frame_reader.hpp"

#include "runtime/value/value_json.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/value/value.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

namespace ahfl::runtime::wasm_host {

namespace {

namespace ir = ::ahfl::ir;
using ir::core::CoreLayoutContainer;
using ir::core::CoreLayoutEnum;
using ir::core::CoreLayoutId;
using ir::core::CoreLayoutPtrLen;
using ir::core::CoreLayoutScalar;
using ir::core::CoreLayoutStruct;
using ir::core::CoreScalarRepr;
using ir::core::CoreWireSchemaEnum;
using ir::core::CoreWireSchemaField;
using ir::core::CoreWireSchemaNodeId;
using ir::core::CoreWireSchemaOption;
using ir::core::CoreWireSchemaSequence;
using ir::core::CoreWireSchemaString;
using ir::core::CoreWireSchemaStruct;
using ir::core::CoreWireSchemaTuple;

// --- bounds-checked page reads ----------------------------------------------

std::optional<std::uint32_t>
read_u32_le(std::span<const std::uint8_t> page, std::uint32_t offset) noexcept {
    if (offset > page.size() || 4u > page.size() - offset) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(page[offset + 0]) |
           (static_cast<std::uint32_t>(page[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(page[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(page[offset + 3]) << 24);
}

std::optional<std::uint64_t>
read_u64_le(std::span<const std::uint8_t> page, std::uint32_t offset) noexcept {
    if (offset > page.size() || 8u > page.size() - offset) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(page[offset + i]) << (i * 8);
    }
    return value;
}

std::optional<std::span<const std::uint8_t>>
read_bytes(std::span<const std::uint8_t> page, std::uint32_t offset,
           std::uint32_t len) noexcept {
    if (offset > page.size() || len > page.size() - offset) {
        return std::nullopt;
    }
    return page.subspan(offset, len);
}

// --- String region authorization --------------------------------------------

// One authorized String-byte region [lo, hi). A module-written PtrLen is
// untrusted evidence; its bytes must lie entirely inside one of these.
struct StringRegion {
    std::uint32_t lo{0};
    std::uint32_t hi{0};
};

// Whether [ptr, ptr+len) lies entirely inside [lo, hi). Checked so a
// wrap-around or a past-end pointer fails closed (the JS oracle's
// `ptr >= lo && len <= hi - ptr` with signed subtraction).
bool region_contains(const StringRegion &region, std::uint32_t ptr,
                     std::uint32_t len) noexcept {
    if (ptr < region.lo || ptr > region.hi) {
        return false;
    }
    const auto end = checked_add_u32(ptr, len);
    return end.has_value() && *end <= region.hi;
}

bool string_in_regions(const std::vector<StringRegion> &regions,
                       std::uint32_t ptr, std::uint32_t len) noexcept {
    return std::any_of(regions.begin(), regions.end(),
                       [ptr, len](const StringRegion &r) {
                           return region_contains(r, ptr, len);
                       });
}

// Build the authorized String regions for an output-frame walk: the input
// payload-arena span, the rodata span (when present), and every bridge
// result-placement payload arena with a positive capacity.
std::vector<StringRegion>
build_output_string_regions(const ir::core::CoreFrameLayoutSection &section) {
    std::vector<StringRegion> regions;
    const auto arena_end =
        checked_add_u32(section.payload_arena_base,
                        static_cast<std::uint64_t>(section.payload_arena_capacity));
    if (arena_end.has_value()) {
        regions.push_back({section.payload_arena_base, *arena_end});
    }
    if (section.rodata_base != 0) {
        const auto rodata_end =
            checked_add_u32(section.rodata_base,
                            static_cast<std::uint64_t>(section.rodata_extent));
        if (rodata_end.has_value()) {
            regions.push_back({section.rodata_base, *rodata_end});
        }
    }
    for (const auto &site : section.bridge_call_sites) {
        if (site.result_payload_capacity == 0) {
            continue;
        }
        const auto end =
            checked_add_u32(site.result_payload_base,
                            static_cast<std::uint64_t>(site.result_payload_capacity));
        if (end.has_value()) {
            regions.push_back({site.result_payload_base, *end});
        }
    }
    return regions;
}

// --- the recursive read ------------------------------------------------------

std::expected<Value, FrameReadError>
read_value(FrameWalkContext &ctx, std::span<const std::uint8_t> page,
           CoreWireSchemaNodeId wId, CoreLayoutId lId, std::uint32_t addr,
           const std::vector<StringRegion> &string_regions);

std::expected<Value, FrameReadError>
read_value(FrameWalkContext &ctx, std::span<const std::uint8_t> page,
           CoreWireSchemaNodeId wId, CoreLayoutId lId, std::uint32_t addr,
           const std::vector<StringRegion> &string_regions) {
    const auto *w = ctx.wire_node(wId);
    if (w == nullptr) {
        return std::unexpected(FrameReadError::NodeIdOutOfRange);
    }
    const auto *l = ctx.layout(lId);
    if (l == nullptr) {
        return std::unexpected(FrameReadError::LayoutIdOutOfRange);
    }

    // Unit -> null.
    if (std::holds_alternative<ir::core::CoreWireSchemaUnit>(w->shape)) {
        return make_unit();
    }

    // Bool: word in {0,1}.
    if (std::holds_alternative<ir::core::CoreWireSchemaBool>(w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr || scalar->repr != CoreScalarRepr::I32) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto word = read_u32_le(page, addr);
        if (!word.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        if (*word != 0u && *word != 1u) {
            return std::unexpected(FrameReadError::BoolWordInvalid);
        }
        return make_bool(*word == 1u);
    }

    // Int: i32/i64 by layout repr + schema bounds.
    if (const auto *w_int = std::get_if<ir::core::CoreWireSchemaInt>(&w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        std::int64_t word = 0;
        if (scalar->repr == CoreScalarRepr::I64) {
            const auto wide = read_u64_le(page, addr);
            if (!wide.has_value()) {
                return std::unexpected(FrameReadError::PageBoundsExceeded);
            }
            word = static_cast<std::int64_t>(*wide);
        } else if (scalar->repr == CoreScalarRepr::I32) {
            const auto narrow = read_u32_le(page, addr);
            if (!narrow.has_value()) {
                return std::unexpected(FrameReadError::PageBoundsExceeded);
            }
            word = static_cast<std::int32_t>(*narrow);
        } else {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        if (w_int->bounds.has_value()) {
            if (word < w_int->bounds->first || word > w_int->bounds->second) {
                return std::unexpected(FrameReadError::IntOutOfSchemaBounds);
            }
        }
        return make_int(word);
    }

    // String: (ptr,len) + region authorization.
    if (const auto *w_str = std::get_if<CoreWireSchemaString>(&w->shape)) {
        if (std::get_if<CoreLayoutPtrLen>(&l->shape) == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto ptr = read_u32_le(page, addr + 0);
        const auto len = read_u32_le(page, addr + 4);
        if (!ptr.has_value() || !len.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        if (w_str->length_bounds.has_value()) {
            const auto len64 = static_cast<std::int64_t>(*len);
            if (len64 < w_str->length_bounds->first ||
                len64 > w_str->length_bounds->second) {
                return std::unexpected(FrameReadError::StringLengthOutOfBounds);
            }
        }
        if (!string_in_regions(string_regions, *ptr, *len)) {
            return std::unexpected(FrameReadError::StringOutsideAuthorizedRegion);
        }
        const auto bytes = read_bytes(page, *ptr, *len);
        if (!bytes.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        return make_string(std::string(reinterpret_cast<const char *>(bytes->data()),
                                       bytes->size()));
    }

    // Struct: {_type:name, ...fields}.
    if (const auto *w_struct = std::get_if<CoreWireSchemaStruct>(&w->shape)) {
        const auto *l_struct = std::get_if<CoreLayoutStruct>(&l->shape);
        if (l_struct == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        if (w_struct->fields.size() != l_struct->field_offsets.size() ||
            w_struct->fields.size() != l_struct->field_layouts.size()) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        StructValue sv;
        sv.type_name = w_struct->wire_name;
        for (std::size_t i = 0; i < w_struct->fields.size(); ++i) {
            const auto field_addr =
                checked_add_u32(addr, l_struct->field_offsets[i]);
            if (!field_addr.has_value()) {
                return std::unexpected(FrameReadError::ArithmeticOverflow);
            }
            auto child = read_value(ctx, page, w_struct->fields[i].type,
                                    l_struct->field_layouts[i], *field_addr,
                                    string_regions);
            if (!child.has_value()) {
                return child;
            }
            sv.fields.set(w_struct->fields[i].wire_name,
                          std::make_unique<Value>(std::move(*child)));
        }
        return Value{std::move(sv)};
    }

    // Option: tag 0 -> None, tag 1 -> Some(inner).
    if (const auto *w_opt = std::get_if<CoreWireSchemaOption>(&w->shape)) {
        const auto *l_enum = std::get_if<CoreLayoutEnum>(&l->shape);
        if (l_enum == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto tag = read_u32_le(page, addr);
        if (!tag.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        if (*tag == 0u) {
            return make_option_none();
        }
        if (*tag != 1u) {
            return std::unexpected(FrameReadError::OptionTagInvalid);
        }
        if (l_enum->variant_payload_layouts.size() < 2) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto payload_addr =
            checked_add_u32(addr, static_cast<std::uint64_t>(l_enum->payload_offset));
        if (!payload_addr.has_value()) {
            return std::unexpected(FrameReadError::ArithmeticOverflow);
        }
        auto inner = read_value(ctx, page, w_opt->value,
                                l_enum->variant_payload_layouts[1], *payload_addr,
                                string_regions);
        if (!inner.has_value()) {
            return inner;
        }
        return make_option_some(std::move(*inner));
    }

    // Enum: ordinal + variant name + payload.
    if (const auto *w_enum = std::get_if<CoreWireSchemaEnum>(&w->shape)) {
        const auto *l_enum = std::get_if<CoreLayoutEnum>(&l->shape);
        if (l_enum == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto ordinal = read_u32_le(page, addr);
        if (!ordinal.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        if (*ordinal >= w_enum->variants.size()) {
            return std::unexpected(FrameReadError::EnumTagOutOfRange);
        }
        const auto &wire_variant = w_enum->variants[*ordinal];
        EnumValue ev;
        ev.enum_name = w_enum->wire_name;
        ev.variant = wire_variant.wire_name;
        if (wire_variant.payload_kind != ir::core::CoreWirePayloadKind::Unit) {
            if (*ordinal >= l_enum->variant_payload_layouts.size()) {
                return std::unexpected(FrameReadError::ShapeMismatch);
            }
            const auto payload_addr = checked_add_u32(
                addr, static_cast<std::uint64_t>(l_enum->payload_offset));
            if (!payload_addr.has_value()) {
                return std::unexpected(FrameReadError::ArithmeticOverflow);
            }
            const auto *payload_layout =
                ctx.layout(l_enum->variant_payload_layouts[*ordinal]);
            if (payload_layout == nullptr) {
                return std::unexpected(FrameReadError::LayoutIdOutOfRange);
            }
            const auto *payload_struct =
                std::get_if<CoreLayoutStruct>(&payload_layout->shape);
            if (payload_struct == nullptr) {
                return std::unexpected(FrameReadError::ShapeMismatch);
            }
            if (wire_variant.slots.size() != payload_struct->field_offsets.size() ||
                wire_variant.slots.size() != payload_struct->field_layouts.size()) {
                return std::unexpected(FrameReadError::ShapeMismatch);
            }
            if (wire_variant.payload_kind == ir::core::CoreWirePayloadKind::Tuple) {
                for (std::size_t i = 0; i < wire_variant.slots.size(); ++i) {
                    const auto slot_addr = checked_add_u32(
                        *payload_addr, payload_struct->field_offsets[i]);
                    if (!slot_addr.has_value()) {
                        return std::unexpected(FrameReadError::ArithmeticOverflow);
                    }
                    auto child = read_value(ctx, page, wire_variant.slots[i].type,
                                            payload_struct->field_layouts[i],
                                            *slot_addr, string_regions);
                    if (!child.has_value()) {
                        return child;
                    }
                    ev.payload.push_back(std::make_unique<Value>(std::move(*child)));
                }
            } else {
                for (std::size_t i = 0; i < wire_variant.slots.size(); ++i) {
                    const auto slot_addr = checked_add_u32(
                        *payload_addr, payload_struct->field_offsets[i]);
                    if (!slot_addr.has_value()) {
                        return std::unexpected(FrameReadError::ArithmeticOverflow);
                    }
                    auto child = read_value(ctx, page, wire_variant.slots[i].type,
                                            payload_struct->field_layouts[i],
                                            *slot_addr, string_regions);
                    if (!child.has_value()) {
                        return child;
                    }
                    ev.named_payload.set(
                        wire_variant.slots[i].wire_name,
                        std::make_unique<Value>(std::move(*child)));
                }
            }
        }
        return Value{std::move(ev)};
    }

    // Sequence: len in [0,capacity], elements at the placement (or the
    // module-written base when no placement exists), Set duplicate check.
    if (const auto *w_seq = std::get_if<CoreWireSchemaSequence>(&w->shape)) {
        const auto *l_container = std::get_if<CoreLayoutContainer>(&l->shape);
        if (l_container == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        const auto len = read_u32_le(page, addr + 4);
        if (!len.has_value()) {
            return std::unexpected(FrameReadError::PageBoundsExceeded);
        }
        const std::uint64_t capacity =
            w_seq->capacity.value_or(l_container->capacity);
        if (static_cast<std::uint64_t>(*len) > capacity) {
            return std::unexpected(FrameReadError::CollectionLengthOutOfRange);
        }
        // The element base: the declared placement overrides the
        // module-written header ptr (the packed input container's elements
        // live in its fixed placement).
        std::uint32_t base = 0;
        if (const auto *placement = ctx.placement_for(lId)) {
            base = placement->base;
            const auto scaled = checked_mul_u32(static_cast<std::uint64_t>(*len),
                                                l_container->stride);
            if (!scaled.has_value() ||
                static_cast<std::uint64_t>(*scaled) > placement->extent) {
                return std::unexpected(FrameReadError::CollectionOverrunsPlacement);
            }
        } else {
            const auto header_ptr = read_u32_le(page, addr + 0);
            if (!header_ptr.has_value()) {
                return std::unexpected(FrameReadError::PageBoundsExceeded);
            }
            base = *header_ptr;
        }
        std::vector<Value> items;
        items.reserve(*len);
        for (std::uint32_t i = 0; i < *len; ++i) {
            const auto elem_addr = checked_add_u32(
                base, static_cast<std::uint64_t>(i) * l_container->stride);
            if (!elem_addr.has_value()) {
                return std::unexpected(FrameReadError::ArithmeticOverflow);
            }
            auto child = read_value(ctx, page, w_seq->element,
                                    l_container->element, *elem_addr,
                                    string_regions);
            if (!child.has_value()) {
                return child;
            }
            items.push_back(std::move(*child));
        }
        if (w_seq->kind == ir::core::CoreWireSequenceKind::Set) {
            // Duplicate check via canonical JSON sort (the JS oracle's
            // exact discipline).
            std::vector<std::string> canonical;
            canonical.reserve(items.size());
            for (const auto &item : items) {
                canonical.push_back(value_to_json(item));
            }
            std::sort(canonical.begin(), canonical.end());
            for (std::size_t i = 1; i < canonical.size(); ++i) {
                if (canonical[i] == canonical[i - 1]) {
                    return std::unexpected(FrameReadError::SetDuplicateElement);
                }
            }
            return make_set(std::move(items));
        }
        return make_list(std::move(items));
    }

    // Tuple: positional elements into a struct layout.
    if (const auto *w_tuple = std::get_if<CoreWireSchemaTuple>(&w->shape)) {
        const auto *l_struct = std::get_if<CoreLayoutStruct>(&l->shape);
        if (l_struct == nullptr) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        if (w_tuple->elements.size() != l_struct->field_offsets.size() ||
            w_tuple->elements.size() != l_struct->field_layouts.size()) {
            return std::unexpected(FrameReadError::ShapeMismatch);
        }
        std::vector<Value> items;
        items.reserve(w_tuple->elements.size());
        for (std::size_t i = 0; i < w_tuple->elements.size(); ++i) {
            const auto field_addr =
                checked_add_u32(addr, l_struct->field_offsets[i]);
            if (!field_addr.has_value()) {
                return std::unexpected(FrameReadError::ArithmeticOverflow);
            }
            auto child = read_value(ctx, page, w_tuple->elements[i],
                                    l_struct->field_layouts[i], *field_addr,
                                    string_regions);
            if (!child.has_value()) {
                return child;
            }
            items.push_back(std::move(*child));
        }
        return make_list(std::move(items));
    }

    // Float / Decimal / Duration / Timestamp / Uuid / Map: outside the frame
    // subset — fail closed.
    return std::unexpected(FrameReadError::ShapeMismatch);
}

} // namespace

std::expected<std::string, FrameReadError>
encode_p6_output(std::span<const std::uint8_t> page,
                 const ir::core::CoreFrameLayoutSection &section,
                 const ir::core::VerifiedWireSchemaBinding &output_binding,
                 std::uint32_t value_ptr, P6FinalKind final_kind) {
    // Runv root authorization: a computed final's value_ptr must equal the
    // output base; an identity final's the input base. The discriminator comes
    // from the admitted descriptor/frame section, never inferred.
    const std::uint32_t authorized_base =
        final_kind == P6FinalKind::Computed
            ? ir::core::kP6AggregateOutputBase
            : ir::core::kP6AggregateInputBase;
    if (value_ptr != authorized_base) {
        return std::unexpected(FrameReadError::RunvRootUnauthorized);
    }

    FrameWalkContext ctx(section, output_binding.table());
    const auto string_regions = build_output_string_regions(section);
    auto value = read_value(ctx, page, output_binding.root(),
                            section.output_layout, value_ptr, string_regions);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    return value_to_json(*value);
}

} // namespace ahfl::runtime::wasm_host
