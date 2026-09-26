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
