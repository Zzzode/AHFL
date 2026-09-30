#pragma once

// RFC 0026 KR6.5 E4-B2-D1a-3: a runtime-owned decoder for the capability-workflow
// node-event buffer that the B2-C Wasm emitter writes into a module's linear memory.
//
// This is a PURE STRUCTURAL FRAMING decoder: it mirrors the emitter's fixed byte
// grammar (8-byte header + 40-byte little-endian tagged records) and returns the
// published records as typed, strong-typed coordinates. It is FOUNDATION only:
//   * it proves completion / ordering FRAMING of the module's own event evidence;
//   * it does NOT join the records to the A2 manifest / call-site coordinates (that
//     cross-check is future B2-D / D1b), it is NOT the B2-E authenticated host event
//     envelope, it does NOT prove no-reinvoke, and it never touches a Wasm VM or a
//     durable-resume store.
// A future B2-D host controller (D1b) collapses ANY `NodeEventError` returned here
// into the stable orchestrator diagnostic `resume.event.malformed`; this decoder
// emits no `resume.*` string of its own.
//
// Buffer grammar (mirrors src/compiler/backends/wasm/core_wasm_codegen.cpp): a
// fixed header at byte 1024 holds `event_count` as a u32 little-endian at [0..3]
// with pad[4..7] == 0; records start at 1032; each record is a fixed 40 bytes; the
// region is statically sized to `node_count` records and the module heap begins at
// `align_up(1024 + 8 + node_count * 40, 8)`. Record layout (little-endian): tag u8
// [0] + pad[1..3]; workflow_node_id u32 [4..7]; schedule_pos u32 [8..11]; capability
// u32 [12..15]; source_symbol u64 [16..23]; invocation_ordinal u64 [24..31]; status
// u32 [32..35]; reserved[36..39]. tag 0 = identity, tag 1 = capability; only
// successful (AHFL_CAP_OK) completions are written, one per scheduled node.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"               // CoreWorkflowNodeId, CoreCapabilityId
#include "runtime/engine/core_wasm_resume_record.hpp" // NodeKind, InvocationOrdinal
#include "runtime/engine/core_wasm_schema_module.hpp" // ManifestNodeIndex

namespace ahfl::runtime::core_wasm_node_events {

// One decoded, validated node-event record. Coordinates reuse the existing strong
// types; `status` is kept as the raw u32 but an admitted record is always
// AHFL_CAP_OK (0). For an identity record (`kind == Identity`) the capability /
// source_symbol / invocation_ordinal fields are all zero.
struct NodeEventRecord {
    core_wasm_resume::NodeKind kind{core_wasm_resume::NodeKind::Identity};
    ir::core::CoreWorkflowNodeId workflow_node_id{};
    core_wasm_schema_module::ManifestNodeIndex schedule_pos{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    core_wasm_resume::InvocationOrdinal invocation_ordinal{};
    std::uint32_t status{0};
    [[nodiscard]] friend bool operator==(const NodeEventRecord &,
                                         const NodeEventRecord &) noexcept = default;
};

// A bare, no-echo structural error (carries no byte / offset / value). The failure
// order is: header-minimum span -> full static layout arithmetic + memory fit ->
// header pad / count -> each published record in index order.
enum class NodeEventError : std::uint8_t {
    Truncated,            // linear memory too small to hold the 8-byte header
    LayoutOverflow,       // node_count out of range or the checked region math overflows
    LayoutExceedsMemory,  // the header + records + heap do not fit the linear memory
    BadHeaderPad,         // header pad[4..7] is nonzero
    CountExceedsNodeCount, // event_count > node_count
    BadTag,               // record tag is neither 0 nor 1
    BadRecordPad,         // record pad[1..3] is nonzero
    BadReserved,          // record reserved[36..39] is nonzero
    InvalidWorkflowNodeId, // record workflow_node_id is the invalid sentinel
    InvalidCapabilityId,  // capability record capability id is the invalid sentinel
    SchedulePosMismatch,  // record schedule_pos != its published index
    IdentityFieldsNonZero, // identity record has a nonzero cap / source_symbol / ordinal
    StatusNotOk,          // record status is not AHFL_CAP_OK
};

// Decode the node-event buffer from a read-only view of the module's WHOLE linear
// memory. `linear_memory.size()` is the SOLE memory-size authority; the record
// region end and the heap base are computed internally from `node_count` with
// checked arithmetic. Returns the published records (0..event_count) in index order,
// or the first structural violation. Not `noexcept`: the returned vector allocates.
[[nodiscard]] std::expected<std::vector<NodeEventRecord>, NodeEventError>
decode_node_events(std::span<const std::uint8_t> linear_memory, std::size_t node_count);

// A single-value overflow error for the pure layout authority below. It is
// deliberately DISTINCT from `NodeEventError` so the two owners map it locally: the
// decoder maps `Overflow` -> `NodeEventError::LayoutOverflow` (future collapse to
// `resume.event.malformed`), while the future B2-D D1b TOTAL preflight maps the SAME
// `Overflow` -> `resume.preflight.resource_exhausted`. One layout SSOT, two
// owner-local mappings.
enum class NodeEventLayoutError : std::uint8_t {
    Overflow, // node_count, or the checked region / heap_base arithmetic, leaves u32
};

// The SINGLE runtime authority for the capability-workflow node-event buffer's heap
// base. LAYOUT-PURE: it takes only `node_count` (never a span / capacity), performs
// the checked u64 arithmetic `heap_base = align_up(1024 + 8 + node_count * 40, 8)`,
// and returns that heap_base, or `Overflow` if `node_count` or the region/align math
// leaves the wasm32 (u32) domain. It renders NO capacity / one-page verdict: a legal
// `heap_base` larger than one 64 KiB page is returned normally (e.g. node_count 1613
// -> 65552); only `decode_node_events` compares that heap_base against the SUPPLIED
// span. It models ONLY the capability-workflow event buffer formula (node_count 0
// yields 1032 as a pure-formula boundary, NOT a claim about a no-capability emitter,
// which has no event buffer). Both `decode_node_events` and the future D1b TOTAL
// preflight call THIS; the compiler-backend `compute_event_layout` remains the
// byte-emitter authority (no runtime<->compiler dependency; agreement is locked by
// the common-KAT).
[[nodiscard]] std::expected<std::uint64_t, NodeEventLayoutError>
event_region_heap_base(std::size_t node_count) noexcept;

// WH-4b P1-1: read the node-event completion counter (event_count) from the buffer
// header. The guest scheduler walks nodes in Kahn order and writes
// `event_count = schedule_pos + 1` on each node's completion, so at an import
// boundary during node K's execution nodes 0..K-1 have completed and
// `event_count == K` (the CURRENT node's schedule position). This is a
// header-only read for the per-import replay classification path; the full
// `decode_node_events` validates every record and is the post-run authority.
// Returns nullopt if the span is too small to hold the 8-byte header. The
// caller bounds-checks the returned count against its own node count.
[[nodiscard]] std::optional<std::uint32_t>
read_event_count(std::span<const std::uint8_t> linear_memory) noexcept;

} // namespace ahfl::runtime::core_wasm_node_events
