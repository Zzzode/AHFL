#include "ahfl/compiler/ir/core_frame_layout.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace ahfl::ir::core {

namespace {

namespace layout_diag = ahfl::ir::core::layout;

[[nodiscard]] CoreLowerDiagnostic fail(std::string message) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(layout_diag::kInvalid),
                               std::move(message),
                               std::nullopt};
}

constexpr std::array<std::uint8_t, 6> kMagic{'A', 'H', 'F', 'L', 'C', 'L'};
constexpr std::uint8_t kVersion = 1;
constexpr std::uint8_t kTargetWasm32 = 0;

// Shape tags (a stable, explicit X-set — never reuse a retired ordinal).
constexpr std::uint8_t kShapePending = 0;
constexpr std::uint8_t kShapeScalar = 1;
constexpr std::uint8_t kShapeBytes = 2;
constexpr std::uint8_t kShapePtrLen = 3;
constexpr std::uint8_t kShapeFnRef = 4;
constexpr std::uint8_t kShapeClosure = 5;
constexpr std::uint8_t kShapeStruct = 6;
constexpr std::uint8_t kShapeEnum = 7;
constexpr std::uint8_t kShapeContainer = 8;
constexpr std::uint8_t kShapeUninhabited = 9;

constexpr std::uint8_t kReprI32 = 0;
constexpr std::uint8_t kReprI64 = 1;
constexpr std::uint8_t kReprF64 = 2;

class Encoder {
  public:
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> run(
        const CoreFrameLayoutSection &section) {
        bytes_.assign(kMagic.begin(), kMagic.end());
        u8(kVersion);

        // Target data layout.
        const TargetDataLayout &target = section.table.target;
        if (target.id != TargetDataLayoutId::Wasm32 || target.pointer_size != 4 ||
            target.pointer_align != 4 || target.function_index_size != 4 ||
            target.function_index_align != 4) {
            diagnostics_.push_back(fail("frame-layout target is not the canonical wasm32 layout"));
            return std::nullopt;
        }
        u8(kTargetWasm32);

        if (!u64_ok(section.table.layouts.size()) ||
            !u64_ok(section.table.value_layouts.size())) {
            diagnostics_.push_back(fail("frame-layout table exceeds its 32-bit id space"));
            return std::nullopt;
        }

        // Boundary roots come first so a reader fails on a bad root before
        // walking either table.
        id(section.input_layout);
        id(section.output_layout);

        // Layout table.
        u32_size(section.table.layouts.size());
        for (const CoreLayout &layout : section.table.layouts) {
            encode_layout(layout);
            if (!diagnostics_.empty()) {
                return std::nullopt;
            }
        }
        u32_size(section.table.value_layouts.size());
        for (const CoreLayoutId mapped : section.table.value_layouts) {
            id(mapped);
        }

        // Disjoint backing placements, in boundary-enumeration order.
        u32_size(section.placements.size());
        for (const CoreFrameBackingPlacement &placement : section.placements) {
            u32(placement.edge_index);
            id(placement.container_layout);
            u32(placement.base);
            u32(placement.extent);
        }

        // Frame-payload arena span.
        u32(section.payload_arena_base);
        u32(section.payload_arena_capacity);
        return std::move(bytes_);
    }

    [[nodiscard]] std::vector<CoreLowerDiagnostic> take_diagnostics() {
        return std::move(diagnostics_);
    }

  private:
    std::vector<std::uint8_t> bytes_;
    std::vector<CoreLowerDiagnostic> diagnostics_;

    void u8(std::uint8_t v) { bytes_.push_back(v); }

