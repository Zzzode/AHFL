#include <doctest.h>

#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// RFC 0026 P6-7 rung A: deterministic `ahfl.core-layout.v1` codec + local
// verifier + layout/wire consistency admission. These tests build known-good
// P4-D layout tables / frame-layout sections and prove:
//   * encode/decode round-trip equality and canonical re-encode acceptance;
//   * truncation / tamper / non-canonical payload rejection;
//   * the disjoint-backing-placement verifier (overlapping / non-dense /
//     wrong-extent placements all fail);
//   * layout/wire structural disagreement rejection and agreement acceptance.

namespace {

using namespace ahfl::ir::core;

[[nodiscard]] CoreLayout scalar_layout(CoreScalarRepr repr) {
    const std::uint64_t size = repr == CoreScalarRepr::I32 ? 4 : 8;
    const std::uint32_t align = repr == CoreScalarRepr::I32 ? 4 : 8;
    return CoreLayout{size, align, false, CoreLayoutScalar{repr}};
}

// Table: node 0 = i64 scalar; both boundary roots point at it; no placements;
// the payload arena begins exactly at the backing base (sum of zero extents).
[[nodiscard]] CoreFrameLayoutSection scalar_section() {
    CoreFrameLayoutSection section;
    section.table.target = TargetDataLayout{};
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I64));
    section.table.value_layouts.push_back(CoreLayoutId{0});
    section.input_layout = CoreLayoutId{0};
    section.output_layout = CoreLayoutId{0};
    section.payload_arena_base = kP6CollectionBackingBase;
    section.payload_arena_capacity = 0;
    return section;
}

// Table: node 0 = i64 element; node 1 = bounded container (capacity 4, stride
// 8, backing 32); roots at the container. Placement 0 occupies
// [16384, 16416); the arena begins at 16416.
[[nodiscard]] CoreFrameLayoutSection container_section() {
    CoreFrameLayoutSection section;
    section.table.target = TargetDataLayout{};
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I64)); // 0 element
    CoreLayout container;
    container.size = 8;
    container.align = 4;
    container.is_zero_sized = false;
    container.shape =
        CoreLayoutContainer{CoreLayoutId{0}, std::nullopt, 4, 8, 0, 32};
    section.table.layouts.push_back(container); // 1 container
    section.table.value_layouts.push_back(CoreLayoutId{1});
    section.input_layout = CoreLayoutId{1};
    section.output_layout = CoreLayoutId{1};
    section.placements.push_back(
        CoreFrameBackingPlacement{0, CoreLayoutId{1}, kP6CollectionBackingBase, 32});
    section.payload_arena_base = kP6CollectionBackingBase + 32;
    section.payload_arena_capacity = 0;
    return section;
}

} // namespace

TEST_CASE("frame-layout codec round-trips a scalar-root section") {
    const auto section = scalar_section();
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    REQUIRE(encoded.bytes.has_value());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
}

TEST_CASE("frame-layout codec round-trips a container-root section with placement") {
    const auto section = container_section();
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
}

TEST_CASE("frame-layout decoder rejects a truncated payload") {
    auto encoded = encode_core_frame_layout_section(scalar_section());
    REQUIRE(encoded.ok());
    for (std::size_t cut = 0; cut + 1 < encoded.bytes->size(); ++cut) {
        auto truncated = std::span<const std::uint8_t>(encoded.bytes->data(), cut);
        auto decoded = decode_core_frame_layout_section(truncated);
        CHECK_FALSE(decoded.ok());
    }
}

TEST_CASE("frame-layout decoder rejects a tampered byte") {
    const auto section = scalar_section();
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    // Flip the first magic byte: the header gate must reject it.
    (*encoded.bytes)[0] ^= 0xff;
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
    // Overlong-LEB the trailing arena-capacity zero: it decodes to the same
    // value but is non-canonical, so the re-encode equality gate rejects it.
    auto overlong = encode_core_frame_layout_section(section);
    REQUIRE(overlong.ok());
    overlong.bytes->back() = 0x80;
    overlong.bytes->push_back(0x00);
    CHECK_FALSE(decode_core_frame_layout_section(*overlong.bytes).ok());
}

