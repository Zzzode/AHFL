#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/handoff/package.hpp"
#include "ahfl/compiler/ir/core_frame_layout.hpp"
#include "ahfl/compiler/ir/core_layout.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "compiler/backends/infra/wasm_backend.hpp"

namespace ahfl::backends {

namespace core_wasm_diag {
inline constexpr std::string_view kInvalidCore = "wasm.INVALID_CORE";
inline constexpr std::string_view kInvalidLayout = "wasm.INVALID_LAYOUT";
inline constexpr std::string_view kUnsupportedTarget = "wasm.UNSUPPORTED_TARGET";
inline constexpr std::string_view kEntryAmbiguous = "wasm.ENTRY_AMBIGUOUS";
inline constexpr std::string_view kEntryNotFound = "wasm.ENTRY_NOT_FOUND";
inline constexpr std::string_view kUnsupportedOrchestration = "wasm.UNSUPPORTED_ORCHESTRATION";
inline constexpr std::string_view kNonterminatingE1Run = "wasm.NONTERMINATING_E1_RUN";
inline constexpr std::string_view kInvalidCapabilityAbi = "wasm.INVALID_CAPABILITY_ABI";
inline constexpr std::string_view kUnsupportedCapabilityFrame = "wasm.UNSUPPORTED_CAPABILITY_FRAME";
inline constexpr std::string_view kUnsupportedWorkflowFrame = "wasm.UNSUPPORTED_WORKFLOW_FRAME";
inline constexpr std::string_view kBinaryOverflow = "wasm.BINARY_OVERFLOW";
inline constexpr std::string_view kResourceExhausted = "wasm.RESOURCE_EXHAUSTED";
inline constexpr std::string_view kInternalInvalid = "wasm.INTERNAL_INVALID";
// RFC 0026 FB-3a1: the closure value model exists, but funcref table / element
// segment / call_indirect emission arrives with the FB-3 codegen slice. Until
// then a CoreClosureExpr / CoreCallClosureExpr in a planned body fails closed
// with NO artifact (the Core verifier may accept a well-formed one, but it is
// not executable on this backend yet).
inline constexpr std::string_view kUnsupportedClosure = "wasm.UNSUPPORTED_CLOSURE";
} // namespace core_wasm_diag

using CoreWasmEntry = std::variant<ir::core::CoreAgentId, ir::core::CoreWorkflowId>;

struct CoreWasmTarget {
    CoreWasmEntry entry{ir::core::CoreAgentId{}};
    WasmProfileKind profile{WasmProfileKind::Wasi};
};

struct CoreWasmArtifact {
    std::vector<std::uint8_t> bytes;
    CoreWasmEntry entry{ir::core::CoreAgentId{}};
    std::vector<ir::core::CoreInstanceId> packaged_agent_instances;
    std::vector<std::string> exports;
    std::vector<std::string> imports;
};

struct CoreWasmDiagnostic {
    std::string code;
    std::string message;
    ir::SourceRangeOpt source_range;
};

// KR6.7 (RFC 0026 P7): the machine-readable EXECUTION DESCRIPTOR for one emitted
// module. It is produced by `emit_core_wasm` from the SAME internal plan the
// bytes are emitted from (never a second derivation), and is the neutral input a
// generic embedded engine (the Node conformance host) uses to reconstruct the
// canonical run observation without parsing bespoke `key=value` lines.
//
// Two execution lanes share one Core-Wasm ABI:
//   * agent:    one state machine driven through step()/run2; `states` is the
//               id -> state-name table and `initial_state` the entry id.
//   * workflow: a Kahn-ordered node schedule; each node invokes one PACKAGED
//               agent runner. `agents` is the runner table (the walk is the
//               ordered state names that runner enters on one invocation) and
//               `nodes` is the schedule in execution order.
//
// Identity is name-only for the descriptor consumer (state / agent / capability
// canonical names); all ids here are dense wasm-visible ordinals the host reads
// from globals and the node-event buffer, never engine-internal SymbolIds.
struct CoreWasmStateWalk {
    std::string agent;             // canonical agent name
    std::vector<std::string> walk; // state names entered on one runner call
};

struct CoreWasmNodeDescriptor {
    std::uint32_t node_id{0};      // workflow node id (node-event record)
    std::uint32_t schedule_pos{0}; // dense execution position
    std::uint32_t runner{0};       // index into CoreWasmExecutionDescriptor::agents
    bool has_capability{false};
    std::uint32_t capability_ordinal{0}; // index into imports when has_capability
    std::uint64_t source_symbol{0};      // source SymbolId carried in the event record
};

struct CoreWasmCapabilityImport {
    std::uint32_t ordinal{0};
    std::string field;          // wasm import field, e.g. "cap_7"
    std::string canonical_name; // canonical capability name
    /// RFC 0026 P6-7 frame-bridge v2 D3 (rung V2-C): the import's functype
    /// protocol. "opaque" is the fixed (i32,i32)->(i32,i32,i32) tuple final
    /// forward; "bridge" is the additive (i32)->(i32,i32) control-block frame
    /// bridge. One capability has exactly one mode per module.
    std::string mode{"opaque"};
};

/// RFC 0026 P6-7: one input-reached bounded container's disjoint backing
/// placement, mirrored into the descriptor so a generic host packs collection
/// headers/elements without parsing the layout section itself.
struct CoreWasmFramePlacement {
    std::uint32_t edge_index{0};
    std::uint32_t base{0};
    std::uint32_t extent{0};
};

/// One per-call-site capability bridge the host callback serves (frame-bridge
/// v2 D3/D4, rung V2-C). The module writes the control block at
/// `block_ptr` = control_base + block_offset; the host walks its arity P4-D
/// argument spans, invokes `capability` (ordinal resolves the import), and
/// packs the validated result root at `result_base` with String payload bytes
/// in the disjoint result payload arena.
struct CoreWasmBridgeCallSite {
    std::uint32_t call_site_id{0};
    std::uint32_t import_ordinal{0};
    std::uint64_t source_symbol{0};
    std::uint32_t arity{0};
    std::uint32_t block_offset{0};
    /// Dense wire-schema node ids for the arguments (declaration order) and the
    /// result, mirroring the layout roots in the frame section.
    std::vector<std::uint32_t> param_wire;
    /// Dense layout ids for the arguments in the frame section's table.
    std::vector<std::uint32_t> param_layout;
    std::uint32_t result_wire{0};
    /// Dense layout id of the result root in the frame section's table (the
    /// authority the host packs at `result_base`).
    std::uint32_t result_layout{0};
    std::uint32_t result_base{0};
    std::uint32_t result_extent{0};
    std::uint32_t result_payload_base{0};
    std::uint32_t result_payload_capacity{0};
};

/// RFC 0026 P6-7: the P6-frame facts an embedded host needs to pack input and
/// read output. Populated iff `frame_contract == P6Frame`. The layout table and
/// wire schema themselves ride in the module's custom sections; this carries
/// only the fixed-region coordinates and final-kind discriminator the host
/// cross-checks against them.
struct CoreWasmFrameLane {
    /// "identity" finals return the borrowed input base; "computed" finals
    /// materialize into the output base. The sole authority for which base
    /// runv's value_ptr may name.
    std::string final_kind; // "identity" / "computed"
    std::uint32_t input_base{0};
    std::uint32_t input_size{0};
    std::uint32_t output_base{0};
    std::uint32_t output_size{0};
    std::vector<CoreWasmFramePlacement> placements;
    std::uint32_t payload_arena_base{0};
    std::uint32_t payload_arena_capacity{0};
    /// RFC 0026 P6-7 frame-bridge v2 D1 (rung V2-B): the read-only String
    /// literal pool the module's Data(11) section initializes. `rodata_base` is
    /// the fixed kP6RodataBase (256); `rodata_extent` is zero when the module
    /// emits no Data section and the active segment byte length otherwise.
    std::uint32_t rodata_base{0};
    std::uint32_t rodata_extent{0};
    /// RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): the capability bridge
    /// control page frame. Empty for a non-bridge module.
    std::uint32_t bridge_control_base{0};
    std::uint32_t bridge_block_stride{0};
    std::uint32_t bridge_control_extent{0};
    std::uint32_t bridge_spill_base{0};
    std::uint32_t bridge_spill_extent{0};
    std::vector<CoreWasmBridgeCallSite> bridge_call_sites;
};

/// The input/output frame contract an embedded host must honor.
enum class CoreWasmFrameContract {
    /// The run2 boundary carries opaque canonical value_json BYTES: the host
    /// writes the scenario's canonical input bytes and reads the output bytes
    /// verbatim (identity passthrough or a forwarded capability result). The
    /// canonical observation is fully reconstructable.
    WireJson,
    /// A P6-frame module (RFC 0026 P6-7): a computation handler projects raw
    /// P4-D input-frame bytes or returns a computed final. It carries the
    /// `ahfl.core-layout.v1` and boundary-root wire-schema sections and exports
    /// `runv() -> (status, value_ptr)`. The host packs canonical input JSON
    /// into the fixed P4-D regions, calls runv, and walks the returned frame
    /// with the verified layout + wire binding to render canonical output.
    /// (Descriptor spelling: "p6_frame".)
    P6Frame,
};

struct CoreWasmExecutionDescriptor {
    bool is_workflow{false};
    CoreWasmFrameContract frame_contract{CoreWasmFrameContract::WireJson};