    void leb(std::uint64_t value) {
        do {
            std::uint8_t current = static_cast<std::uint8_t>(value & 0x7fU);
            value >>= 7U;
            if (value != 0) {
                current |= 0x80U;
            }
            u8(current);
        } while (value != 0);
    }
    void u32(std::uint32_t value) { leb(value); }
    void u64(std::uint64_t value) { leb(value); }
    void u32_size(std::size_t value) {
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            diagnostics_.push_back(fail("frame-layout vector exceeds its 32-bit encoding domain"));
            return;
        }
        u32(static_cast<std::uint32_t>(value));
    }
    [[nodiscard]] bool u64_ok(std::size_t value) const noexcept {
        return value < CoreLayoutId::kInvalid;
    }
    void id(CoreLayoutId value) {
        if (value.value == CoreLayoutId::kInvalid) {
            diagnostics_.push_back(fail("frame-layout carries an invalid layout id"));
            return;
        }
        u32(value.value);
    }

    void encode_layout(const CoreLayout &layout) {
        u64(layout.size);
        u32(layout.align);
        u8(layout.is_zero_sized ? 1U : 0U);
        std::visit(
            Overloaded{
                [&](const CoreLayoutPending &) {
                    diagnostics_.push_back(
                        fail("frame-layout table contains an unfinished layout placeholder"));
                },
                [&](const CoreLayoutScalar &shape) {
                    u8(kShapeScalar);
                    switch (shape.repr) {
                    case CoreScalarRepr::I32:
                        u8(kReprI32);
                        break;
                    case CoreScalarRepr::I64:
                        u8(kReprI64);
                        break;
                    case CoreScalarRepr::F64:
                        u8(kReprF64);
                        break;
                    }
                },
                [&](const CoreLayoutBytes &shape) {
                    u8(kShapeBytes);
                    u64(shape.byte_count);
                },
                [&](const CoreLayoutPtrLen &) { u8(kShapePtrLen); },
                [&](const CoreLayoutFnRef &) { u8(kShapeFnRef); },
                [&](const CoreLayoutClosure &shape) {
                    u8(kShapeClosure);
                    u8(shape.environment.has_value() ? 1U : 0U);
                    if (shape.environment.has_value()) {
                        id(*shape.environment);
                    }
                },
                [&](const CoreLayoutStruct &shape) {
                    u8(kShapeStruct);
                    u32_size(shape.field_offsets.size());
                    u32_size(shape.field_layouts.size());
                    for (const std::uint64_t offset : shape.field_offsets) {
                        u64(offset);
                    }
                    for (const CoreLayoutId field : shape.field_layouts) {
                        id(field);
                    }
                },
                [&](const CoreLayoutEnum &shape) {
                    u8(kShapeEnum);
                    u32(shape.tag_size);
                    u64(shape.payload_offset);
                    u32_size(shape.variant_payload_layouts.size());
                    u32_size(shape.variant_payload_sizes.size());
                    for (const CoreLayoutId payload : shape.variant_payload_layouts) {
                        id(payload);
                    }
                    for (const std::uint64_t size : shape.variant_payload_sizes) {
                        u64(size);
                    }
                },
                [&](const CoreLayoutContainer &shape) {
                    u8(kShapeContainer);
                    id(shape.element);
                    u8(shape.value.has_value() ? 1U : 0U);
                    if (shape.value.has_value()) {
                        id(*shape.value);
                    }
                    u64(shape.capacity);
                    u64(shape.stride);
                    u64(shape.value_offset);
                    u64(shape.backing_size);
                },
                [&](const CoreLayoutUninhabited &) { u8(kShapeUninhabited); },
            },
            layout.shape);
    }
};

