#pragma once

// RFC 0026 P6-7 rung A: the deterministic `ahfl.core-layout.v1` custom section.
//
// A P6-frame Core-Wasm agent (one that projects the raw P4-D input frame or
// returns a computed final value) carries TWO verified custom sections at EOF:
// the existing `ahfl.wire-schema.v1` table (extended with the agent boundary
// roots) and, immediately before it, this core-layout section. The section is
// the host's physical-layout authority: it carries the finalized P4-D
// `CoreLayoutTable`, the input/output boundary root layout ids, every
// input-reached bounded container's DISJOINT backing placement (the section 6.2
// sum-of-prior-backing rule), and the frame-payload arena span. A host never
// re-derives an offset, stride, capacity, or backing extent -- doing so would
// fork the deterministic P4-D algorithm into a second SSOT.
//
// Encoding is deterministic ULEB128 (mirror of the wire-schema codec); the
// decoder canonical-re-encodes and byte-compares, so a truncated, overlong,
// non-canonical, or tampered payload is rejected before any table is returned.
// Diagnostics are fixed, schema-only strings: page bytes, decoded names, and
// addresses are NEVER echoed.

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"

namespace ahfl::ir::core {

inline constexpr std::string_view kCoreLayoutSectionName = "ahfl.core-layout.v1";

/// One input-reached bounded container's disjoint backing placement. The edge
/// index is the 0-based position at which the container was enumerated during
/// the planner's boundary DFS (depth-first, declaration order); `base` is its
/// absolute linear-memory address and `extent` its aligned `backing_size`. Two
/// placements may never overlap -- the local verifier rejects any section whose
/// extents intersect.
struct CoreFrameBackingPlacement {
    std::uint32_t edge_index{0};
    CoreLayoutId container_layout{};
    std::uint32_t base{0};
    std::uint32_t extent{0};

    [[nodiscard]] friend bool operator==(const CoreFrameBackingPlacement &,
                                         const CoreFrameBackingPlacement &) noexcept = default;
};

/// One per-call-site CAPABILITY BRIDGE result placement (frame-bridge v2 D3/D4,
/// rung V2-C). The module builds the call's control block at
/// `bridge_control_base + block_offset`; the host walks the P4-D argument spans
/// into wire JSON, invokes the capability, validates the result, and packs it
/// at `result_base` with String payload bytes in the disjoint
/// `[result_payload_base, +result_payload_capacity)` arena. Every field is a
/// compile-time constant and the placements are pairwise disjoint (the
/// sum-of-prior-backing family): two call sites can never alias, regardless of
/// the acyclic handler walk that reaches them.
struct CoreFrameBridgeCallSite {
    /// Dense call-site id (ANF order, module-unique); also the block's word 0.
    std::uint32_t call_site_id{0};
    /// The called capability's resolved source SymbolId; admission cross-checks
    /// it against the wire-schema capability record (index identity never
    /// trusted from this section alone).
    std::uint64_t source_symbol{0};
    /// Must equal the capability's declared parameter count and the number of
    /// dense param layout roots.
    std::uint32_t arity{0};
    /// This call's control-block offset relative to `bridge_control_base`; the
    /// host checks `block_offset == call_site_id * bridge_block_stride`.
    std::uint32_t block_offset{0};
    /// Dense P4-D layout roots for the arguments, in declaration order.
    std::vector<CoreLayoutId> param_layouts;
    /// Dense P4-D layout root the host packs the capability result at.
    CoreLayoutId result_layout{};
    std::uint32_t result_base{0};
    std::uint32_t result_extent{0};
    std::uint32_t result_payload_base{0};
    std::uint32_t result_payload_capacity{0};

    [[nodiscard]] friend bool operator==(const CoreFrameBridgeCallSite &,
                                         const CoreFrameBridgeCallSite &) noexcept = default;
};

/// The decoded `ahfl.core-layout.v1` payload.
struct CoreFrameLayoutSection {
    std::uint32_t format_version{1};
    CoreLayoutTable table;
    /// Boundary roots into `table.layouts`.
    CoreLayoutId input_layout{};
    CoreLayoutId output_layout{};
    /// Every input-reached bounded container, in boundary-enumeration order;
    /// pairwise disjoint.
    std::vector<CoreFrameBackingPlacement> placements;
    /// Frame-payload arena span (packed input String bytes), [base, +capacity).
    std::uint32_t payload_arena_base{0};
    std::uint32_t payload_arena_capacity{0};
    /// V2 (frame-bridge v2 D1, rung V2-B): read-only String literal pool span.
    /// Base is the constant kP6RodataBase (256); extent is the active Data(11)
    /// segment's byte length (0 when the module emits no Data section).
    std::uint32_t rodata_base{0};
    std::uint32_t rodata_extent{0};

