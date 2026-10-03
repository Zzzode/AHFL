#include "ahfl/compiler/ir/core_frame_layout.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_layout_geometry.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::ir::core {

namespace {

namespace layout_diag = ahfl::ir::core::layout;

[[nodiscard]] CoreLowerDiagnostic fail(std::string message) {
    // kr68 §12.16: whole-program frame-layout diagnostic — no owning module;
    // source_module deliberately left empty.
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(layout_diag::kInvalid),
                               std::move(message),
                               std::nullopt, {}};
}

constexpr std::array<std::uint8_t, 6> kMagic{'A', 'H', 'F', 'L', 'C', 'L'};
// Frame-bridge v2 D1 (rung V2-B): payload format v2 appends the rodata span
// after the arena span. v1 payloads end exactly at the arena span and stay
// decodable; the encoder writes kVersion.
// Frame-bridge v2 fix-forward: payload format v3 appends each bridge call
// site's private scalar/PtrLen spill window [spill_base, +spill_extent) to the
// per-site record, so an embedded host can region-membership-test every
// spilled descriptor against the site's own declared window (design 4.3/5).
// v2 records stay decodable with zero spill windows.
constexpr std::uint8_t kVersion = 3;
constexpr std::uint8_t kMinVersion = 1;
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
        if (section.format_version < kMinVersion || section.format_version > kVersion) {
            diagnostics_.push_back(
                fail("frame-layout section requests an unsupported format version"));
            return std::nullopt;
        }
        // A v1 payload carries no rodata span; a v2 section must report the v1
        // constants with a zero rodata extent (the canonical no-Data-section
        // shape), never a half-upgraded form.
        const bool carries_v3_spill_windows =
            std::any_of(section.bridge_call_sites.begin(),
                        section.bridge_call_sites.end(),
                        [](const CoreFrameBridgeCallSite &site) {
                            return site.spill_base != 0 || site.spill_extent != 0;
                        });
        if (section.format_version == 1 &&
            (section.rodata_base != 0 || section.rodata_extent != 0 ||
             section.bridge_control_base != 0 || section.bridge_block_stride != 0 ||
             section.bridge_control_extent != 0 || section.bridge_spill_base != 0 ||
             section.bridge_spill_extent != 0 || !section.bridge_call_sites.empty() ||
             !section.node_blocks.empty() || section.entry_payload_base != 0 ||
             section.entry_payload_capacity != 0 || section.workflow_output_base != 0 ||
             section.state_trace_base != 0 || section.state_trace_capacity != 0 ||
             !section.transcode_sites.empty() ||
             section.transcode_shadow_base != 0 ||
             section.transcode_shadow_extent != 0 ||
             section.transcode_payload_base != 0 ||
             section.transcode_payload_capacity != 0 ||
             section.transcode_entry_shadow_base != 0 ||
             section.transcode_entry_shadow_extent != 0)) {
            diagnostics_.push_back(
                fail("a v1 frame-layout section cannot carry a rodata or bridge span"));
            return std::nullopt;
        }
        // The per-site private spill windows are a v3 addition; a v2 payload
        // predates them and must carry zero words.
        if (section.format_version < 3 && carries_v3_spill_windows) {
            diagnostics_.push_back(
                fail("a v2 frame-layout section cannot carry per-site bridge spill windows"));
            return std::nullopt;
        }
        bytes_.assign(kMagic.begin(), kMagic.end());
        u8(static_cast<std::uint8_t>(section.format_version));

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
        // walking either table. The input root is a raw u32 because a workflow
        // section encodes kInvalid there (its boundary roots are the wire
        // schema's per-node arrays); verify_local rejects kInvalid on an agent
        // section.
        boundary_root_id(section.input_layout);
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
        // V2-B: the rodata literal-pool span (base is kP6RodataBase).
        if (section.format_version >= 2) {
            u32(section.rodata_base);
            u32(section.rodata_extent);
            // V2-C: the capability-bridge control-block page frame followed by
            // the dense per-call-site records (root placement + payload span +
            // dense param/result layout ids).
            u32(section.bridge_control_base);
            u32(section.bridge_block_stride);
            u32(section.bridge_control_extent);
            u32(section.bridge_spill_base);
            u32(section.bridge_spill_extent);
            u32_size(section.bridge_call_sites.size());
            for (const CoreFrameBridgeCallSite &site : section.bridge_call_sites) {
                u32(site.call_site_id);
                u64(site.source_symbol);
                u32(site.arity);
                u32(site.block_offset);
                u32(site.result_base);
                u32(site.result_extent);
                u32(site.result_payload_base);
                u32(site.result_payload_capacity);
                id(site.result_layout);
                u32_size(site.param_layouts.size());
                for (const CoreLayoutId param : site.param_layouts) {
                    id(param);
                }
                // v3: the site's private scalar/PtrLen spill window.
                if (section.format_version >= 3) {
                    u32(site.spill_base);
                    u32(site.spill_extent);
                }
            }
            // V2-D: the presence-gated workflow node-packaging extension.
            if (!section.node_blocks.empty()) {
                u8(1);
                u32(section.entry_payload_base);
                u32(section.entry_payload_capacity);
                u32(section.workflow_output_base);
                u32_size(section.node_blocks.size());
                for (const CoreFrameLayoutSection::NodeBlock &block : section.node_blocks) {
                    id(block.input_layout);
                    id(block.context_layout);
                    id(block.output_layout);
                    u32(block.input_size);
                    u32(block.context_size);
                    u32(block.output_size);
                    u32(block.input_base);
                    u32(block.context_base);
                    u32(block.scratch_base);
                    u32(block.scratch_size);
                    u32(block.output_base);
                }
                u32(section.state_trace_base);
                u32(section.state_trace_capacity);
                // WH-5b.3: the presence-gated transcode extension (shadow
                // spans + the dense transcode_sites table). A workflow with no
                // crossed lane edge carries no sites and keeps its exact bytes.
                if (!section.transcode_sites.empty()) {
                    u8(1);
                    u32(section.transcode_shadow_base);
                    u32(section.transcode_shadow_extent);
                    u32(section.transcode_payload_base);
                    u32(section.transcode_payload_capacity);
                    u32(section.transcode_entry_shadow_base);
                    u32(section.transcode_entry_shadow_extent);
                    u32_size(section.transcode_sites.size());
                    for (const CoreFrameTranscodeSite &site : section.transcode_sites) {
                        u32(site.import_ordinal);
                        u8(static_cast<std::uint8_t>(site.direction));
                        u8(static_cast<std::uint8_t>(site.source));
                        u32(site.source_node_ordinal);
                        u32(site.target_node_ordinal);
                        id(site.layout);
                        // WH-5c.4 P1-2: the dense capability parameter index
                        // (Source::CapabilityParam only; 0 otherwise).
                        u32(site.param_ordinal);
                    }
                }
            }
        }
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
    // A boundary root may be the kInvalid sentinel (the workflow section has no
    // agent input root; its roots are the per-node wire arrays), encoded as the
    // raw u32 so it round-trips without being mistaken for a table id.
    void boundary_root_id(CoreLayoutId value) { u32(value.value); }

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
    // Boundary-root counterpart of Encoder::boundary_root_id: kInvalid is a
    // legal sentinel for a workflow input root; local verification is where an
    // agent section rejects it.
    [[nodiscard]] std::optional<CoreLayoutId> boundary_root_id() {
        const auto value = u32();
        if (!value.has_value()) {
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
        if (!version.has_value() || *version < kMinVersion || *version > kVersion) {
            result.diagnostics.push_back(fail("frame-layout section has an unsupported version"));
            return result;
        }
        const std::uint8_t payload_version = *version;
        const auto target = cursor_.u8();
        if (!target.has_value() || *target != kTargetWasm32) {
            result.diagnostics.push_back(
                fail("frame-layout section carries an unsupported target data layout"));
            return result;
        }

        CoreFrameLayoutSection section;
        const auto input = cursor_.boundary_root_id();
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
        if (!arena_base.has_value() || !arena_capacity.has_value()) {
            result.diagnostics.push_back(fail("frame-layout arena span is malformed"));
            return result;
        }
        section.payload_arena_base = *arena_base;
        section.payload_arena_capacity = *arena_capacity;
        section.format_version = payload_version;

        // V2-B: a v2 payload carries the rodata span after the arena span; a v1
        // payload ends exactly here (its rodata span is absent / zero).
        if (payload_version < 2) {
            if (!cursor_.at_end()) {
                result.diagnostics.push_back(fail("frame-layout v1 section carries trailing bytes"));
                return result;
            }
        } else {
            const auto rodata_base = cursor_.u32();
            const auto rodata_extent = cursor_.u32();
            if (!rodata_base.has_value() || !rodata_extent.has_value()) {
                result.diagnostics.push_back(fail("frame-layout rodata span is malformed"));
                return result;
            }
            section.rodata_base = *rodata_base;
            section.rodata_extent = *rodata_extent;
            if (!decode_bridge_sites(section)) {
                result.diagnostics = std::move(diagnostics_);
                return result;
            }
            // V2-D: the optional trailing workflow node-packaging extension.
            if (!cursor_.at_end()) {
                const auto present = cursor_.u8();
                if (!present.has_value() || *present != 1) {
                    result.diagnostics.push_back(
                        fail("frame-layout v2 node-block tag is malformed"));
                    return result;
                }
                const auto entry_base = cursor_.u32();
                const auto entry_capacity = cursor_.u32();
                const auto wf_output = cursor_.u32();
                if (!entry_base.has_value() || !entry_capacity.has_value() ||
                    !wf_output.has_value()) {
                    result.diagnostics.push_back(
                        fail("frame-layout workflow span is malformed"));
                    return result;
                }
                section.entry_payload_base = *entry_base;
                section.entry_payload_capacity = *entry_capacity;
                section.workflow_output_base = *wf_output;
                const auto block_count = cursor_.u32();
                if (!block_count.has_value() || !bounded_count(*block_count)) {
                    result.diagnostics.push_back(
                        fail("frame-layout node-block count is malformed"));
                    return result;
                }
                section.node_blocks.reserve(*block_count);
                for (std::uint32_t i = 0; i < *block_count; ++i) {
                    CoreFrameLayoutSection::NodeBlock block;
                    const auto input_layout = cursor_.id();
                    const auto context_layout = cursor_.id();
                    const auto output_layout = cursor_.id();
                    if (!input_layout.has_value() || !context_layout.has_value() ||
                        !output_layout.has_value()) {
                        bad("frame-layout node-block root layout is truncated");
                        return result;
                    }
                    block.input_layout = *input_layout;
                    block.context_layout = *context_layout;
                    block.output_layout = *output_layout;
                    const auto isize = cursor_.u32();
                    const auto csize = cursor_.u32();
                    const auto osize = cursor_.u32();
                    const auto ibase = cursor_.u32();
                    const auto cbase = cursor_.u32();
                    const auto sbase = cursor_.u32();
                    const auto ssize = cursor_.u32();
                    const auto obase = cursor_.u32();
                    if (!isize || !csize || !osize || !ibase || !cbase || !sbase ||
                        !ssize || !obase) {
                        bad("frame-layout node-block span is truncated");
                        return result;
                    }
                    block.input_size = *isize;
                    block.context_size = *csize;
                    block.output_size = *osize;
                    block.input_base = *ibase;
                    block.context_base = *cbase;
                    block.scratch_base = *sbase;
                    block.scratch_size = *ssize;
                    block.output_base = *obase;
                    section.node_blocks.push_back(std::move(block));
                }
                const auto trace_base = cursor_.u32();
                const auto trace_capacity = cursor_.u32();
                if (!trace_base.has_value() || !trace_capacity.has_value()) {
                    result.diagnostics.push_back(
                        fail("frame-layout workflow state-trace span is truncated"));
                    return result;
                }
                section.state_trace_base = *trace_base;
                section.state_trace_capacity = *trace_capacity;
                // WH-5b.3: the optional trailing transcode extension.
                if (!cursor_.at_end()) {
                    const auto present = cursor_.u8();
                    if (!present.has_value() || *present != 1) {
                        result.diagnostics.push_back(
                            fail("frame-layout transcode tag is malformed"));
                        return result;
                    }
                    const auto shadow_base = cursor_.u32();
                    const auto shadow_extent = cursor_.u32();
                    const auto payload_base = cursor_.u32();
                    const auto payload_capacity = cursor_.u32();
                    const auto entry_shadow_base = cursor_.u32();
                    const auto entry_shadow_extent = cursor_.u32();
                    if (!shadow_base || !shadow_extent || !payload_base ||
                        !payload_capacity || !entry_shadow_base ||
                        !entry_shadow_extent) {
                        result.diagnostics.push_back(
                            fail("frame-layout transcode shadow span is malformed"));
                        return result;
                    }
                    section.transcode_shadow_base = *shadow_base;
                    section.transcode_shadow_extent = *shadow_extent;
                    section.transcode_payload_base = *payload_base;
                    section.transcode_payload_capacity = *payload_capacity;
                    section.transcode_entry_shadow_base = *entry_shadow_base;
                    section.transcode_entry_shadow_extent = *entry_shadow_extent;
                    const auto site_count = cursor_.u32();
                    if (!site_count.has_value() || !bounded_count(*site_count) ||
                        *site_count == 0) {
                        result.diagnostics.push_back(
                            fail("frame-layout transcode-site count is malformed"));
                        return result;
                    }
                    section.transcode_sites.reserve(*site_count);
                    for (std::uint32_t i = 0; i < *site_count; ++i) {
                        CoreFrameTranscodeSite site;
                        const auto import_ordinal = cursor_.u32();
                        const auto direction = cursor_.u8();
                        const auto source = cursor_.u8();
                        const auto source_node = cursor_.u32();
                        const auto target_node = cursor_.u32();
                        const auto layout = cursor_.id();
                        const auto param_ordinal = cursor_.u32();
                        if (!import_ordinal || !direction || !source ||
                            !source_node || !target_node || !layout.has_value() ||
                            !param_ordinal) {
                            bad("frame-layout transcode-site record is truncated");
                            return result;
                        }
                        if (*direction >
                                static_cast<std::uint8_t>(
                                    CoreFrameTranscodeSite::Direction::JsonToP4D) ||
                            *source >
                                static_cast<std::uint8_t>(
                                    CoreFrameTranscodeSite::Source::CapabilityParam)) {
                            bad("frame-layout transcode-site carries an unknown direction or source");
                            return result;
                        }
                        site.import_ordinal = *import_ordinal;
                        site.direction =
                            static_cast<CoreFrameTranscodeSite::Direction>(*direction);
                        site.source =
                            static_cast<CoreFrameTranscodeSite::Source>(*source);
                        site.source_node_ordinal = *source_node;
                        site.target_node_ordinal = *target_node;
                        site.layout = *layout;
                        site.param_ordinal = *param_ordinal;
                        section.transcode_sites.push_back(std::move(site));
                    }
                }
            }
            if (!cursor_.at_end()) {
                result.diagnostics.push_back(
                    fail("frame-layout v2 section carries trailing bytes"));
                return result;
            }
        }

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

    [[nodiscard]] static bool
    valid_id(const CoreLayoutTable &table, CoreLayoutId id) noexcept {
        return id.value < table.layouts.size();
    }

    // Mirror the wire-schema decoder's bounded_count: a decoded count can never
    // name the reserved 32-bit sentinel and can never exceed the bytes still
    // available (each element costs at least one). This MUST run before any
    // reserve(): an attacker-inflated count would otherwise force a multi-GB
    // allocation and terminate the embedding host with std::bad_alloc instead
    // of returning a failed decode result.
    [[nodiscard]] bool bounded_count(std::uint32_t count) {
        if (count >= CoreLayoutId::kInvalid) {
            bad("frame-layout payload declares a count at the reserved 32-bit sentinel");
            return false;
        }
        if (static_cast<std::uint64_t>(count) > cursor_.remaining()) {
            bad("frame-layout payload declares more elements than remaining bytes");
            return false;
        }
        return true;
    }

    [[nodiscard]] bool decode_table(CoreLayoutTable &table) {
        table.target = TargetDataLayout{};
        const auto layout_count = cursor_.u32();
        if (!layout_count.has_value()) {
            bad("frame-layout layout table count is malformed");
            return false;
        }
        if (!bounded_count(*layout_count)) {
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
        if (!bounded_count(*value_count)) {
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
            if (!bounded_count(*offset_count)) {
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
            if (!bounded_count(*layout_count)) {
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

    // V2-C: decode the capability-bridge control-frame span and the dense
    // per-call-site records.
    [[nodiscard]] bool decode_bridge_sites(CoreFrameLayoutSection &section) {
        const auto control_base = cursor_.u32();
        const auto block_stride = cursor_.u32();
        const auto control_extent = cursor_.u32();
        const auto spill_base = cursor_.u32();
        const auto spill_extent = cursor_.u32();
        if (!control_base.has_value() || !block_stride.has_value() ||
            !control_extent.has_value() || !spill_base.has_value() ||
            !spill_extent.has_value()) {
            bad("frame-layout bridge control-frame span is malformed");
            return false;
        }
        section.bridge_control_base = *control_base;
        section.bridge_block_stride = *block_stride;
        section.bridge_control_extent = *control_extent;
        section.bridge_spill_base = *spill_base;
        section.bridge_spill_extent = *spill_extent;

        const auto site_count = cursor_.u32();
        if (!site_count.has_value()) {
            bad("frame-layout bridge call-site count is malformed");
            return false;
        }
        if (!bounded_count(*site_count)) {
            return false;
        }
        section.bridge_call_sites.reserve(*site_count);
        for (std::uint32_t i = 0; i < *site_count; ++i) {
            CoreFrameBridgeCallSite site;
            const auto call_site_id = cursor_.u32();
            const auto source_symbol = cursor_.u64();
            const auto arity = cursor_.u32();
            const auto block_offset = cursor_.u32();
            const auto result_base = cursor_.u32();
            const auto result_extent = cursor_.u32();
            const auto payload_base = cursor_.u32();
            const auto payload_capacity = cursor_.u32();
            const auto result_layout = cursor_.id();
            if (!call_site_id.has_value() || !source_symbol.has_value() ||
                !arity.has_value() || !block_offset.has_value() ||
                !result_base.has_value() || !result_extent.has_value() ||
                !payload_base.has_value() || !payload_capacity.has_value() ||
                !result_layout.has_value()) {
                bad("frame-layout bridge call-site record is truncated");                return false;
            }
            site.call_site_id = *call_site_id;
            site.source_symbol = *source_symbol;
            site.arity = *arity;
            site.block_offset = *block_offset;
            site.result_base = *result_base;
            site.result_extent = *result_extent;
            site.result_payload_base = *payload_base;
            site.result_payload_capacity = *payload_capacity;
            site.result_layout = *result_layout;
            const auto param_count = cursor_.u32();
            if (!param_count.has_value() || !bounded_count(*param_count)) {
                bad("frame-layout bridge call-site param count is malformed");
                return false;
            }
            if (*param_count != site.arity) {
                bad("frame-layout bridge call-site param-layout count disagrees with its arity");
                return false;
            }
            site.param_layouts.reserve(*param_count);
            for (std::uint32_t p = 0; p < *param_count; ++p) {
                const auto param = cursor_.id();
                if (!param.has_value()) {
                    bad("frame-layout bridge call-site param layout id is truncated");
                    return false;
                }
                site.param_layouts.push_back(*param);
            }
            // v3: the site's private scalar/PtrLen spill window.
            if (section.format_version >= 3) {
                const auto spill_base = cursor_.u32();
                const auto spill_extent = cursor_.u32();
                if (!spill_base.has_value() || !spill_extent.has_value()) {
                    bad("frame-layout bridge call-site spill window is truncated");
                    return false;
                }
                site.spill_base = *spill_base;
                site.spill_extent = *spill_extent;
            }
            section.bridge_call_sites.push_back(std::move(site));
        }
        return true;
    }

    [[nodiscard]] bool decode_placements(CoreFrameLayoutSection &section) {
        const auto count = cursor_.u32();
        if (!count.has_value()) {
            bad("frame-layout backing-placement count is malformed");
            return false;
        }
        if (!bounded_count(*count)) {
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
        // A workflow section (node blocks present) has no agent input root: its
        // boundary roots are the per-node arrays in the wire schema and the
        // input root is encoded kInvalid. The workflow OUTPUT root is still a
        // real layout id (run2 returns the workflow output slot at that root).
        const bool workflow_section = !section.node_blocks.empty();
        const bool input_root_ok =
            workflow_section
                ? section.input_layout.value == CoreLayoutId::kInvalid ||
                      valid_id(table, section.input_layout)
                : valid_id(table, section.input_layout);
        if (!input_root_ok || !valid_id(table, section.output_layout)) {
            fail_local("frame-layout boundary root id is out of range");
            return diags;
        }

        for (std::uint32_t i = 0; i < table.layouts.size(); ++i) {
            const CoreLayout &layout = table.layouts[i];
            const bool ok_shape = std::visit(
                Overloaded{
                    [](const CoreLayoutPending &) { return false; },
                    [&](const CoreLayoutScalar &shape) {
                        const bool i32 = shape.repr == CoreScalarRepr::I32;
                        const bool wide = shape.repr == CoreScalarRepr::I64 ||
                                          shape.repr == CoreScalarRepr::F64;
                        return (i32 || wide) &&
                               (i32 ? (layout.size == 4 && layout.align == 4)
                                    : (layout.size == 8 && layout.align == 8));
                    },
                    [&](const CoreLayoutBytes &shape) {
                        return shape.byte_count == layout.size && shape.byte_count > 0 &&
                               layout.align == 1;
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
                        for (const CoreLayoutId field : shape.field_layouts) {
                            if (!valid_id(table, field)) {
                                return false;
                            }
                        }
                        // Re-derive canonical offsets/size from the child layouts;
                        // a transported section must not ship its own.
                        const auto expected = layout_geometry::expected_struct(table, shape);
                        return expected.has_value() &&
                               shape.field_offsets == expected->field_offsets &&
                               layout.size == expected->size && layout.align == expected->align;
                    },
                    [&](const CoreLayoutEnum &shape) {
                        if (shape.tag_size != 4 ||
                            shape.variant_payload_layouts.size() !=
                                shape.variant_payload_sizes.size()) {
                            return false;
                        }
                        for (const CoreLayoutId payload : shape.variant_payload_layouts) {
                            if (!valid_id(table, payload)) {
                                return false;
                            }
                        }
                        // Re-derive the tag/payload gap and record size.
                        const auto expected = layout_geometry::expected_enum(table, shape);
                        return expected.has_value() &&
                               shape.payload_offset == expected->payload_offset &&
                               layout.size == expected->size && layout.align == expected->align;
                    },
                    [&](const CoreLayoutContainer &shape) {
                        if (layout.size != 8 || layout.align != 4 ||
                            !valid_id(table, shape.element) ||
                            (shape.value.has_value() && !valid_id(table, *shape.value))) {
                            return false;
                        }
                        // Recompute stride/value-offset/backing extent from the
                        // element (+map value) layouts and require stride*capacity.
                        const auto expected = layout_geometry::expected_container(table, shape);
                        return expected.has_value() &&
                               shape.value_offset == expected->value_offset &&
                               shape.stride == expected->stride &&
                               shape.backing_size == expected->backing_size;
                    },
                    [](const CoreLayoutUninhabited &) { return true; },
                },
                layout.shape);
            if (!ok_shape) {
                fail_local("frame-layout layout node is structurally invalid");
                return diags;
            }
        }

        // Placements: dense edge indices, container nodes, extents that match the
        // RE-DERIVED aligned backing (== stride*capacity), and pairwise-disjoint
        // fixed-page extents on the sum-of-prior-backing rule. A V2-D workflow
        // section carries input-collection backing placements (the host packer
        // needs them to place collection elements) but NO agent payload arena:
        // the entry payload arena is the separate workflow span, checked by
        // verify_workflow_spans below.
        constexpr std::uint64_t kBackingBase = kP6CollectionBackingBase;
        constexpr std::uint64_t kPageEnd = kCoreWasmFixedLinearMemoryCapacityBytes;
        const bool workflow_section_v2 =
            section.format_version >= 2 && !section.node_blocks.empty();
        if (workflow_section_v2) {
            if (section.payload_arena_base != 0 ||
                section.payload_arena_capacity != 0) {
                fail_local("a workflow frame-layout section cannot carry an agent payload arena");
                return diags;
            }
        }
        std::uint64_t expected_cursor = kBackingBase;
        // Each container layout may have at most ONE backing placement: the
        // packer/reader's backing_by_layout map is indexed by layout id, so a
        // duplicate would silently keep the last placement and shadow the
        // first (the JS oracle rejects this with Map.has). Reject at admission.
        std::vector<bool> placed_layouts(table.layouts.size(), false);
        for (std::uint32_t i = 0;
             i < section.placements.size(); ++i) {
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
            if (placed_layouts[placement.container_layout.value]) {
                fail_local("frame-layout backing placement names a container layout that "
                           "already has a placement");
                return diags;
            }
            placed_layouts[placement.container_layout.value] = true;
            const auto geometry = layout_geometry::expected_container(table, *container);
            if (!geometry.has_value() || geometry->stride == 0) {
                fail_local("frame-layout backing placement names a container with invalid backing "
                           "geometry");
                return diags;
            }
            const std::uint64_t aligned_extent =
                (geometry->backing_size + 7u) & ~std::uint64_t{7u};
            // extent must equal the aligned re-derived backing and cover the
            // full stride*capacity store, so a shrink cannot pass with capacity
            // left intact.
            if (placement.extent != aligned_extent || placement.extent == 0 ||
                placement.extent < geometry->backing_size) {
                fail_local("frame-layout backing placement extent does not cover its container's "
                           "stride * capacity backing");
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
        if (!workflow_section_v2) {
            const std::uint64_t expected_arena = (expected_cursor + 7u) & ~std::uint64_t{7u};
            if (section.payload_arena_base != expected_arena) {
                fail_local("frame-layout payload arena does not begin at the aligned backing "
                           "high-water");
                return diags;
            }
        }
        if (static_cast<std::uint64_t>(section.payload_arena_base) +
                section.payload_arena_capacity >
            kPageEnd) {
            fail_local("frame-layout payload arena exceeds the fixed page");
            return diags;
        }

        // V2-B: the rodata span names the fixed [256,1024) reservation. It is
        // either empty (no Data section) or an 8-aligned extent entirely inside
        // the region, beginning exactly at kP6RodataBase.
        if (section.format_version >= 2) {
            if (section.rodata_base != kP6RodataBase) {
                fail_local("frame-layout rodata span must begin at the fixed rodata base 256");
                return diags;
            }
            if (section.rodata_extent > kP6RodataCapacity) {
                fail_local("frame-layout rodata extent exceeds its fixed [256,1024) region");
                return diags;
            }
            if (!section.node_blocks.empty()) {
                // V2-D workflow module: the physical layout (node blocks, the
                // shared bridge page, the entry arena, result placements)
                // follows the workflow D6 cursor, not the agent backing/arena
                // cursor. Verify in-page bounds, alignment and pairwise
                // disjointness independently.
                if (!verify_workflow_spans(section, fail_local)) {
                    return diags;
                }
            } else if (!verify_bridge_spans(section, fail_local)) {
                return diags;
            }
        }
        return diags;
    }

    // Shared validation of the bridge control-frame shape and the dense
    // per-call-site RECORDS on the explicitly untrusted transport boundary.
    // The agent-lane (V2-C) and workflow (V2-D) sections carry identical dense
    // records, so a transported section of either kind gets the same
    // guarantees: a zero-site section carries an all-zero control span; with
    // sites the stride is a non-zero 8-aligned constant, the control extent is
    // exactly stride*sites + spill, call-site ids are dense from zero, arity
    // matches the param root count, the control block sits at its dense
    // fixed-stride offset, every param/result root names a table layout, and
    // the result placement extent is the re-derived aligned layout size. Only
    // the arena-cursor-relative PLACEMENT rules differ between the two lanes
    // (an agent chains its bridge page off the frame-payload arena and its
    // result placements off that page; a workflow follows the workflow D6
    // cursor) and are enforced by the callers.
    [[nodiscard]] static bool
    verify_bridge_records(const CoreFrameLayoutSection &section,
                          const std::function<void(std::string)> &fail_local) {
        constexpr std::uint64_t kPageEnd = kCoreWasmFixedLinearMemoryCapacityBytes;
        const auto align8 = [](std::uint64_t value) { return (value + 7u) & ~std::uint64_t{7u}; };
        const std::uint32_t site_count =
            static_cast<std::uint32_t>(section.bridge_call_sites.size());
        if (site_count == 0) {
            if (section.bridge_control_base != 0 || section.bridge_block_stride != 0 ||
                section.bridge_control_extent != 0 || section.bridge_spill_base != 0 ||
                section.bridge_spill_extent != 0) {
                fail_local("a frame-layout section without bridge call sites must carry a zero "
                           "bridge control-frame span");
                return false;
            }
            return true;
        }

        if (section.bridge_block_stride % 8u != 0 || section.bridge_block_stride < 8u) {
            fail_local("frame-layout bridge block stride must be a non-zero 8-aligned constant");
            return false;
        }
        const std::uint64_t blocks_extent =
            static_cast<std::uint64_t>(site_count) * section.bridge_block_stride;
        if (blocks_extent > kPageEnd) {
            fail_local("frame-layout bridge control blocks exceed the fixed page");
            return false;
        }
        // The control page frame is a non-zero 8-aligned fixed-page span on
        // BOTH lanes (it never starts at address 0, which the reserved frame
        // regions own).
        if (section.bridge_control_base == 0 || section.bridge_control_base % 8u != 0 ||
            section.bridge_control_base >= kPageEnd) {
            fail_local("frame-layout bridge control frame must begin at a non-zero 8-aligned "
                       "fixed-page address");
            return false;
        }
        const std::uint64_t expected_spill =
            section.bridge_control_base + blocks_extent;
        if (static_cast<std::uint64_t>(section.bridge_spill_base) != expected_spill) {
            fail_local("frame-layout bridge spill slots must immediately follow the control blocks");
            return false;
        }
        const std::uint64_t control_extent =
            blocks_extent + section.bridge_spill_extent;
        if (section.bridge_control_extent != control_extent ||
            section.bridge_control_base + section.bridge_control_extent > kPageEnd) {
            fail_local("frame-layout bridge control-frame extent is inconsistent or exceeds the "
                       "fixed page");
            return false;
        }

        std::uint64_t spill_cursor = 0;
        for (std::uint32_t i = 0; i < site_count; ++i) {
            const CoreFrameBridgeCallSite &site = section.bridge_call_sites[i];
            if (site.call_site_id != i) {
                fail_local("frame-layout bridge call-site ids must be dense starting at zero");
                return false;
            }
            if (site.arity != site.param_layouts.size()) {
                fail_local("frame-layout bridge call-site arity disagrees with its param roots");
                return false;
            }
            const std::uint64_t block_bytes = 8u + 8u * static_cast<std::uint64_t>(site.arity);
            if (block_bytes > section.bridge_block_stride) {
                fail_local("a frame-layout bridge control block exceeds its fixed stride");
                return false;
            }
            if (site.block_offset != i * section.bridge_block_stride) {
                fail_local("a frame-layout bridge control block is not at its dense fixed-stride "
                           "offset");
                return false;
            }
            if (section.format_version >= 3) {
                if (site.spill_extent % 8u != 0) {
                    fail_local("a frame-layout bridge spill window must be 8-aligned");
                    return false;
                }
                const std::uint64_t expected_site_spill =
                    static_cast<std::uint64_t>(section.bridge_spill_base) + spill_cursor;
                if (static_cast<std::uint64_t>(site.spill_base) != expected_site_spill) {
                    fail_local("a frame-layout bridge spill window does not densely follow the "
                               "prior sites' windows");
                    return false;
                }
                spill_cursor += site.spill_extent;
                if (spill_cursor > section.bridge_spill_extent) {
                    fail_local("a frame-layout bridge spill window runs past the module spill "
                               "region");
                    return false;
                }
            }
            for (const CoreLayoutId param : site.param_layouts) {
                if (!valid_id(section.table, param)) {
                    fail_local("frame-layout bridge call-site names an unknown param layout");
                    return false;
                }
            }
            if (!valid_id(section.table, site.result_layout)) {
                fail_local("frame-layout bridge call-site names an unknown result layout");
                return false;
            }
            const CoreLayout &result_layout =
                section.table.layouts[site.result_layout.value];
            const std::uint64_t aligned_result = align8(result_layout.size);
            if (aligned_result != site.result_extent || site.result_extent == 0) {
                fail_local("a frame-layout bridge result placement extent must equal its layout's "
                           "aligned size");
                return false;
            }
        }
        if (section.format_version >= 3 && spill_cursor != section.bridge_spill_extent) {
            fail_local("the per-site bridge spill windows do not exactly partition the module "
                       "spill region");
            return false;
        }
        return true;
    }

    // V2-D: validate a workflow module's frame spans. The agent placement/arena
    // cursor rules do not apply (a workflow has no agent input placements; the
    // entry frame is packed directly into an entry node's I block). Every named
    // span must be inside the fixed page, 8-aligned where it starts a region,
    // and the node blocks / bridge page / result placements must be pairwise
    // disjoint. The bridge call-site records get the SAME dense-record
    // validation the agent lane enforces.
    [[nodiscard]] static bool
    verify_workflow_spans(const CoreFrameLayoutSection &section,
                          const std::function<void(std::string)> &fail_local) {
        constexpr std::uint64_t kPageEnd = kCoreWasmFixedLinearMemoryCapacityBytes;
        const auto align8 = [](std::uint64_t value) { return (value + 7u) & ~std::uint64_t{7u}; };

        struct Span {
            std::uint64_t lo;
            std::uint64_t hi;
        };
        std::vector<Span> spans;
        auto add = [&](std::uint64_t base, std::uint64_t extent) -> bool {
            if (extent == 0 || base % 8u != 0 || base >= kPageEnd ||
                base + extent > kPageEnd) {
                fail_local("a workflow frame span is unaligned, empty, or outside the fixed page");
                return false;
            }
            spans.push_back({base, base + extent});
            return true;
        };

        if (section.node_blocks.empty()) {
            fail_local("a workflow frame section must name at least one node block");
            return false;
        }
        for (const CoreFrameLayoutSection::NodeBlock &block : section.node_blocks) {
            if (block.input_layout.value >= section.table.layouts.size() ||
                block.context_layout.value >= section.table.layouts.size() ||
                block.output_layout.value >= section.table.layouts.size()) {
                fail_local("a workflow node block names an out-of-range layout root");
                return false;
            }
            // Re-derive every block size from its named layout root: a crafted
            // section cannot name an 8-byte root and claim a multi-KiB span
            // (or vice versa). Input/output roots are always non-empty; the
            // context block is empty exactly when its layout is zero-sized
            // (Unit context).
            const std::uint64_t expected_input =
                align8(section.table.layouts[block.input_layout.value].size);
            const std::uint64_t expected_context =
                align8(section.table.layouts[block.context_layout.value].size);
            const std::uint64_t expected_output =
                align8(section.table.layouts[block.output_layout.value].size);
            if (expected_input == 0 || expected_output == 0) {
                fail_local("a workflow node input/output layout root must be non-empty");
                return false;
            }
            if (block.input_size != expected_input ||
                block.output_size != expected_output ||
                block.context_size != expected_context) {
                fail_local("a workflow node block size disagrees with the aligned size of its "
                           "named layout root");
                return false;
            }
            if (!add(block.input_base, block.input_size) ||
                !add(block.output_base, block.output_size)) {
                return false;
            }
            // The context block is present iff the context layout is non-empty.
            if (block.context_size != 0 && !add(block.context_base, block.context_size)) {
                return false;
            }
            // The scratch block carries no named layout root; it may be empty.
            if (block.scratch_size != 0 && !add(block.scratch_base, block.scratch_size)) {
                return false;
            }
        }
        if (!verify_bridge_records(section, fail_local)) {
            return false;
        }
        if (section.bridge_control_extent != 0 &&
            !add(section.bridge_control_base, section.bridge_control_extent)) {
            return false;
        }
        if (section.entry_payload_capacity != 0 &&
            !add(section.entry_payload_base, section.entry_payload_capacity)) {
            return false;
        }
        if (section.workflow_output_base != 0) {
            if (!valid_id(section.table, section.output_layout)) {
                fail_local("a workflow output slot names an out-of-range layout root");
                return false;
            }
            const std::uint64_t output_extent =
                align8(section.table.layouts[section.output_layout.value].size);
            if (output_extent == 0 || !add(section.workflow_output_base, output_extent)) {
                return false;
            }
        }
        // V2-D emission half 2: the state-entry trace ring (8-byte count
        // header plus the fixed record capacity) is a disjoint in-page span.
        if (section.state_trace_capacity != 0) {
            if (section.state_trace_base == 0 ||
                section.state_trace_base % 8u != 0 ||
                section.state_trace_capacity < 8u ||
                section.state_trace_capacity % 8u != 0 ||
                !add(section.state_trace_base, section.state_trace_capacity)) {
                fail_local("a workflow state-trace span is unaligned, undersized, or outside the "
                           "fixed page");
                return false;
            }
        }
        // WH-5b.3: the transcode shadow region, its String payload arena, and
        // the entry shadow are disjoint in-page spans whose presence is
        // REQUIRED by the site directions that use them, not merely declared:
        //   * every JSON_TO_P4D site needs the reused shadow landing area (at
        //     least the site's aligned layout size) and the payload arena (the
        //     planner emits one pool share per JSON_TO_P4D site);
        //   * every P4D_TO_JSON ENTRY site needs the host-packed entry shadow;
        //   * a P4D_TO_JSON NODE_OUTPUT site reads the producer's O_k and
        //     needs no host span.
        // A section whose sites need a span but carries a zeroed base/extent is
        // rejected here so a corrupt or foreign module can never make the host
        // pack a frame at guest address zero. A span that is present but not
        // required by any site is still checked for disjointness.
        bool needs_shadow = false;
        bool needs_payload = false;
        bool needs_entry_shadow = false;
        std::uint64_t needed_shadow_extent = 0;
        std::uint64_t needed_entry_shadow_extent = 0;
        for (const CoreFrameTranscodeSite &site : section.transcode_sites) {
            if (!valid_id(section.table, site.layout)) {
                fail_local("a transcode site names an out-of-range layout root");
                return false;
            }
            const std::uint64_t needed =
                align8(section.table.layouts[site.layout.value].size);
            if (site.direction == CoreFrameTranscodeSite::Direction::JsonToP4D) {
                needs_shadow = true;
                needs_payload = true;
                needed_shadow_extent = std::max(needed_shadow_extent, needed);
            } else if (site.source == CoreFrameTranscodeSite::Source::Entry) {
                needs_entry_shadow = true;
                needed_entry_shadow_extent =
                    std::max(needed_entry_shadow_extent, needed);
            }
            // A P4D_TO_JSON NodeOutput or CapabilityParam site reads the
            // producer's own O_k block and needs NO host span (CapabilityParam
            // is the construct-terminal self-transcode: its source is the
            // node's constructed O_k, identical in placement to NodeOutput).
        }
        auto check_transcode_span =
            [&](bool required, std::uint64_t base, std::uint64_t extent,
                std::uint64_t min_extent, const char *message,
                const char *present_message) -> bool {
            if (!required) {
                // Not required by any site: an absent span is fine, but a
                // present one must still be a nonzero, 8-aligned, disjoint
                // in-page region.
                if (extent == 0) {
                    return true;
                }
                if (base == 0 || base % 8u != 0) {
                    fail_local(present_message);
                    return false;
                }
                // add() emits the page-bounds/overlap diagnostic itself.
                if (!add(base, extent)) {
                    return false;
                }
                return true;
            }
            if (base == 0 || base % 8u != 0 || extent < min_extent) {
                fail_local(message);
                return false;
            }
            // add() emits the page-bounds/overlap diagnostic itself.
            if (!add(base, extent)) {
                return false;
            }
            return true;
        };
        if (!check_transcode_span(
                needs_shadow, section.transcode_shadow_base,
                section.transcode_shadow_extent, needed_shadow_extent,
                "a JSON_TO_P4D transcode site needs a nonzero, 8-aligned shadow "
                "region that fits its P4-D layout",
                "a transcode shadow span is present without a JSON_TO_P4D site "
                "and must be nonzero, 8-aligned, and disjoint inside the fixed "
                "page") ||
            !check_transcode_span(
                needs_payload, section.transcode_payload_base,
                section.transcode_payload_capacity, 1,
                "a JSON_TO_P4D transcode site needs a nonzero, 8-aligned "
                "payload arena",
                "a transcode payload arena is present without a JSON_TO_P4D "
                "site and must be nonzero, 8-aligned, and disjoint inside the "
                "fixed page") ||
            !check_transcode_span(
                needs_entry_shadow, section.transcode_entry_shadow_base,
                section.transcode_entry_shadow_extent,
                needed_entry_shadow_extent,
                "a P4D_TO_JSON ENTRY transcode site needs a nonzero, "
                "8-aligned entry shadow that fits its P4-D layout",
                "a transcode entry shadow is present without a P4D_TO_JSON "
                "ENTRY site and must be nonzero, 8-aligned, and disjoint inside "
                "the fixed page")) {
            return false;
        }
        for (const CoreFrameBridgeCallSite &site : section.bridge_call_sites) {
            if (!add(site.result_base, site.result_extent) ||
                (site.result_payload_capacity != 0 &&
                 !add(site.result_payload_base, site.result_payload_capacity))) {
                return false;
            }
        }
        std::sort(spans.begin(), spans.end(),
                  [](const Span &a, const Span &b) { return a.lo < b.lo; });
        for (std::size_t i = 1; i < spans.size(); ++i) {
            if (spans[i].lo < spans[i - 1].hi) {
                fail_local("two workflow frame spans overlap inside the fixed page");
                return false;
            }
        }
        return true;
    }

    // V2-C: validate the capability-bridge control-frame span and the dense
    // per-call-site result placements. The regions form ONE sum cursor chained
    // off the frame-payload arena: control blocks, the scalar/PtrLen spill
    // slots, then per call site the result root and its payload arena. Two sites
    // can therefore never alias (the e5ef55c sum-of-prior-backing class).
    [[nodiscard]] bool
    verify_bridge_spans(const CoreFrameLayoutSection &section,
                        const std::function<void(std::string)> &fail_local) {
        constexpr std::uint64_t kPageEnd = kCoreWasmFixedLinearMemoryCapacityBytes;
        const auto align8 = [](std::uint64_t value) { return (value + 7u) & ~std::uint64_t{7u}; };
        const std::uint32_t site_count =
            static_cast<std::uint32_t>(section.bridge_call_sites.size());
        // Dense-record validation is shared with the workflow lane (zero-site
        // invariant, stride/control-extent shape, dense ids, arity, block
        // offsets, param/result roots, result extents, v3 spill windows).
        if (!verify_bridge_records(section, fail_local)) {
            return false;
        }
        if (site_count == 0) {
            return true;
        }

        // Agent-lane-only placement rules: the control page frame begins
        // exactly at the aligned end of the frame-payload arena on the same
        // sum cursor the backing placements use, and every per-site result
        // root + payload arena chains off one sum cursor (so two sites can
        // never alias, the e5ef55c sum-of-prior-backing class).
        const std::uint64_t expected_control = align8(
            static_cast<std::uint64_t>(section.payload_arena_base) +
            section.payload_arena_capacity);
        if (section.bridge_control_base != expected_control) {
            fail_local("frame-layout bridge control frame must begin at the 8-aligned arena "
                       "high-water");
            return false;
        }

        std::uint64_t cursor = section.bridge_control_base + section.bridge_control_extent;
        for (std::uint32_t i = 0; i < site_count; ++i) {
            const CoreFrameBridgeCallSite &site = section.bridge_call_sites[i];
            if (site.result_base != cursor || site.result_base % 8u != 0) {
                fail_local("frame-layout bridge result placements must be disjoint and follow the "
                           "sum cursor");
                return false;
            }
            cursor += site.result_extent;
            if (site.result_payload_base != align8(cursor)) {
                fail_local("a frame-layout bridge result payload arena must immediately follow its "
                           "result placement");
                return false;
            }
            cursor = site.result_payload_base;
            if (static_cast<std::uint64_t>(site.result_payload_base) +
                        site.result_payload_capacity >
                    kPageEnd ||
                cursor + site.result_payload_capacity > kPageEnd) {
                fail_local("a frame-layout bridge result payload arena exceeds the fixed page");
                return false;
            }
            cursor += site.result_payload_capacity;
        }
        return true;
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
        if (!check_iterative(layout_root, wire_root)) {
            return std::vector<CoreLowerDiagnostic>{fail(message_)};
        }
        return {};
    }

  private:
    const CoreLayoutTable &layouts_;
    const CoreWireSchemaTable &wire_;
    std::string message_;

    using Pair = std::pair<CoreLayoutId, CoreWireSchemaNodeId>;

    // One DFS worklist entry. `exiting` closes the pair's path scope once its
    // children have all been checked, emulating the recursive enter/leave
    // without any C++ recursion (a deep acyclic graph is untrusted input).
    struct Frame {
        CoreLayoutId layout;
        CoreWireSchemaNodeId wire;
        bool exiting{false};
    };

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

    [[nodiscard]] static std::uint64_t pair_key(CoreLayoutId l, CoreWireSchemaNodeId w) noexcept {
        return (static_cast<std::uint64_t>(l.value) << 32U) |
               static_cast<std::uint64_t>(w.value);
    }

    // Local-shape verdict for one pair plus the child pairs that still have to
    // agree. This replaces the recursive `check_shapes`: every arm validates
    // ONLY its own node and enumerates children; the worklist drives descent.
    struct Analysis {
        bool valid{false};
        std::string message; // populated when a fixed reject reason applies
        std::vector<Pair> children;
    };

    [[nodiscard]] bool check_scalar_repr(CoreScalarRepr repr,
                                         const CoreWireSchemaInt &schema) {
        const bool narrow =
            schema.bounds.has_value() &&
            schema.bounds->first >= std::numeric_limits<std::int32_t>::min() &&
            schema.bounds->second <= std::numeric_limits<std::int32_t>::max();
        const CoreScalarRepr expected = narrow ? CoreScalarRepr::I32 : CoreScalarRepr::I64;
        return repr == expected;
    }

    [[nodiscard]] Analysis analyze(CoreLayoutId layout_id, CoreWireSchemaNodeId wire_id) {
        Analysis result;
        const CoreLayout &layout = layouts_.layouts[layout_id.value];
        const auto ok = [&](bool valid, std::string message = {}) {
            result.valid = valid;
            result.message = std::move(message);
            return result;
        };
        std::visit(
            Overloaded{
                [&](const CoreWireSchemaUnit &) {
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    ok(s != nullptr && layout.is_zero_sized && s->field_layouts.empty());
                },
                [&](const CoreWireSchemaBool &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && s->repr == CoreScalarRepr::I32);
                },
                [&](const CoreWireSchemaInt &schema) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && check_scalar_repr(s->repr, schema));
                },
                [&](const CoreWireSchemaFloat &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && s->repr == CoreScalarRepr::F64);
                },
                [&](const CoreWireSchemaString &) {
                    ok(std::holds_alternative<CoreLayoutPtrLen>(layout.shape));
                },
                [&](const CoreWireSchemaDecimal &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && s->repr == CoreScalarRepr::I64);
                },
                [&](const CoreWireSchemaDuration &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && s->repr == CoreScalarRepr::I64);
                },
                [&](const CoreWireSchemaTimestamp &) {
                    const auto *s = std::get_if<CoreLayoutScalar>(&layout.shape);
                    ok(s != nullptr && s->repr == CoreScalarRepr::I64);
                },
                [&](const CoreWireSchemaUuid &) {
                    const auto *s = std::get_if<CoreLayoutBytes>(&layout.shape);
                    ok(s != nullptr && s->byte_count == 16);
                },
                [&](const CoreWireSchemaOption &schema) {
                    const auto *e = std::get_if<CoreLayoutEnum>(&layout.shape);
                    if (e == nullptr || e->variant_payload_layouts.size() != 2) {
                        ok(false, "frame layout/wire disagree on Option enum shape");
                        return;
                    }
                    // AHFL's Option<T> defines Some(T) as variant 0 and None as
                    // variant 1; the wire schema's Option shape is
                    // order-agnostic. Find the None variant (zero payload) and
                    // the Some variant (one-slot payload) by their payload
                    // properties, not by a hard-coded index.
                    std::optional<std::uint32_t> none_idx;
                    std::optional<std::uint32_t> some_idx;
                    for (std::uint32_t i = 0; i < 2; ++i) {
                        if (e->variant_payload_sizes[i] == 0) {
                            none_idx = i;
                        } else {
                            some_idx = i;
                        }
                    }
                    if (!none_idx.has_value() || !some_idx.has_value()) {
                        ok(false, "frame layout/wire disagree on Option enum shape");
                        return;
                    }
                    const CoreLayoutId some_payload =
                        e->variant_payload_layouts[*some_idx];
                    if (!layout_valid(some_payload)) {
                        ok(false, "frame layout/wire Option payload edge is out of range");
                        return;
                    }
                    const auto *payload =
                        std::get_if<CoreLayoutStruct>(&layouts_.layouts[some_payload.value].shape);
                    if (payload == nullptr || payload->field_layouts.size() != 1) {
                        ok(false, "frame layout/wire Option::Some payload is not one slot");
                        return;
                    }
                    result.valid = true;
                    result.children.emplace_back(payload->field_layouts[0], schema.value);
                },
                [&](const CoreWireSchemaSequence &schema) {
                    const auto *c = std::get_if<CoreLayoutContainer>(&layout.shape);
                    if (c == nullptr || c->value.has_value()) {
                        ok(false, "frame layout/wire disagree on sequence container shape");
                        return;
                    }
                    const std::uint64_t expected_capacity =
                        schema.capacity.value_or(c->capacity);
                    if (c->capacity != expected_capacity) {
                        ok(false, "frame layout/wire sequence capacities disagree");
                        return;
                    }
                    result.valid = true;
                    result.children.emplace_back(c->element, schema.element);
                },
                [&](const CoreWireSchemaMap &schema) {
                    const auto *c = std::get_if<CoreLayoutContainer>(&layout.shape);
                    if (c == nullptr || !c->value.has_value()) {
                        ok(false, "frame layout/wire disagree on map container shape");
                        return;
                    }
                    const std::uint64_t expected_capacity =
                        schema.capacity.value_or(c->capacity);
                    if (c->capacity != expected_capacity) {
                        ok(false, "frame layout/wire map capacities disagree");
                        return;
                    }
                    if (!std::holds_alternative<CoreLayoutPtrLen>(
                            layouts_.layouts[c->element.value].shape)) {
                        ok(false, "frame layout/wire map key is not a String PtrLen");
                        return;
                    }
                    result.valid = true;
                    // Push value before key so key is popped first (left-to-right).
                    result.children.emplace_back(*c->value, schema.value);
                    result.children.emplace_back(c->element, schema.key);
                },
                [&](const CoreWireSchemaStruct &schema) {
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    if (s == nullptr || s->field_layouts.size() != schema.fields.size()) {
                        ok(false, "frame layout/wire struct field arity disagrees");
                        return;
                    }
                    result.valid = true;
                    result.children.reserve(schema.fields.size());
                    for (std::uint32_t i = 0; i < schema.fields.size(); ++i) {
                        result.children.emplace_back(s->field_layouts[i], schema.fields[i].type);
                    }
                },
                [&](const CoreWireSchemaEnum &schema) {
                    const auto *e = std::get_if<CoreLayoutEnum>(&layout.shape);
                    if (e == nullptr ||
                        e->variant_payload_layouts.size() != schema.variants.size()) {
                        ok(false, "frame layout/wire enum variant arity disagrees");
                        return;
                    }
                    for (std::uint32_t i = 0; i < schema.variants.size(); ++i) {
                        const CoreLayoutId payload_id = e->variant_payload_layouts[i];
                        if (!layout_valid(payload_id)) {
                            ok(false, "frame layout/wire enum payload edge is out of range");
                            return;
                        }
                        const auto *payload = std::get_if<CoreLayoutStruct>(
                            &layouts_.layouts[payload_id.value].shape);
                        const std::uint32_t slot_count =
                            static_cast<std::uint32_t>(schema.variants[i].slots.size());
                        if (payload == nullptr || payload->field_layouts.size() != slot_count) {
                            ok(false, "frame layout/wire enum payload slot arity disagrees");
                            return;
                        }
                        for (std::uint32_t slot = 0; slot < slot_count; ++slot) {
                            result.children.emplace_back(payload->field_layouts[slot],
                                                        schema.variants[i].slots[slot].type);
                        }
                    }
                    result.valid = true;
                },
                [&](const CoreWireSchemaTuple &schema) {
                    const auto *s = std::get_if<CoreLayoutStruct>(&layout.shape);
                    if (s == nullptr || s->field_layouts.size() != schema.elements.size()) {
                        ok(false, "frame layout/wire tuple arity disagrees");
                        return;
                    }
                    result.valid = true;
                    result.children.reserve(schema.elements.size());
                    for (std::uint32_t i = 0; i < schema.elements.size(); ++i) {
                        result.children.emplace_back(s->field_layouts[i], schema.elements[i]);
                    }
                },
            },
            wire_.nodes[wire_id.value].shape);
        return result;
    }

    // Iterative DFS: an explicit enter/exit worklist with a path set for
    // recursive-nominal cycle termination and a memo set for shared-subtree
    // diamonds. A decoder feeds this checker untrusted transported payloads, so
    // a legal but very deep acyclic Struct/Tuple chain must never recurse on the
    // C++ stack. Children are pushed in reverse so they pop left-to-right,
    // matching the former recursion's first-error order.
    [[nodiscard]] bool check_iterative(CoreLayoutId layout_root,
                                       CoreWireSchemaNodeId wire_root) {
        if (!layout_valid(layout_root) || !wire_id_valid(wire_root)) {
            return reject("frame layout/wire root id is out of range");
        }
        std::vector<Frame> stack;
        stack.push_back(Frame{layout_root, wire_root, false});
        std::unordered_set<std::uint64_t> on_path;
        std::unordered_set<std::uint64_t> accepted;
        while (!stack.empty()) {
            const Frame frame = stack.back();
            stack.pop_back();
            if (frame.exiting) {
                on_path.erase(pair_key(frame.layout, frame.wire));
                accepted.insert(pair_key(frame.layout, frame.wire));
                continue;
            }
            if (!layout_valid(frame.layout) || !wire_id_valid(frame.wire)) {
                return reject("frame layout/wire child id is out of range");
            }
            const std::uint64_t key = pair_key(frame.layout, frame.wire);
            if (on_path.find(key) != on_path.end()) {
                continue; // back edge over a recursive nominal pair
            }
            if (accepted.find(key) != accepted.end()) {
                continue; // shared subtree already fully verified
            }
            Analysis analysis = analyze(frame.layout, frame.wire);
            if (!analysis.valid) {
                return reject(analysis.message.empty()
                                  ? "frame layout and wire schema disagree at a node"
                                  : std::move(analysis.message));
            }
            on_path.insert(key);
            stack.push_back(Frame{frame.layout, frame.wire, true});
            for (auto it = analysis.children.rbegin(); it != analysis.children.rend(); ++it) {
                stack.push_back(Frame{it->first, it->second, false});
            }
        }
        return true;
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

std::vector<CoreLowerDiagnostic>
verify_frame_bridge_sites(const CoreFrameLayoutSection &section,
                          const CoreWireSchemaTable &wire) {
    if (section.bridge_call_sites.empty()) {
        return {};
    }
    // Frame-bridge v2 D4 rung V2-C: every capability argument/result root must
    // lie inside the frame-WALK wire subset — Unit/Bool/Int/String, struct,
    // enum, option, tuple, bounded sequence. map / f64 / decimal / duration /
    // timestamp / uuid stay fail-closed HERE (compile + admission), so the host
    // walkers never meet a shape they cannot pack/read across the bridge. The
    // predicate is node-local (no path dependence), so a flat visited set is a
    // complete cycle-safe traversal of the hash-consed graph.
    const auto within_frame_walk_subset = [&](CoreWireSchemaNodeId root) {
        std::vector<std::uint32_t> worklist{root.value};
        std::vector<std::uint8_t> visited(wire.nodes.size(), 0);
        while (!worklist.empty()) {
            const std::uint32_t id = worklist.back();
            worklist.pop_back();
            if (id >= wire.nodes.size() || visited[id] != 0) {
                if (id >= wire.nodes.size()) {
                    return false;
                }
                continue;
            }
            visited[id] = 1;
            const bool rejected = std::visit(
                Overloaded{
                    [](const CoreWireSchemaFloat &) { return true; },
                    [](const CoreWireSchemaDecimal &) { return true; },
                    [](const CoreWireSchemaDuration &) { return true; },
                    [](const CoreWireSchemaTimestamp &) { return true; },
                    [](const CoreWireSchemaUuid &) { return true; },
                    [](const CoreWireSchemaMap &) { return true; },
                    [](const CoreWireSchemaUnit &) { return false; },
                    [](const CoreWireSchemaBool &) { return false; },
                    [](const CoreWireSchemaInt &) { return false; },
                    [](const CoreWireSchemaString &) { return false; },
                    [&](const CoreWireSchemaOption &s) {
                        worklist.push_back(s.value.value);
                        return false;
                    },
                    [&](const CoreWireSchemaSequence &s) {
                        worklist.push_back(s.element.value);
                        return false;
                    },
                    [&](const CoreWireSchemaTuple &s) {
                        for (const CoreWireSchemaNodeId element : s.elements) {
                            worklist.push_back(element.value);
                        }
                        return false;
                    },
                    [&](const CoreWireSchemaStruct &s) {
                        for (const CoreWireSchemaField &field : s.fields) {
                            worklist.push_back(field.type.value);
                        }
                        return false;
                    },
                    [&](const CoreWireSchemaEnum &s) {
                        for (const CoreWireSchemaVariant &variant : s.variants) {
                            for (const CoreWireSchemaField &slot : variant.slots) {
                                worklist.push_back(slot.type.value);
                            }
                        }
                        return false;
                    },
                },
                wire.nodes[id].shape);
            if (rejected) {
                return false;
            }
        }
        return true;
    };

    auto fail_local = [](std::string message) {
        return std::vector<CoreLowerDiagnostic>{fail(std::move(message))};
    };
    for (const CoreFrameBridgeCallSite &site : section.bridge_call_sites) {
        const auto capability_it =
            std::find_if(wire.capabilities.begin(), wire.capabilities.end(),
                         [&](const CoreWireCapabilitySchema &schema) {
                             return schema.source_symbol == site.source_symbol;
                         });
        if (capability_it == wire.capabilities.end()) {
            return fail_local("a frame-layout bridge call site names a capability the wire-schema "
                              "table does not project");
        }
        if (capability_it->params.size() != site.arity ||
            site.param_layouts.size() != site.arity) {
            return fail_local("a frame-layout bridge call site arity disagrees with the wire-schema "
                              "capability parameter count");
        }
        for (std::uint32_t i = 0; i < site.arity; ++i) {
            if (!within_frame_walk_subset(capability_it->params[i])) {
                return fail_local(
                    "a frame-layout bridge argument is outside the frame-walk wire subset "
                    "(map, f64, decimal, duration, timestamp, and uuid shapes cannot cross the "
                    "capability bridge)");
            }
            auto diags = verify_frame_layout_wire_consistency(
                section.table, site.param_layouts[i], wire, capability_it->params[i]);
            if (!diags.empty()) {
                return diags;
            }
        }
        if (!within_frame_walk_subset(capability_it->result)) {
            return fail_local(
                "a frame-layout bridge result is outside the frame-walk wire subset "
                "(map, f64, decimal, duration, timestamp, and uuid shapes cannot cross the "
                "capability bridge)");
        }
        auto diags = verify_frame_layout_wire_consistency(
            section.table, site.result_layout, wire, capability_it->result);
        if (!diags.empty()) {
            return diags;
        }
    }
    return {};
}

} // namespace ahfl::ir::core
