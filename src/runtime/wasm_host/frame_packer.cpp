#include "runtime/wasm_host/frame_packer.hpp"

#include "runtime/engine/core_wasm_resume_capacity.hpp"
#include "runtime/wasm_host/frame_spelling.hpp"
#include "runtime/wasm_host/frame_walk.hpp"
#include "runtime/value/scalar_spelling.hpp"
#include "runtime/value/value.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

namespace ahfl::runtime::wasm_host {

namespace ir = ::ahfl::ir;
using ir::core::CoreLayoutContainer;
using ir::core::CoreLayoutEnum;
using ir::core::CoreLayoutId;
using ir::core::CoreLayoutPtrLen;
using ir::core::CoreLayoutScalar;
using ir::core::CoreLayoutStruct;
using ir::core::CoreScalarRepr;
using ir::core::CoreWireSchemaDecimal;
using ir::core::CoreWireSchemaDuration;
using ir::core::CoreWireSchemaEnum;
using ir::core::CoreWireSchemaField;
using ir::core::CoreWireSchemaMap;
using ir::core::CoreWireSchemaNodeId;
using ir::core::CoreWireSchemaOption;
using ir::core::CoreWireSchemaSequence;
using ir::core::CoreWireSchemaString;
using ir::core::CoreWireSchemaStruct;
using ir::core::CoreWireSchemaTuple;

// Forward declaration: the generic P4-D packer (defined after the anonymous-
// namespace helpers). pack_slots (below, inside the anonymous namespace) calls
// it, so it must be declared first.
std::expected<void, FramePackError>
pack_value_at(FrameWalkContext &ctx, std::span<std::uint8_t> page,
              CoreWireSchemaNodeId wId, CoreLayoutId lId, const Value &value,
              std::uint32_t addr, std::uint32_t &arena_cursor,
              std::uint32_t arena_base, std::uint32_t arena_capacity);

namespace {

// --- bounds-checked page writes ---------------------------------------------

bool write_u32_le(std::span<std::uint8_t> page, std::uint32_t offset,
                  std::uint32_t value) noexcept {
    if (offset > page.size() || 4u > page.size() - offset) {
        return false;
    }
    page[offset + 0] = static_cast<std::uint8_t>(value & 0xffu);
    page[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    page[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    page[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
    return true;
}

bool write_u64_le(std::span<std::uint8_t> page, std::uint32_t offset,
                  std::uint64_t value) noexcept {
    if (offset > page.size() || 8u > page.size() - offset) {
        return false;
    }
    for (std::size_t i = 0; i < 8; ++i) {
        page[offset + i] = static_cast<std::uint8_t>((value >> (i * 8)) & 0xffu);
    }
    return true;
}

bool write_bytes(std::span<std::uint8_t> page, std::uint32_t offset,
                 std::span<const std::uint8_t> bytes) noexcept {
    if (offset > page.size() || bytes.size() > page.size() - offset) {
        return false;
    }
    if (!bytes.empty()) {
        std::memcpy(page.data() + offset, bytes.data(), bytes.size());
    }
    return true;
}

// Pack a sequence of wire slots into a struct layout at `addr`. `get(i)`
// returns the i-th slot's value (by name for struct/named-payload, by position
// for tuple/positional-payload), or nullptr when the slot is absent.
template <class Get>
std::expected<void, FramePackError>
pack_slots(FrameWalkContext &ctx, std::span<std::uint8_t> page,
           const std::vector<CoreWireSchemaField> &slots,
           const CoreLayoutStruct &struct_layout, Get &&get,
           std::uint32_t addr, std::uint32_t &arena_cursor,
           std::uint32_t arena_base, std::uint32_t arena_capacity) {
    if (slots.size() != struct_layout.field_offsets.size() ||
        slots.size() != struct_layout.field_layouts.size()) {
        return std::unexpected(FramePackError::ShapeMismatch);
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const Value *child = get(i);
        if (child == nullptr) {
            return std::unexpected(FramePackError::StructMissingField);
        }
        const auto field_addr = checked_add_u32(addr, struct_layout.field_offsets[i]);
        if (!field_addr.has_value()) {
            return std::unexpected(FramePackError::ArithmeticOverflow);
        }
        auto result = pack_value_at(ctx, page, slots[i].type,
                                 struct_layout.field_layouts[i], *child, *field_addr,
                                 arena_cursor, arena_base, arena_capacity);
        if (!result.has_value()) {
            return result;
        }
    }
    return {};
}

} // namespace

std::string_view frame_pack_error_name(FramePackError e) noexcept {
    switch (e) {
    case FramePackError::NodeIdOutOfRange:
        return "NodeIdOutOfRange";
    case FramePackError::LayoutIdOutOfRange:
        return "LayoutIdOutOfRange";
    case FramePackError::ShapeMismatch:
        return "ShapeMismatch";
    case FramePackError::ValueNotWireEncodable:
        return "ValueNotWireEncodable";
    case FramePackError::IntOutOfSchemaBounds:
        return "IntOutOfSchemaBounds";
    case FramePackError::IntDoesNotFitI32:
        return "IntDoesNotFitI32";
    case FramePackError::StringLengthOutOfBounds:
        return "StringLengthOutOfBounds";
    case FramePackError::ArenaExhausted:
        return "ArenaExhausted";
    case FramePackError::CollectionExceedsCapacity:
        return "CollectionExceedsCapacity";
    case FramePackError::CollectionOverrunsPlacement:
        return "CollectionOverrunsPlacement";
    case FramePackError::CollectionHasNoPlacement:
        return "CollectionHasNoPlacement";
    case FramePackError::StructMissingField:
        return "StructMissingField";
    case FramePackError::EnumUnknownVariant:
        return "EnumUnknownVariant";
    case FramePackError::PageBoundsExceeded:
        return "PageBoundsExceeded";
    case FramePackError::ArithmeticOverflow:
        return "ArithmeticOverflow";
    }
    return "Unknown";
}

// --- the recursive pack (public, shared with the bridge executor) -----------

std::expected<void, FramePackError>
pack_value_at(FrameWalkContext &ctx, std::span<std::uint8_t> page,
              CoreWireSchemaNodeId wId, CoreLayoutId lId, const Value &value,
              std::uint32_t addr, std::uint32_t &arena_cursor,
              std::uint32_t arena_base, std::uint32_t arena_capacity) {
    const auto *w = ctx.wire_node(wId);
    if (w == nullptr) {
        return std::unexpected(FramePackError::NodeIdOutOfRange);
    }
    const auto *l = ctx.layout(lId);
    if (l == nullptr) {
        return std::unexpected(FramePackError::LayoutIdOutOfRange);
    }

    // Fail closed on values with no CoreLayout wire representation.
    // Closures (and any future non-wire value kind) must never be packed.
    if (std::holds_alternative<InterpreterClosureHandle>(value.node)) {
        return std::unexpected(FramePackError::ValueNotWireEncodable);
    }

    // Unit: no write (the JS oracle returns immediately).
    if (std::holds_alternative<ir::core::CoreWireSchemaUnit>(w->shape)) {
        if (!std::holds_alternative<UnitValue>(value.node) &&
            !std::holds_alternative<NoneValue>(value.node)) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        return {};
    }

    // Bool: i32 {0,1}.
    if (std::holds_alternative<ir::core::CoreWireSchemaBool>(w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr || scalar->repr != CoreScalarRepr::I32) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *b = std::get_if<BoolValue>(&value.node);
        if (b == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        if (!write_u32_le(page, addr, b->value ? 1u : 0u)) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Int: schema bounds + i32/i64 by layout repr.
    if (const auto *w_int = std::get_if<ir::core::CoreWireSchemaInt>(&w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *iv = std::get_if<IntValue>(&value.node);
        if (iv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        if (w_int->bounds.has_value()) {
            if (iv->value < w_int->bounds->first ||
                iv->value > w_int->bounds->second) {
                return std::unexpected(FramePackError::IntOutOfSchemaBounds);
            }
        }
        if (scalar->repr == CoreScalarRepr::I64) {
            if (!write_u64_le(page, addr, static_cast<std::uint64_t>(iv->value))) {
                return std::unexpected(FramePackError::PageBoundsExceeded);
            }
        } else if (scalar->repr == CoreScalarRepr::I32) {
            if (iv->value < INT32_MIN || iv->value > INT32_MAX) {
                return std::unexpected(FramePackError::IntDoesNotFitI32);
            }
            if (!write_u32_le(page, addr, static_cast<std::uint32_t>(iv->value))) {
                return std::unexpected(FramePackError::PageBoundsExceeded);
            }
        } else {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        return {};
    }

    // Float: f64 by layout repr.
    if (std::holds_alternative<ir::core::CoreWireSchemaFloat>(w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr || scalar->repr != CoreScalarRepr::F64) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *fv = std::get_if<FloatValue>(&value.node);
        if (fv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, &fv->value, sizeof(bits));
        if (!write_u64_le(page, addr, bits)) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // String: (ptr,len) with bytes bump-allocated in the payload arena.
    if (const auto *w_str = std::get_if<CoreWireSchemaString>(&w->shape)) {
        if (std::get_if<CoreLayoutPtrLen>(&l->shape) == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *sv = std::get_if<StringValue>(&value.node);
        if (sv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto len = static_cast<std::uint64_t>(sv->value.size());
        if (w_str->length_bounds.has_value()) {
            if (len < static_cast<std::uint64_t>(w_str->length_bounds->first) ||
                len > static_cast<std::uint64_t>(w_str->length_bounds->second)) {
                return std::unexpected(FramePackError::StringLengthOutOfBounds);
            }
        }
        // Arena bump: cursor + len <= base + capacity (checked, no wrap).
        const auto arena_end =
            checked_add_u32(arena_base,
                            static_cast<std::uint64_t>(arena_capacity));
        if (!arena_end.has_value()) {
            return std::unexpected(FramePackError::ArithmeticOverflow);
        }
        const auto next_cursor = checked_add_u32(arena_cursor, len);
        if (!next_cursor.has_value() || *next_cursor > *arena_end) {
            return std::unexpected(FramePackError::ArenaExhausted);
        }
        const auto *bytes_ptr =
            reinterpret_cast<const std::uint8_t *>(sv->value.data());
        if (!write_bytes(page, arena_cursor,
                         std::span(bytes_ptr, sv->value.size()))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        if (!write_u32_le(page, addr + 0, arena_cursor) ||
            !write_u32_le(page, addr + 4, static_cast<std::uint32_t>(len))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        arena_cursor = *next_cursor;
        return {};
    }

    // Struct: iterate wire fields, pack at field_offsets.
    if (const auto *w_struct = std::get_if<CoreWireSchemaStruct>(&w->shape)) {
        const auto *l_struct = std::get_if<CoreLayoutStruct>(&l->shape);
        if (l_struct == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *sv = std::get_if<StructValue>(&value.node);
        if (sv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        return pack_slots(ctx, page, w_struct->fields, *l_struct,
                          [&](std::size_t i) -> const Value * {
                              return sv->fields.get(w_struct->fields[i].wire_name);
                          },
                          addr, arena_cursor, arena_base, arena_capacity);
    }

    // Option: resolve Some/None variant indices by payload properties (AHFL's
    // Option<T> defines Some(T) as variant 0 and None as variant 1, but the
    // wire schema's Option shape is order-agnostic).
    if (const auto *w_opt = std::get_if<CoreWireSchemaOption>(&w->shape)) {
        const auto *l_enum = std::get_if<CoreLayoutEnum>(&l->shape);
        if (l_enum == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto variants = option_variant_indices(*l_enum);
        if (!variants.has_value()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto [some_idx, none_idx] = *variants;
        // None: a bare null or an Option::None.
        if (std::holds_alternative<NoneValue>(value.node) ||
            is_optional_none(value)) {
            if (!write_u32_le(page, addr, none_idx)) {
                return std::unexpected(FramePackError::PageBoundsExceeded);
            }
            return {};
        }
        const auto payload_addr =
            checked_add_u32(addr, static_cast<std::uint64_t>(l_enum->payload_offset));
        if (!payload_addr.has_value()) {
            return std::unexpected(FramePackError::ArithmeticOverflow);
        }
        // Some: an Option::Some unwraps to its inner; any other non-null value
        // is packed as the Some payload itself (the JS oracle's "any non-null
        // is Some" behavior, so JSON-decoded raw values pack faithfully).
        const Value *inner = is_some(value) ? optional_inner(value) : &value;
        if (inner == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // Pack the payload BEFORE writing the tag, so a non-encodable inner
        // (e.g. a closure) fails closed without leaving a partial Some tag.
        // The Some variant's payload is a one-slot struct (the enum payload
        // wrapper); the inner value lands at the struct's sole field.
        const auto *payload_layout =
            ctx.layout(l_enum->variant_payload_layouts[some_idx]);
        const auto *payload_struct =
            payload_layout != nullptr
                ? std::get_if<CoreLayoutStruct>(&payload_layout->shape)
                : nullptr;
        if (payload_struct == nullptr ||
            payload_struct->field_layouts.size() != 1) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto inner_addr = checked_add_u32(
            *payload_addr,
            static_cast<std::uint64_t>(payload_struct->field_offsets[0]));
        if (!inner_addr.has_value()) {
            return std::unexpected(FramePackError::ArithmeticOverflow);
        }
        auto packed = pack_value_at(ctx, page, w_opt->value,
                                 payload_struct->field_layouts[0], *inner,
                                 *inner_addr, arena_cursor, arena_base, arena_capacity);
        if (!packed.has_value()) {
            return packed;
        }
        if (!write_u32_le(page, addr, some_idx)) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Enum: ordinal by variant name, tag@0, payload@payload_offset.
    if (const auto *w_enum = std::get_if<CoreWireSchemaEnum>(&w->shape)) {
        const auto *l_enum = std::get_if<CoreLayoutEnum>(&l->shape);
        if (l_enum == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *ev = std::get_if<EnumValue>(&value.node);
        if (ev == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // An Option value must arrive through the wire Option arm, not here.
        if (is_optional(value)) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        std::size_t ordinal = 0;
        bool found = false;
        for (std::size_t i = 0; i < w_enum->variants.size(); ++i) {
            if (w_enum->variants[i].wire_name == ev->variant) {
                ordinal = i;
                found = true;
                break;
            }
        }
        if (!found) {
            return std::unexpected(FramePackError::EnumUnknownVariant);
        }
        const auto &wire_variant = w_enum->variants[ordinal];
        if (wire_variant.payload_kind == ir::core::CoreWirePayloadKind::Unit) {
            // Unit variant: tag only, no payload to leak.
            if (!write_u32_le(page, addr, static_cast<std::uint32_t>(ordinal))) {
                return std::unexpected(FramePackError::PageBoundsExceeded);
            }
            return {};
        }
        if (ordinal >= l_enum->variant_payload_layouts.size()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto payload_addr =
            checked_add_u32(addr, static_cast<std::uint64_t>(l_enum->payload_offset));
        if (!payload_addr.has_value()) {
            return std::unexpected(FramePackError::ArithmeticOverflow);
        }
        const auto *payload_layout =
            ctx.layout(l_enum->variant_payload_layouts[ordinal]);
        if (payload_layout == nullptr) {
            return std::unexpected(FramePackError::LayoutIdOutOfRange);
        }
        const auto *payload_struct =
            std::get_if<CoreLayoutStruct>(&payload_layout->shape);
        if (payload_struct == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // Pack the payload BEFORE writing the tag, so a non-encodable payload
        // (e.g. a closure in a variant field) fails closed without leaving a
        // partial tag byte.
        std::expected<void, FramePackError> packed =
            std::unexpected(FramePackError::ShapeMismatch);
        if (wire_variant.payload_kind == ir::core::CoreWirePayloadKind::Tuple) {
            packed = pack_slots(ctx, page, wire_variant.slots, *payload_struct,
                                [&](std::size_t i) -> const Value * {
                                    if (i >= ev->payload.size()) {
                                        return nullptr;
                                    }
                                    return ev->payload[i].get();
                                },
                                *payload_addr, arena_cursor, arena_base, arena_capacity);
        } else {
            // Struct variant: named payload.
            packed = pack_slots(ctx, page, wire_variant.slots, *payload_struct,
                                [&](std::size_t i) -> const Value * {
                                    return ev->named_payload.get(
                                        wire_variant.slots[i].wire_name);
                                },
                                *payload_addr, arena_cursor, arena_base, arena_capacity);
        }
        if (!packed.has_value()) {
            return packed;
        }
        if (!write_u32_le(page, addr, static_cast<std::uint32_t>(ordinal))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Sequence: inline (ptr,len) header, elements at the declared placement.
    if (const auto *w_seq = std::get_if<CoreWireSchemaSequence>(&w->shape)) {
        const auto *l_container = std::get_if<CoreLayoutContainer>(&l->shape);
        if (l_container == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // Accept a List or a Set (the JSON decode yields a List for both).
        const std::vector<std::unique_ptr<Value>> *items = nullptr;
        if (const auto *lv = std::get_if<ListValue>(&value.node)) {
            items = &lv->items;
        } else if (const auto *sv = std::get_if<SetValue>(&value.node)) {
            items = &sv->items;
        } else {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto len = items->size();
        const std::uint64_t capacity =
            w_seq->capacity.value_or(l_container->capacity);
        if (static_cast<std::uint64_t>(len) > capacity) {
            return std::unexpected(FramePackError::CollectionExceedsCapacity);
        }
        const auto *placement = ctx.placement_for(lId);
        if (placement == nullptr) {
            return std::unexpected(FramePackError::CollectionHasNoPlacement);
        }
        const auto scaled =
            checked_mul_u32(static_cast<std::uint64_t>(len), l_container->stride);
        if (!scaled.has_value() ||
            static_cast<std::uint64_t>(*scaled) > placement->extent) {
            return std::unexpected(FramePackError::CollectionOverrunsPlacement);
        }
        for (std::size_t i = 0; i < len; ++i) {
            const auto elem_addr = checked_add_u32(
                placement->base,
                static_cast<std::uint64_t>(i) * l_container->stride);
            if (!elem_addr.has_value()) {
                return std::unexpected(FramePackError::ArithmeticOverflow);
            }
            auto result = pack_value_at(ctx, page, w_seq->element,
                                     l_container->element, *(*items)[i], *elem_addr,
                                     arena_cursor, arena_base, arena_capacity);
            if (!result.has_value()) {
                return result;
            }
        }
        if (!write_u32_le(page, addr + 0, placement->base) ||
            !write_u32_le(page, addr + 4, static_cast<std::uint32_t>(len))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Tuple: positional elements into a struct layout.
    if (const auto *w_tuple = std::get_if<CoreWireSchemaTuple>(&w->shape)) {
        const auto *l_struct = std::get_if<CoreLayoutStruct>(&l->shape);
        if (l_struct == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *lv = std::get_if<ListValue>(&value.node);
        if (lv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        if (w_tuple->elements.size() != l_struct->field_offsets.size() ||
            w_tuple->elements.size() != l_struct->field_layouts.size() ||
            w_tuple->elements.size() != lv->items.size()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        for (std::size_t i = 0; i < w_tuple->elements.size(); ++i) {
            const auto field_addr =
                checked_add_u32(addr, l_struct->field_offsets[i]);
            if (!field_addr.has_value()) {
                return std::unexpected(FramePackError::ArithmeticOverflow);
            }
            auto result = pack_value_at(ctx, page, w_tuple->elements[i],
                                     l_struct->field_layouts[i], *lv->items[i],
                                     *field_addr, arena_cursor, arena_base, arena_capacity);
            if (!result.has_value()) {
                return result;
            }
        }
        return {};
    }

    // Decimal: rides an i64 word (mantissa). The spelling is parsed through
    // the runtime's single spelling authority; the parsed scale MUST match the
    // wire-schema scale so the reader can rebuild a spelling identical to the
    // evaluator's preserved source literal (WH-5c.7 mechanism (f)).
    if (const auto *w_dec = std::get_if<CoreWireSchemaDecimal>(&w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr || scalar->repr != CoreScalarRepr::I64) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *dv = std::get_if<DecimalValue>(&value.node);
        if (dv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto decoded = scalar_spelling::parse_decimal(dv->spelling);
        if (!decoded.has_value()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        if (static_cast<std::int64_t>(decoded->parts.scale) != w_dec->scale) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // WH-5c.7 spelling round-trip guard: the spelling the reader will
        // rebuild from the bare i64 mantissa must equal the source spelling
        // exactly, so non-canonical forms ("01.20" for Decimal(2)) are
        // rejected at entry pack before the module runs.
        if (format_decimal_spelling(decoded->parts.mantissa,
                                    decoded->parts.scale) != dv->spelling) {
            return std::unexpected(FramePackError::ValueNotWireEncodable);
        }
        if (!write_u64_le(page, addr,
                          static_cast<std::uint64_t>(decoded->parts.mantissa))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Duration: rides an i64 word (milliseconds). The spelling is parsed
    // through the runtime's single spelling authority (WH-5c.7 mechanism (f)).
    if (std::holds_alternative<CoreWireSchemaDuration>(w->shape)) {
        const auto *scalar = std::get_if<CoreLayoutScalar>(&l->shape);
        if (scalar == nullptr || scalar->repr != CoreScalarRepr::I64) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *dv = std::get_if<DurationValue>(&value.node);
        if (dv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto decoded = scalar_spelling::parse_duration(dv->spelling);
        if (!decoded.has_value()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        // WH-5c.7 spelling round-trip guard: the spelling the reader will
        // rebuild from the bare i64 millis must equal the source spelling
        // exactly, so non-canonical unit forms ("60s", "5000ms") are rejected
        // at entry pack before the module runs.
        if (format_duration_spelling(decoded->millis) != dv->spelling) {
            return std::unexpected(FramePackError::ValueNotWireEncodable);
        }
        if (!write_u64_le(page, addr,
                          static_cast<std::uint64_t>(decoded->millis))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Map: inline (ptr,len) header, key/value pairs at the declared placement.
    // Each entry occupies one stride: key at offset 0, value at value_offset
    // (WH-5c.7 mechanism (f)).
    if (const auto *w_map = std::get_if<CoreWireSchemaMap>(&w->shape)) {
        const auto *l_container = std::get_if<CoreLayoutContainer>(&l->shape);
        if (l_container == nullptr || !l_container->value.has_value()) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto *mv = std::get_if<MapValue>(&value.node);
        if (mv == nullptr) {
            return std::unexpected(FramePackError::ShapeMismatch);
        }
        const auto len = mv->entries.size();
        const std::uint64_t capacity =
            w_map->capacity.value_or(l_container->capacity);
        if (static_cast<std::uint64_t>(len) > capacity) {
            return std::unexpected(FramePackError::CollectionExceedsCapacity);
        }
        const auto *placement = ctx.placement_for(lId);
        if (placement == nullptr) {
            return std::unexpected(FramePackError::CollectionHasNoPlacement);
        }
        const auto scaled =
            checked_mul_u32(static_cast<std::uint64_t>(len), l_container->stride);
        if (!scaled.has_value() ||
            static_cast<std::uint64_t>(*scaled) > placement->extent) {
            return std::unexpected(FramePackError::CollectionOverrunsPlacement);
        }
        for (std::size_t i = 0; i < len; ++i) {
            const auto entry_base = checked_add_u32(
                placement->base,
                static_cast<std::uint64_t>(i) * l_container->stride);
            if (!entry_base.has_value()) {
                return std::unexpected(FramePackError::ArithmeticOverflow);
            }
            auto key_result = pack_value_at(ctx, page, w_map->key,
                                         l_container->element, *mv->entries[i].first,
                                         *entry_base, arena_cursor, arena_base,
                                         arena_capacity);
            if (!key_result.has_value()) {
                return key_result;
            }
            const auto value_addr = checked_add_u32(
                *entry_base, l_container->value_offset);
            if (!value_addr.has_value()) {
                return std::unexpected(FramePackError::ArithmeticOverflow);
            }
            auto val_result = pack_value_at(ctx, page, w_map->value,
                                         *l_container->value, *mv->entries[i].second,
                                         *value_addr, arena_cursor, arena_base,
                                         arena_capacity);
            if (!val_result.has_value()) {
                return val_result;
            }
        }
        if (!write_u32_le(page, addr + 0, placement->base) ||
            !write_u32_le(page, addr + 4, static_cast<std::uint32_t>(len))) {
            return std::unexpected(FramePackError::PageBoundsExceeded);
        }
        return {};
    }

    // Timestamp / Uuid / Closure: outside the frame subset (or not a wire
    // value) — fail closed.
    return std::unexpected(FramePackError::ValueNotWireEncodable);
}

std::expected<void, FramePackError>
pack_p6_input(std::span<std::uint8_t> page,
              const ir::core::CoreFrameLayoutSection &section,
              const ir::core::VerifiedWireSchemaBinding &input_binding,
              const Value &input) {
    // The packer writes into the live fixed single-page guest memory: the
    // caller MUST pass the whole 64 KiB page, not a trimmed or oversized
    // buffer (the JS oracle writes e.memory.buffer, which is always exactly
    // the fixed page). A partial page would silently skip the zeroing of
    // padding words; an oversized buffer would let the packer touch bytes
    // beyond the guest's real memory.
    if (page.size() != core_wasm_resume_capacity::fixed_single_page_capacity().value) {
        return std::unexpected(FramePackError::PageBoundsExceeded);
    }
    std::fill(page.begin() + ir::core::kP6AggregateInputBase,
              page.begin() + ir::core::kP6CollectionBackingBase, std::uint8_t{0});

    FrameWalkContext ctx(section, input_binding.table());
    std::uint32_t arena_cursor = section.payload_arena_base;
    return pack_value_at(ctx, page, input_binding.root(), section.input_layout, input,
                      ir::core::kP6AggregateInputBase, arena_cursor,
                      section.payload_arena_base, section.payload_arena_capacity);
}

} // namespace ahfl::runtime::wasm_host