    /// V2 (frame-bridge v2 D3/D4, rung V2-C): the per-module capability bridge
    /// control-block page frame. It holds the dense call-site control blocks
    /// (`[bridge_control_base, +call_site_count * bridge_block_stride)`)
    /// followed by the private scalar/PtrLen spill slots
    /// (`[bridge_spill_base, +bridge_spill_extent)`). Every span is zero on a
    /// module with no bridge calls.
    std::uint32_t bridge_control_base{0};
    std::uint32_t bridge_block_stride{0};
    std::uint32_t bridge_control_extent{0};
    std::uint32_t bridge_spill_base{0};
    std::uint32_t bridge_spill_extent{0};
    /// Per-call-site blocks and disjoint result placements, in dense
    /// call_site_id order (empty for a non-bridge module).
    std::vector<CoreFrameBridgeCallSite> bridge_call_sites;

    // ------------------------------------------------------------------
    // V2-D workflow node-packaging extension (all fields empty/zero on an
    // agent section and on an all-opaque workflow). Encoded as a trailing
    // presence-gated block inside a format-version-2 payload, so an agent
    // frame section keeps its rung V2-C bytes exactly.
    // ------------------------------------------------------------------

    /// One packaged instance's fixed node-frame block (parallel to the
    /// workflow module's sorted packaged-instance table).
    struct NodeBlock {
        CoreLayoutId input_layout{};
        CoreLayoutId context_layout{};
        CoreLayoutId output_layout{};
        std::uint32_t input_size{0};
        std::uint32_t context_size{0};
        std::uint32_t output_size{0};
        std::uint32_t input_base{0};
        std::uint32_t context_base{0};
        std::uint32_t scratch_base{0};
        std::uint32_t scratch_size{0};
        std::uint32_t output_base{0};

        [[nodiscard]] friend bool operator==(const NodeBlock &,
                                             const NodeBlock &) noexcept = default;
    };
    std::vector<NodeBlock> node_blocks;
    /// Host-packed workflow entry payload arena (String bytes for the entry
    /// node's I frame), [base, +capacity).
    std::uint32_t entry_payload_base{0};
    std::uint32_t entry_payload_capacity{0};
    /// The fixed slot the scheduler copies a constructed workflow output into
    /// and run2 returns (size is the workflow output root layout size).
    std::uint32_t workflow_output_base{0};

    [[nodiscard]] friend bool operator==(const CoreFrameLayoutSection &,
                                         const CoreFrameLayoutSection &) noexcept = default;
};

struct CoreFrameLayoutEncodeResult {
    std::optional<std::vector<std::uint8_t>> bytes;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return bytes.has_value() && !has_errors(); }
};

struct CoreFrameLayoutDecodeResult {
    std::optional<CoreFrameLayoutSection> section;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return section.has_value() && !has_errors(); }
};

/// Encode the frame-layout section to its deterministic payload bytes.
[[nodiscard]] CoreFrameLayoutEncodeResult
encode_core_frame_layout_section(const CoreFrameLayoutSection &section);

/// Decode a frame-layout payload with local verification AND canonical
/// re-encode byte equality (the same admission discipline as the wire-schema
/// codec). On failure the section is absent and only fixed diagnostics are
/// emitted.
[[nodiscard]] CoreFrameLayoutDecodeResult
decode_core_frame_layout_section(std::span<const std::uint8_t> bytes);

/// Structural consistency between a P4-D layout graph and the logical wire
/// schema graph at the agent boundary roots. This is the admission-time join
/// the design section 3.5 mandates for a transported (layout, schema) pair:
/// same field/variant arity and declaration order, scalar repr implied by the
/// wire bounds agrees with the layout repr, container capacities agree,
/// String/PtrLen correspondence, Option/Enum correspondence, and a
/// closure/Fn/Never shape at a boundary root rejected. Empty diagnostics means
/// the two graphs describe the same boundary type.
///
/// Cycle-safe: recursive nominal graphs terminate (a pair already under
/// comparison is accepted provisionally; acyclic fixtures get the full check).
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_frame_layout_wire_consistency(const CoreLayoutTable &layouts,
                                     CoreLayoutId layout_root,
                                     const CoreWireSchemaTable &wire,
                                     CoreWireSchemaNodeId wire_root);

/// Admission-time cross-check of the V2-C capability bridge records against
/// the admitted wire-schema table: every call site names a capability the
/// wire table projects (by source SymbolId), its arity equals the projected
/// parameter count, and every dense argument/result layout root is structurally
/// consistent with its wire root. Empty diagnostics means every bridge record
/// is admissible; a section with no bridge records always passes.
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_frame_bridge_sites(const CoreFrameLayoutSection &section,
                          const CoreWireSchemaTable &wire);

} // namespace ahfl::ir::core