class Cursor {
  public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }

    [[nodiscard]] std::optional<std::uint8_t> u8() {
        if (offset_ >= bytes_.size()) {
            return std::nullopt;
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] std::optional<std::uint64_t> leb() {
        std::uint64_t result = 0;
        std::uint32_t shift = 0;
        while (true) {
            const auto byte = u8();
            if (!byte.has_value()) {
                return std::nullopt;
            }
            if (shift == 63 && (*byte & 0x7fU) > 0x01U) {
                return std::nullopt;
            }
            if (shift >= 64) {
                return std::nullopt;
            }
            result |= static_cast<std::uint64_t>(*byte & 0x7fU) << shift;
            if ((*byte & 0x80U) == 0) {
                return result;
            }
            shift += 7;
        }
    }
    [[nodiscard]] std::optional<std::uint32_t> u32() {
        const auto value = leb();
        if (!value.has_value() || *value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value);
    }
    [[nodiscard]] std::optional<std::uint64_t> u64() { return leb(); }

    [[nodiscard]] std::optional<CoreLayoutId> id() {
        const auto value = u32();
        if (!value.has_value() || *value == CoreLayoutId::kInvalid) {
            return std::nullopt;
        }
        return CoreLayoutId{*value};
    }

    [[nodiscard]] bool match(std::span<const std::uint8_t> expected) {
        if (remaining() < expected.size()) {
            return false;
        }
        for (std::uint8_t byte : expected) {
            if (bytes_[offset_++] != byte) {
                return false;
            }
        }
        return true;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{0};
};

class Decoder {
  public:
    explicit Decoder(std::span<const std::uint8_t> bytes) : cursor_(bytes) {}

    [[nodiscard]] CoreFrameLayoutDecodeResult run() {
        CoreFrameLayoutDecodeResult result;
        if (!cursor_.match(kMagic)) {
            result.diagnostics.push_back(fail("frame-layout section has a bad magic header"));
            return result;
        }
        const auto version = cursor_.u8();
        if (!version.has_value() || *version != kVersion) {
            result.diagnostics.push_back(fail("frame-layout section has an unsupported version"));
            return result;
        }
        const auto target = cursor_.u8();
        if (!target.has_value() || *target != kTargetWasm32) {
            result.diagnostics.push_back(
                fail("frame-layout section carries an unsupported target data layout"));
            return result;
        }

        CoreFrameLayoutSection section;
        const auto input = cursor_.id();
        const auto output = cursor_.id();
        if (!input.has_value() || !output.has_value()) {
            result.diagnostics.push_back(fail("frame-layout boundary roots are malformed"));
            return result;
        }
        section.input_layout = *input;
        section.output_layout = *output;

        if (!decode_table(section.table)) {
            result.diagnostics = std::move(diagnostics_);
            return result;
        }
        if (!decode_placements(section)) {
            result.diagnostics = std::move(diagnostics_);
            return result;
        }
        const auto arena_base = cursor_.u32();
        const auto arena_capacity = cursor_.u32();
        if (!arena_base.has_value() || !arena_capacity.has_value() || !cursor_.at_end()) {
            result.diagnostics.push_back(fail("frame-layout arena span is malformed or the "
                                              "section carries trailing bytes"));
            return result;
        }
        section.payload_arena_base = *arena_base;
        section.payload_arena_capacity = *arena_capacity;

        auto local = verify_local(section);
        if (!local.empty()) {
            result.diagnostics = std::move(local);
            return result;
        }
        result.section = std::move(section);
        return result;
    }

  private:
    Cursor cursor_;
    std::vector<CoreLowerDiagnostic> diagnostics_;

    void bad(std::string message) {
        if (diagnostics_.empty()) {
            diagnostics_.push_back(fail(std::move(message)));
        }
    }

    [[nodiscard]] bool valid_id(const CoreLayoutTable &table, CoreLayoutId id) const noexcept {
        return id.value < table.layouts.size();
    }

    [[nodiscard]] bool decode_table(CoreLayoutTable &table) {
        table.target = TargetDataLayout{};
        const auto layout_count = cursor_.u32();
        if (!layout_count.has_value()) {
            bad("frame-layout layout table count is malformed");
            return false;
        }
        table.layouts.reserve(*layout_count);
        for (std::uint32_t i = 0; i < *layout_count; ++i) {
            auto layout = decode_layout();
            if (!layout.has_value()) {
                return false;
            }
            table.layouts.push_back(std::move(*layout));
        }
        const auto value_count = cursor_.u32();
        if (!value_count.has_value()) {
            bad("frame-layout value-layout mapping count is malformed");
            return false;
        }
        table.value_layouts.reserve(*value_count);
        for (std::uint32_t i = 0; i < *value_count; ++i) {
            const auto id = cursor_.id();
            if (!id.has_value() || !valid_id(table, *id)) {
                bad("frame-layout value-layout mapping references an unknown layout");
                return false;
            }
            table.value_layouts.push_back(*id);
        }
        return true;
    }

    [[nodiscard]] std::optional<CoreLayout> decode_layout() {
        const auto size = cursor_.u64();
        const auto align = cursor_.u32();
        const auto zero_sized = cursor_.u8();
        const auto tag = cursor_.u8();
        if (!size.has_value() || !align.has_value() || !zero_sized.has_value() ||
            !tag.has_value()) {
            bad("frame-layout layout header is truncated");
            return std::nullopt;
        }
        if (*align == 0 || (*align & (*align - 1)) != 0) {
            bad("frame-layout layout alignment is not a power of two");
            return std::nullopt;
        }
        CoreLayout layout;
        layout.size = *size;
        layout.align = *align;
        layout.is_zero_sized = *zero_sized != 0;
        switch (*tag) {
        case kShapePending:
            bad("frame-layout table contains an unfinished layout placeholder");
            return std::nullopt;
        case kShapeScalar: {
            const auto repr = cursor_.u8();
            if (!repr.has_value()) {
                bad("frame-layout scalar repr is truncated");
                return std::nullopt;
            }
            CoreScalarRepr scalar;
            switch (*repr) {
            case kReprI32:
                scalar = CoreScalarRepr::I32;
                break;
            case kReprI64:
                scalar = CoreScalarRepr::I64;
                break;
            case kReprF64:
                scalar = CoreScalarRepr::F64;
                break;
            default:
                bad("frame-layout scalar repr is unknown");
                return std::nullopt;
            }
            layout.shape = CoreLayoutScalar{scalar};
            break;
        }
        case kShapeBytes: {
            const auto byte_count = cursor_.u64();
            if (!byte_count.has_value()) {
                bad("frame-layout bytes shape is truncated");
                return std::nullopt;
            }
            layout.shape = CoreLayoutBytes{*byte_count};
            break;
        }
        case kShapePtrLen:
            layout.shape = CoreLayoutPtrLen{};
            break;
        case kShapeFnRef:
            layout.shape = CoreLayoutFnRef{};
            break;
        case kShapeClosure: {
            const auto has_env = cursor_.u8();
            if (!has_env.has_value()) {
                bad("frame-layout closure shape is truncated");
                return std::nullopt;
            }
            CoreLayoutClosure shape;
            if (*has_env != 0) {
                const auto env = cursor_.id();
                if (!env.has_value()) {
                    bad("frame-layout closure environment edge is malformed");
                    return std::nullopt;
                }
                shape.environment = *env;
            }
            layout.shape = std::move(shape);
            break;
        }
        case kShapeStruct: {
            // Encoder order: count(offsets), count(layouts), offsets, layouts.
            const auto offset_count = cursor_.u32();
            const auto layout_count = cursor_.u32();
            if (!offset_count.has_value() || !layout_count.has_value() ||
                *offset_count != *layout_count) {
                bad("frame-layout struct field counts are malformed or disagree");
                return std::nullopt;
            }
            CoreLayoutStruct shape;
            shape.field_offsets.reserve(*offset_count);
            for (std::uint32_t i = 0; i < *offset_count; ++i) {
                const auto offset = cursor_.u64();
                if (!offset.has_value()) {
                    bad("frame-layout struct field offset is truncated");
                    return std::nullopt;
                }
                shape.field_offsets.push_back(*offset);
            }
            shape.field_layouts.reserve(*layout_count);
            for (std::uint32_t i = 0; i < *layout_count; ++i) {
                const auto field = cursor_.id();
                if (!field.has_value()) {
                    bad("frame-layout struct field layout id is truncated");
                    return std::nullopt;
                }
                shape.field_layouts.push_back(*field);
            }
            layout.shape = std::move(shape);
            break;
        }
        case kShapeEnum: {
            // Encoder order: tag_size, payload_offset, count(payload_layouts),
            // count(payload_sizes), payload_layouts, payload_sizes.
            CoreLayoutEnum shape;
            const auto tag_size = cursor_.u32();
            const auto payload_offset = cursor_.u64();
            if (!tag_size.has_value() || !payload_offset.has_value()) {
                bad("frame-layout enum header is truncated");
                return std::nullopt;
            }
            shape.tag_size = *tag_size;
            shape.payload_offset = *payload_offset;
            const auto layout_count = cursor_.u32();
            const auto size_count = cursor_.u32();
            if (!layout_count.has_value() || !size_count.has_value() ||
                *layout_count != *size_count) {
                bad("frame-layout enum variant counts are malformed or disagree");
                return std::nullopt;
            }
            shape.variant_payload_layouts.reserve(*layout_count);
            for (std::uint32_t i = 0; i < *layout_count; ++i) {
                const auto payload = cursor_.id();
                if (!payload.has_value()) {
                    bad("frame-layout enum payload layout id is truncated");
                    return std::nullopt;
                }
                shape.variant_payload_layouts.push_back(*payload);
            }
            shape.variant_payload_sizes.reserve(*size_count);
            for (std::uint32_t i = 0; i < *size_count; ++i) {
                const auto size = cursor_.u64();
                if (!size.has_value()) {
                    bad("frame-layout enum payload size is truncated");
                    return std::nullopt;
                }
                shape.variant_payload_sizes.push_back(*size);
            }
            layout.shape = std::move(shape);
            break;
        }
        case kShapeContainer: {
            CoreLayoutContainer shape;
            const auto element = cursor_.id();
            const auto has_value = cursor_.u8();
            if (!element.has_value() || !has_value.has_value()) {
                bad("frame-layout container header is truncated");
                return std::nullopt;
            }
            shape.element = *element;
            if (*has_value != 0) {
                const auto value = cursor_.id();
                if (!value.has_value()) {
                    bad("frame-layout container value edge is malformed");
                    return std::nullopt;
                }
                shape.value = *value;
            }
            const auto capacity = cursor_.u64();
            const auto stride = cursor_.u64();
            const auto value_offset = cursor_.u64();
            const auto backing_size = cursor_.u64();
            if (!capacity.has_value() || !stride.has_value() || !value_offset.has_value() ||
                !backing_size.has_value()) {
                bad("frame-layout container extents are truncated");
                return std::nullopt;
            }
            shape.capacity = *capacity;
            shape.stride = *stride;
            shape.value_offset = *value_offset;
            shape.backing_size = *backing_size;
            layout.shape = std::move(shape);
            break;
        }
        case kShapeUninhabited:
            layout.shape = CoreLayoutUninhabited{};
            break;
        default:
            bad("frame-layout shape tag is unknown");
            return std::nullopt;
        }
        return layout;
    }

    [[nodiscard]] bool decode_placements(CoreFrameLayoutSection &section) {
        const auto count = cursor_.u32();
        if (!count.has_value()) {
            bad("frame-layout backing-placement count is malformed");
            return false;
        }
        section.placements.reserve(*count);
        for (std::uint32_t i = 0; i < *count; ++i) {
            const auto edge = cursor_.u32();
            const auto container = cursor_.id();
            const auto base = cursor_.u32();
            const auto extent = cursor_.u32();
            if (!edge.has_value() || !container.has_value() || !base.has_value() ||
                !extent.has_value()) {
                bad("frame-layout backing placement is truncated");
                return false;
            }
            section.placements.push_back(CoreFrameBackingPlacement{
                *edge, *container, *base, *extent});
        }
        return true;
    }

    // Local structural verification over the decoded tables (no CoreProgram is
    // present host-side).
    [[nodiscard]] std::vector<CoreLowerDiagnostic>
    verify_local(const CoreFrameLayoutSection &section) {
        std::vector<CoreLowerDiagnostic> diags;
        const CoreLayoutTable &table = section.table;
        const auto fail_local = [&](std::string message) {
            diags.push_back(fail(std::move(message)));
        };
        if (!valid_id(table, section.input_layout) || !valid_id(table, section.output_layout)) {
            fail_local("frame-layout boundary root id is out of range");
            return diags;
        }

        for (std::uint32_t i = 0; i < table.layouts.size(); ++i) {
            const CoreLayout &layout = table.layouts[i];
            const bool ok_shape = std::visit(
                Overloaded{
                    [](const CoreLayoutPending &) { return false; },
                    [&](const CoreLayoutScalar &) {
                        return layout.size == 4 || layout.size == 8;
                    },
                    [&](const CoreLayoutBytes &shape) {
                        return shape.byte_count == layout.size && shape.byte_count > 0;
                    },
                    [&](const CoreLayoutPtrLen &) { return layout.size == 8 && layout.align == 4; },
                    [](const CoreLayoutFnRef &) { return true; },
                    [&](const CoreLayoutClosure &shape) {
                        return layout.size == 8 && layout.align == 4 &&
                               (!shape.environment.has_value() || valid_id(table, *shape.environment));
                    },
                    [&](const CoreLayoutStruct &shape) {
                        if (shape.field_offsets.size() != shape.field_layouts.size()) {
                            return false;
                        }
                        for (std::uint32_t f = 0; f < shape.field_layouts.size(); ++f) {
                            if (!valid_id(table, shape.field_layouts[f])) {
                                return false;
                            }
                            if (shape.field_offsets[f] >= layout.size && layout.size != 0) {
                                return false;
                            }
                        }
                        return true;
                    },
                    [&](const CoreLayoutEnum &shape) {
                        return shape.tag_size == 4 &&
                               shape.variant_payload_layouts.size() ==
                                   shape.variant_payload_sizes.size() &&
                               std::ranges::all_of(shape.variant_payload_layouts,
                                                   [&](CoreLayoutId id) { return valid_id(table, id); });
                    },
                    [&](const CoreLayoutContainer &shape) {
                        return layout.size == 8 && layout.align == 4 &&
                               valid_id(table, shape.element) &&
                               (!shape.value.has_value() || valid_id(table, *shape.value));
                    },
                    [](const CoreLayoutUninhabited &) { return true; },
                },
                layout.shape);
            if (!ok_shape) {
                fail_local("frame-layout layout node is structurally invalid");
                return diags;
            }
        }

        // Placements: dense edge indices, container nodes, aligned extents, and
        // pairwise-disjoint fixed-page extents.
        constexpr std::uint64_t kBackingBase = kP6CollectionBackingBase;
        constexpr std::uint64_t kPageEnd = kCoreWasmFixedLinearMemoryCapacityBytes;
        std::uint64_t expected_cursor = kBackingBase;
        for (std::uint32_t i = 0; i < section.placements.size(); ++i) {
            const CoreFrameBackingPlacement &placement = section.placements[i];
            if (placement.edge_index != i) {
                fail_local("frame-layout backing placement edge indices are not dense and in "
                           "enumeration order");
                return diags;
            }
            if (!valid_id(table, placement.container_layout)) {
                fail_local("frame-layout backing placement names an unknown layout");
                return diags;
            }
            const auto *container = std::get_if<CoreLayoutContainer>(
                &table.layouts[placement.container_layout.value].shape);
            if (container == nullptr) {
                fail_local("frame-layout backing placement does not name a container layout");
                return diags;
            }
            const std::uint64_t aligned_extent = (container->backing_size + 7u) & ~std::uint64_t{7u};
            if (placement.extent != aligned_extent || placement.extent == 0) {
                fail_local("frame-layout backing placement extent does not match its container's "
                           "aligned backing size");
                return diags;
            }
            if (placement.base != expected_cursor) {
                fail_local("frame-layout backing placements do not follow the "
                           "sum-of-prior-backing rule or two placements overlap");
                return diags;
            }
            if (placement.base < kBackingBase ||
                static_cast<std::uint64_t>(placement.base) + placement.extent > kPageEnd) {
                fail_local("frame-layout backing placement lies outside the fixed page");
                return diags;
            }
            expected_cursor = placement.base + placement.extent;
        }

        // Arena span begins exactly at the aligned post-placement high-water and
        // stays inside the page.
        const std::uint64_t expected_arena = (expected_cursor + 7u) & ~std::uint64_t{7u};
        if (section.payload_arena_base != expected_arena) {
            fail_local("frame-layout payload arena does not begin at the aligned backing "
                       "high-water");
            return diags;
        }
        if (static_cast<std::uint64_t>(section.payload_arena_base) +
                section.payload_arena_capacity >
            kPageEnd) {
            fail_local("frame-layout payload arena exceeds the fixed page");
            return diags;
        }
        return diags;
    }
};

// ---------------------------------------------------------------------------
// Layout <-> wire structural consistency
// ---------------------------------------------------------------------------

class ConsistencyChecker {
  public:
    ConsistencyChecker(const CoreLayoutTable &layouts, const CoreWireSchemaTable &wire)
        : layouts_(layouts), wire_(wire) {}

    [[nodiscard]] std::vector<CoreLowerDiagnostic> run(CoreLayoutId layout_root,
                                                       CoreWireSchemaNodeId wire_root) {
        if (!check_pair(layout_root, wire_root)) {
            return std::vector<CoreLowerDiagnostic>{fail(message_)};
        }
        return {};
    }

  private:
    const CoreLayoutTable &layouts_;
    const CoreWireSchemaTable &wire_;
    std::string message_;
    // (layout id, wire node id) pairs already accepted on the current DFS path;
    // revisiting a pair terminates recursion over a recursive nominal.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> in_progress_;

    [[nodiscard]] bool reject(std::string message) {
        message_ = std::move(message);
        return false;
    }

    [[nodiscard]] bool wire_id_valid(CoreWireSchemaNodeId id) const noexcept {
        return id.value < wire_.nodes.size();
    }
    [[nodiscard]] bool layout_valid(CoreLayoutId id) const noexcept {
        return id.value < layouts_.layouts.size();
    }

    [[nodiscard]] bool check_pair(CoreLayoutId layout_id, CoreWireSchemaNodeId wire_id) {
        if (!layout_valid(layout_id) || !wire_id_valid(wire_id)) {
            return reject("frame layout/wire root id is out of range");
        }
        const std::pair key{layout_id.value, wire_id.value};
        if (std::ranges::find(in_progress_, key) != in_progress_.end()) {
            return true; // recursive nominal pair
        }
        in_progress_.push_back(key);
        const bool ok = check_shapes(layouts_.layouts[layout_id.value],
                                     wire_.nodes[wire_id.value].shape);
        in_progress_.pop_back();
        return ok;
    }

    [[nodiscard]] bool check_scalar_repr(CoreScalarRepr repr,
                                         const CoreWireSchemaInt &schema) {
        const bool narrow =
            schema.bounds.has_value() &&
            schema.bounds->first >= std::numeric_limits<std::int32_t>::min() &&
            schema.bounds->second <= std::numeric_limits<std::int32_t>::max();
        const CoreScalarRepr expected = narrow ? CoreScalarRepr::I32 : CoreScalarRepr::I64;
        return repr == expected;
    }

    [[nodiscard]] bool check_shapes(const CoreLayout &layout,
                                    const CoreWireSchemaShape &shape) {
        return std::visit(
            Overloaded{
                [&](const CoreWireSchemaUnit &) {
                    // Unit loweres to a zero-sized empty struct layout.
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    return s != nullptr && layout.is_zero_sized && s->field_layouts.empty();
                },
                [&](const CoreWireSchemaBool &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && s->repr == CoreScalarRepr::I32;
                },
                [&](const CoreWireSchemaInt &schema) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && check_scalar_repr(s->repr, schema);
                },
                [&](const CoreWireSchemaFloat &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && s->repr == CoreScalarRepr::F64;
                },
                [&](const CoreWireSchemaString &) {
                    return std::holds_alternative<CoreLayoutPtrLen>(layout.shape);
                },
                [&](const CoreWireSchemaDecimal &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && s->repr == CoreScalarRepr::I64;
                },
                [&](const CoreWireSchemaDuration &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && s->repr == CoreScalarRepr::I64;
                },
                [&](const CoreWireSchemaTimestamp &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    return s != nullptr && s->repr == CoreScalarRepr::I64;
                },
                [&](const CoreWireSchemaUuid &) {
                    const auto *s = std::get_if<CoreLayoutBytes>(&layout.shape);
                    return s != nullptr && s->byte_count == 16;
                },
                [&](const CoreWireSchemaOption &schema) {
                    const auto *e = std::get_if<CoreLayoutEnum>(&layout.shape);
                    if (e == nullptr || e->variant_payload_layouts.size() != 2 ||
                        e->variant_payload_sizes[0] != 0) {
                        return reject("frame layout/wire disagree on Option enum shape");
                    }
                    const CoreLayoutId some_payload = e->variant_payload_layouts[1];
                    if (!layout_valid(some_payload)) {
                        return reject("frame layout/wire Option payload edge is out of range");
                    }
                    const auto *payload =
                        std::get_if<CoreLayoutStruct>(&layouts_.layouts[some_payload.value].shape);
                    if (payload == nullptr || payload->field_layouts.size() != 1) {
                        return reject("frame layout/wire Option::Some payload is not one slot");
                    }
                    return check_pair(payload->field_layouts[0], schema.value);
                },
                [&](const CoreWireSchemaSequence &schema) {
                    const auto *c = std::get_if<CoreLayoutContainer>(&layout.shape);
                    if (c == nullptr || c->value.has_value()) {
                        return reject("frame layout/wire disagree on sequence container shape");
                    }
                    const std::uint64_t expected_capacity =
                        schema.capacity.value_or(c->capacity);
                    if (c->capacity != expected_capacity) {
                        return reject("frame layout/wire sequence capacities disagree");
                    }
                    return check_pair(c->element, schema.element);
                },
                [&](const CoreWireSchemaMap &schema) {
                    const auto *c = std::get_if<CoreLayoutContainer>(&layout.shape);
                    if (c == nullptr || !c->value.has_value()) {
                        return reject("frame layout/wire disagree on map container shape");
                    }
                    const std::uint64_t expected_capacity =
                        schema.capacity.value_or(c->capacity);
                    if (c->capacity != expected_capacity) {
                        return reject("frame layout/wire map capacities disagree");
                    }
                    if (!std::holds_alternative<CoreLayoutPtrLen>(
                            layouts_.layouts[c->element.value].shape)) {
                        return reject("frame layout/wire map key is not a String PtrLen");
                    }
                    return check_pair(c->element, schema.key) &&
                           check_pair(*c->value, schema.value);
                },
                [&](const CoreWireSchemaStruct &schema) {
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    if (s == nullptr || s->field_layouts.size() != schema.fields.size()) {
                        return reject("frame layout/wire struct field arity disagrees");
                    }
                    for (std::uint32_t i = 0; i < schema.fields.size(); ++i) {
                        if (!check_pair(s->field_layouts[i], schema.fields[i].type)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const CoreWireSchemaEnum &schema) {
                    const auto *e = std::get_if<CoreLayoutEnum>(&layout.shape);
                    if (e == nullptr || e->variant_payload_layouts.size() != schema.variants.size()) {
                        return reject("frame layout/wire enum variant arity disagrees");
                    }
                    for (std::uint32_t i = 0; i < schema.variants.size(); ++i) {
                        const CoreLayoutId payload_id = e->variant_payload_layouts[i];
                        if (!layout_valid(payload_id)) {
                            return reject("frame layout/wire enum payload edge is out of range");
                        }
                        const auto *payload =
                            std::get_if<CoreLayoutStruct>(&layouts_.layouts[payload_id.value].shape);
                        const std::uint32_t slot_count =
                            static_cast<std::uint32_t>(schema.variants[i].slots.size());
                        if (payload == nullptr || payload->field_layouts.size() != slot_count) {
                            return reject("frame layout/wire enum payload slot arity disagrees");
                        }
                        for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
                            if (!check_pair(payload->field_layouts[slot],
                                            schema.variants[i].slots[slot].type)) {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                [&](const CoreWireSchemaTuple &schema) {
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    if (s == nullptr || s->field_layouts.size() != schema.elements.size()) {
                        return reject("frame layout/wire tuple arity disagrees");
                    }
                    for (std::uint32_t i = 0; i < schema.elements.size(); ++i) {
                        if (!check_pair(s->field_layouts[i], schema.elements[i])) {
                            return false;
                        }
                    }
                    return true;
                },
            },
            shape);
    }
};

} // namespace

bool CoreFrameLayoutEncodeResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const CoreLowerDiagnostic &d) {
        return d.severity == CoreDiagnosticSeverity::Error;
    });
}

bool CoreFrameLayoutDecodeResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const CoreLowerDiagnostic &d) {
        return d.severity == CoreDiagnosticSeverity::Error;
    });
}