    // Agent lane.
    std::string agent_name;          // canonical entry agent name
    std::vector<std::string> states; // id -> state name
    std::uint32_t initial_state{0};

    // Workflow lane.
    std::vector<CoreWasmStateWalk> agents;     // runner index -> walk
    std::vector<CoreWasmNodeDescriptor> nodes; // schedule order

    // Both lanes: reachable capability imports in ordinal order.
    std::vector<CoreWasmCapabilityImport> imports;

    // RFC 0026 P6-7: populated iff frame_contract == P6Frame.
    std::optional<CoreWasmFrameLane> frame;
    // RFC 0026 P6-7: the verified boundary tables a generic embedded host packs
    // input from and encodes output against, populated iff frame_contract ==
    // P6Frame. They mirror the module's `ahfl.core-layout.v1` /
    // `ahfl.wire-schema.v1` custom sections (same bytes the sections encode), so
    // the descriptor needs no separate trust channel; the host may re-admit the
    // sections independently. These are descriptor-rendering inputs, not part of
    // the wire ABI proper.
    std::optional<ir::core::CoreFrameLayoutSection> frame_section;
    std::optional<ir::core::CoreWireSchemaTable> wire_schema;

    // Node-event buffer layout (identity for both lanes; the ABI SSOT).
    std::uint32_t event_log_base{0};
    std::uint32_t event_header_bytes{0};
    std::uint32_t event_record_bytes{0};
    std::uint32_t event_records_base{0};
    // Number of scheduled nodes (workflow) / 0 for a bare agent; also the
    // static capacity of the node-event record region.
    std::uint32_t workflow_node_count{0};
    // First bump-heap address: kNodeEventLogBase for an identity workflow /
    // agent, align_up(records_region, 8) for a capability workflow.
    std::uint32_t heap_base{0};
};

struct CoreWasmCodegenResult {
    std::optional<CoreWasmArtifact> artifact;
    std::vector<CoreWasmDiagnostic> diagnostics;
    /// Populated iff `artifact` is present: the machine-readable execution
    /// descriptor derived from the plan that produced the artifact.
    std::optional<CoreWasmExecutionDescriptor> descriptor;