TEST_CASE("frame-layout decoder rejects trailing bytes") {
    auto encoded = encode_core_frame_layout_section(scalar_section());
    encoded.bytes->push_back(0x00);
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects overlapping backing placements") {
    auto section = container_section();
    // Duplicate the placement but keep the SAME base (aliased onto placement 0):
    // the dense/order and sum-of-prior rules must reject it.
    section.placements.push_back(
        CoreFrameBackingPlacement{1, CoreLayoutId{1}, kP6CollectionBackingBase, 32});
    section.payload_arena_base = kP6CollectionBackingBase + 32;
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a placement extent mismatch") {
    auto section = container_section();
    section.placements[0].extent = 16; // layout backing aligned size is 32
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a payload arena off the high-water") {
    auto section = container_section();
    section.payload_arena_base = kP6CollectionBackingBase; // should be 16416
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame layout/wire consistency agrees on matching scalar widths") {
    const auto section = scalar_section();
    CoreWireSchemaTable wire;
    wire.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // wide Int -> i64
    const auto diags = verify_frame_layout_wire_consistency(
        section.table, section.input_layout, wire, CoreWireSchemaNodeId{0});
    CHECK(diags.empty());
}

TEST_CASE("frame layout/wire consistency rejects a scalar-width disagreement") {
    const auto section = scalar_section(); // i64 layout
    CoreWireSchemaTable wire;
    // Narrow bounds imply i32, so the i64 layout disagrees.
    wire.nodes.push_back(CoreWireSchemaNode{
        CoreWireSchemaInt{std::pair<std::int64_t, std::int64_t>{0, 10}}});
    const auto diags = verify_frame_layout_wire_consistency(
        section.table, section.input_layout, wire, CoreWireSchemaNodeId{0});
    CHECK_FALSE(diags.empty());
}

TEST_CASE("frame layout/wire consistency rejects struct arity disagreement") {
    CoreFrameLayoutSection section;
    section.table.target = TargetDataLayout{};
    // One-field struct at node 0.
    CoreLayout one_field;
    one_field.size = 8;
    one_field.align = 8;
    one_field.shape =
        CoreLayoutStruct{std::vector<std::uint64_t>{0}, std::vector<CoreLayoutId>{CoreLayoutId{1}}};
    section.table.layouts.push_back(one_field); // 0 struct
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I64)); // 1 i64
    section.table.value_layouts.push_back(CoreLayoutId{0});
    section.input_layout = section.output_layout = CoreLayoutId{0};
    section.payload_arena_base = kP6CollectionBackingBase;

    CoreWireSchemaTable wire;
    wire.nodes.push_back(CoreWireSchemaNode{CoreWireSchemaInt{}}); // 0 i64
    // A TWO-field wire struct over one field layout node id 0.
    CoreWireSchemaStruct st;
    st.wire_name = "Frame";
    st.fields.push_back(CoreWireSchemaField{"a", CoreWireSchemaNodeId{0}});
    st.fields.push_back(CoreWireSchemaField{"b", CoreWireSchemaNodeId{0}});
    wire.nodes.push_back(CoreWireSchemaNode{st}); // 1 struct
    const auto diags = verify_frame_layout_wire_consistency(
        section.table, CoreLayoutId{0}, wire, CoreWireSchemaNodeId{1});
    CHECK_FALSE(diags.empty());
}

// RFC 0026 P6-7 frame-bridge v2 D5/D6 (rung V2-D): a workflow frame-layout
// section is a format-version-2 payload whose trailing presence-gated block
// carries the per-packaged-instance node-frame blocks (I/C/scratch/O), the
// host-packed entry payload arena span and the workflow output slot.
[[nodiscard]] CoreFrameLayoutSection workflow_node_section() {
    CoreFrameLayoutSection section;
    section.format_version = 2;
    section.rodata_base = kP6RodataBase;
    section.table.target = TargetDataLayout{};
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I64)); // 0
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I32)); // 1
    section.table.value_layouts.push_back(CoreLayoutId{0});
    // The agent input boundary root is kInvalid on a workflow table (the wire
    // schema carries the per-node boundary arrays instead); the output root
    // names the workflow output layout.
    section.input_layout = CoreLayoutId{CoreLayoutId::kInvalid};
    section.output_layout = CoreLayoutId{0};
    CoreFrameLayoutSection::NodeBlock block;
    block.input_layout = CoreLayoutId{0};
    block.context_layout = CoreLayoutId{0};
    block.output_layout = CoreLayoutId{0};
    block.input_size = 8;
    block.context_size = 8;
    block.output_size = 8;
    block.input_base = 8192;
    block.context_base = 8200;
    block.scratch_base = 8208;
    block.scratch_size = 16;
    block.output_base = 8224;
    section.node_blocks.push_back(block);
    section.entry_payload_base = 8240;
    section.entry_payload_capacity = kP6FrameStringPoolBytes;
    section.workflow_output_base = 8240 + kP6FrameStringPoolBytes;
    return section;
}