CoreFrameLayoutEncodeResult
encode_core_frame_layout_section(const CoreFrameLayoutSection &section) {
    CoreFrameLayoutEncodeResult result;
    Encoder encoder;
    auto bytes = encoder.run(section);
    result.diagnostics = encoder.take_diagnostics();
    if (bytes.has_value() && result.diagnostics.empty()) {
        result.bytes = std::move(*bytes);
    }
    return result;
}

CoreFrameLayoutDecodeResult
decode_core_frame_layout_section(std::span<const std::uint8_t> bytes) {
    Decoder decoder(bytes);
    CoreFrameLayoutDecodeResult decoded = decoder.run();
    if (!decoded.ok()) {
        return decoded;
    }
    // Canonical re-encode equality: a non-canonical but otherwise parseable
    // payload (overlong LEB, reordered vector, extra byte) must never mint a
    // section.
    auto reencoded = encode_core_frame_layout_section(*decoded.section);
    if (!reencoded.ok() || !reencoded.bytes.has_value() ||
        reencoded.bytes->size() != bytes.size() ||
        !std::equal(reencoded.bytes->begin(), reencoded.bytes->end(), bytes.begin())) {
        CoreFrameLayoutDecodeResult rejected;
        rejected.diagnostics.push_back(
            fail("frame-layout section is not its canonical re-encoding"));
        return rejected;
    }
    return decoded;
}

std::vector<CoreLowerDiagnostic>
verify_frame_layout_wire_consistency(const CoreLayoutTable &layouts,
                                     CoreLayoutId layout_root,
                                     const CoreWireSchemaTable &wire,
                                     CoreWireSchemaNodeId wire_root) {
    ConsistencyChecker checker(layouts, wire);
    return checker.run(layout_root, wire_root);
}

} // namespace ahfl::ir::core