    [[nodiscard]] bool ok() const noexcept {
        return artifact.has_value() && diagnostics.empty();
    }
};

/// Resolve the package/legacy CLI boundary into the same typed entry consumed
/// by emit_core_wasm. An explicit package entry is exact-canonical and never
/// falls back to a display name or declaration position.
[[nodiscard]] std::expected<CoreWasmEntry, CoreWasmDiagnostic>
resolve_core_wasm_entry(const ir::core::CoreProgram &program,
                        const handoff::PackageMetadata *package_metadata);

/// Emit a deterministic wasm32 binary selected by one typed entry identity:
/// the KR6.5 E1/E2 agent subset, the E3 no-capability identity-workflow subset,
/// or (RFC 0026 E4-B2-C FOUNDATION) a capability-bearing workflow module. The
/// public C ABI and exported symbol names are unchanged across all of these; the
/// capability-workflow artifact reuses the E2 `ahfl_cap` import ABI and
/// additionally carries an exec-manifest and a node-event buffer.
/// Pure: neither the verified Core program nor its P4-D layout side artifact is
/// mutated. Unsupported Core nodes fail closed with no partial artifact.
[[nodiscard]] CoreWasmCodegenResult emit_core_wasm(const ir::core::CoreProgram &program,
                                                   const ir::core::CoreLayoutTable &layouts,
                                                   CoreWasmTarget target);

} // namespace ahfl::backends