TEST_CASE("frame-layout codec round-trips a V2-D workflow node-block section") {
    const auto section = workflow_node_section();
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
    CHECK(decoded.section->format_version == 2);
    CHECK(decoded.section->node_blocks.size() == 1);
}

TEST_CASE("frame-layout verifier rejects overlapping workflow node blocks") {
    auto section = workflow_node_section();
    // Overlap the output block onto the scratch span.
    section.node_blocks[0].output_base = section.node_blocks[0].scratch_base;
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

// V2-D fix-forward: a workflow section carrying a capability bridge must get
// the SAME dense call-site record validation the agent-lane (V2-C) section
// gets, plus node-block sizes re-derived from the named layout roots. This
// fixture packs one node block (I/C/scratch/O) + one one-arity bridge site +
// the entry arena + the workflow output slot into pairwise-disjoint spans.
//   node block : I@8192/8 C@8200/8 scratch@8208/16 O@8224/8 -> ends 8232
//   bridge page: control@8232 stride16 one block (spill zero) -> ends 8248
//   result     : @8248/8 -> ends 8256
//   entry arena: @8256/2048 -> ends 10304
//   output slot: @10304/8
[[nodiscard]] CoreFrameLayoutSection workflow_bridge_section() {
    CoreFrameLayoutSection section;
    section.format_version = 2;
    section.rodata_base = kP6RodataBase;
    section.table.target = TargetDataLayout{};
    section.table.layouts.push_back(scalar_layout(CoreScalarRepr::I64)); // 0
    // A zero-sized empty struct is the Unit context's finalized layout.
    CoreLayout unit;
    unit.size = 0;
    unit.align = 1;
    unit.is_zero_sized = true;
    unit.shape = CoreLayoutStruct{{}, {}};
    section.table.layouts.push_back(unit); // 1
    section.table.value_layouts.push_back(CoreLayoutId{0});
    section.input_layout = CoreLayoutId{CoreLayoutId::kInvalid};
    section.output_layout = CoreLayoutId{0};

    CoreFrameLayoutSection::NodeBlock block;
    block.input_layout = CoreLayoutId{0};
    block.context_layout = CoreLayoutId{0};
    block.output_layout = CoreLayoutId{0};
    block.input_size = 8;
    block.context_size = 8;
    block.output_size = 8;
    block.input_base = 8192;
    block.context_base = 8200;
    block.scratch_base = 8208;
    block.scratch_size = 16;
    block.output_base = 8224;
    section.node_blocks.push_back(block);

    section.bridge_control_base = 8232;
    section.bridge_block_stride = 16;
    section.bridge_control_extent = 16;
    section.bridge_spill_base = 8248;
    section.bridge_spill_extent = 0;

    CoreFrameBridgeCallSite site;
    site.call_site_id = 0;
    site.source_symbol = 1;
    site.arity = 1;
    site.block_offset = 0;
    site.param_layouts = {CoreLayoutId{0}};
    site.result_layout = CoreLayoutId{0};
    site.result_base = 8248;
    site.result_extent = 8;
    site.result_payload_base = 0;
    site.result_payload_capacity = 0;
    section.bridge_call_sites.push_back(site);

    section.entry_payload_base = 8256;
    section.entry_payload_capacity = kP6FrameStringPoolBytes;
    section.workflow_output_base = 8256 + kP6FrameStringPoolBytes;
    return section;
}

TEST_CASE("frame-layout codec round-trips a V2-D workflow bridge section") {
    const auto section = workflow_bridge_section();
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
    CHECK(decoded.section->bridge_call_sites.size() == 1);
}

TEST_CASE("frame-layout verifier accepts a V2-D node block with a zero-sized "
          "Unit context") {
    // A packaged agent declares context: Unit, whose finalized layout is
    // zero-sized: the C sub-block is absent (context_size == 0) and the
    // scratch block starts exactly where I ends.
    auto section = workflow_bridge_section();
    auto &block = section.node_blocks[0];
    block.context_layout = CoreLayoutId{1}; // zero-sized Unit
    block.context_size = 0;
    block.context_base = 0; // absent block carries no span base
    block.scratch_base = block.input_base + block.input_size; // 8200
    block.output_base = block.scratch_base + block.scratch_size; // 8216
    // Shift the bridge page / result / arena / output down with the block.
    const std::uint32_t page = block.output_base + block.output_size; // 8224
    section.bridge_control_base = page;
    section.bridge_spill_base = page + section.bridge_block_stride;
    section.bridge_call_sites[0].result_base = section.bridge_spill_base;
    section.entry_payload_base = section.bridge_call_sites[0].result_base +
                                 section.bridge_call_sites[0].result_extent;
    section.workflow_output_base = section.entry_payload_base +
                                   section.entry_payload_capacity;
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow node block size that lies "
          "about its layout root") {
    auto section = workflow_bridge_section();
    section.node_blocks[0].input_size = 4096; // root is an 8-byte i64
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow node block naming an "
          "unknown layout root") {
    auto section = workflow_bridge_section();
    section.node_blocks[0].output_layout = CoreLayoutId{99};
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a non-dense workflow bridge call-site id") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites[0].call_site_id = 1;
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow bridge arity that "
          "disagrees with its param roots") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites[0].arity = 2; // only one param root
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow bridge block off its "
          "dense fixed-stride offset") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites[0].block_offset = 16; // must be 0 for site 0
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow bridge result naming an "
          "unknown layout") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites[0].result_layout = CoreLayoutId{99};
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a workflow bridge result extent "
          "that disagrees with its layout size") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites[0].result_extent = 16; // i64 root aligns to 8
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects an inconsistent workflow bridge "
          "control extent") {
    auto section = workflow_bridge_section();
    section.bridge_control_extent = 24; // stride*sites + spill == 16
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects non-zero bridge control fields on a "
          "zero-site workflow section") {
    auto section = workflow_bridge_section();
    section.bridge_call_sites.clear();
    section.bridge_control_extent = 0;
    section.bridge_block_stride = 16; // must be zero when there are no sites
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

// WH-5b.3 fix-forward: the transcode extension's direction=>span invariant.
// A P4D_TO_JSON NODE_OUTPUT site needs NO host span (it reads the producer's
// O_k), so a section carrying one with six zero spans must encode, decode,
// and verify. A JSON_TO_P4D site REQUIRES the shadow region + payload arena;
// a P4D_TO_JSON ENTRY site REQUIRES the entry shadow. Zeroed spans on a
// direction that needs them are rejected.

[[nodiscard]] CoreFrameTranscodeSite
transcode_site(CoreFrameTranscodeSite::Direction direction,
               CoreFrameTranscodeSite::Source source,
               std::uint32_t source_node, std::uint32_t target_node,
               CoreLayoutId layout) {
    CoreFrameTranscodeSite site;
    site.import_ordinal = 0;
    site.direction = direction;
    site.source = source;
    site.source_node_ordinal = source_node;
    site.target_node_ordinal = target_node;
    site.layout = layout;
    return site;
}

TEST_CASE("frame-layout codec round-trips a P4D_TO_JSON-only transcode section "
          "with six zero spans") {
    // The p6_to_opaque shape: one P4D_TO_JSON NODE_OUTPUT site, all six
    // transcode spans zero by planner design. The decoder presence checks
    // test byte presence (optional::has_value), not the contained value, so
    // the zero spans decode; the verifier requires no host span for a
    // NODE_OUTPUT site. Regression pin for the adjudicated P1.
    auto section = workflow_node_section();
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::P4DToJson,
        CoreFrameTranscodeSite::Source::NodeOutput, 0, 1, CoreLayoutId{0}));
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
    CHECK(decoded.section->transcode_sites.size() == 1);
    CHECK(decoded.section->transcode_shadow_base == 0);
    CHECK(decoded.section->transcode_shadow_extent == 0);
    CHECK(decoded.section->transcode_payload_base == 0);
    CHECK(decoded.section->transcode_payload_capacity == 0);
    CHECK(decoded.section->transcode_entry_shadow_base == 0);
    CHECK(decoded.section->transcode_entry_shadow_extent == 0);
}

TEST_CASE("frame-layout verifier rejects a JSON_TO_P4D site with zeroed shadow "
          "and payload spans") {
    // A crafted section carrying a JSON_TO_P4D site but zeroed shadow/payload
    // spans must be rejected: the host would otherwise pack the frame at
    // guest address zero. The compiler never emits this shape.
    auto section = workflow_node_section();
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::JsonToP4D,
        CoreFrameTranscodeSite::Source::Entry, 0, 1, CoreLayoutId{0}));
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier rejects a P4D_TO_JSON ENTRY site with a zeroed "
          "entry shadow") {
    // A P4D_TO_JSON ENTRY site needs the host-packed entry shadow; a zeroed
    // entry shadow base/extent must be rejected.
    auto section = workflow_node_section();
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::P4DToJson,
        CoreFrameTranscodeSite::Source::Entry, 0, 1, CoreLayoutId{0}));
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    CHECK_FALSE(decode_core_frame_layout_section(*encoded.bytes).ok());
}

TEST_CASE("frame-layout verifier accepts a direction-consistent mixed "
          "transcode section") {
    // Both directions with planner-consistent spans: a JSON_TO_P4D site gets
    // the shadow region (>= the site's aligned layout size) + one pool-share
    // payload arena; a P4D_TO_JSON ENTRY site gets the entry shadow. All
    // spans are disjoint in-page regions chained after the workflow output
    // slot (which ends at 8240 + 2048 + 8 = 10296).
    auto section = workflow_node_section();
    constexpr std::uint32_t kShadowBase =
        8240 + kP6FrameStringPoolBytes + 8; // 10296
    section.transcode_shadow_base = kShadowBase;
    section.transcode_shadow_extent = 8; // align8(i64 layout root)
    section.transcode_payload_base = kShadowBase + 8; // 10304
    section.transcode_payload_capacity = kP6FrameStringPoolBytes;
    section.transcode_entry_shadow_base =
        kShadowBase + 8 + kP6FrameStringPoolBytes; // 12352
    section.transcode_entry_shadow_extent = 8;
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::JsonToP4D,
        CoreFrameTranscodeSite::Source::Entry, 0, 1, CoreLayoutId{0}));
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::P4DToJson,
        CoreFrameTranscodeSite::Source::Entry, 0, 1, CoreLayoutId{0}));
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto decoded = decode_core_frame_layout_section(*encoded.bytes);
    REQUIRE(decoded.ok());
    REQUIRE(decoded.section.has_value());
    CHECK(*decoded.section == section);
}

TEST_CASE("frame-layout verifier rejects a present-but-unneeded transcode "
          "span at guest address zero") {
    // A P4D_TO_JSON NODE_OUTPUT site needs no host span. Declaring a shadow
    // region anyway at base 0 must still be rejected: a present span is a
    // real in-page region and can never live at the reserved zero page, even
    // when no site direction requires it. Regression for the !required
    // branch accepting base == 0.
    auto section = workflow_node_section();
    section.transcode_shadow_base = 0;
    section.transcode_shadow_extent = 8;
    section.transcode_sites.push_back(transcode_site(
        CoreFrameTranscodeSite::Direction::P4DToJson,
        CoreFrameTranscodeSite::Source::NodeOutput, 0, 1, CoreLayoutId{0}));
    auto encoded = encode_core_frame_layout_section(section);
    REQUIRE(encoded.ok());
    auto probed = decode_core_frame_layout_section(*encoded.bytes);
    CHECK_FALSE(probed.ok());
}
