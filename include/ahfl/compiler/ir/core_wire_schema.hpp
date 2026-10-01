#pragma once

// Deterministic logical wire schemas for selected Core capability imports.
// This is a side artifact: it never lives in or mutates CoreProgram, and it is
// deliberately independent of P4-D physical layouts.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core {

struct CoreWireSchemaNodeId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreWireSchemaNodeId,
                                         CoreWireSchemaNodeId) noexcept = default;
};

struct CoreWireSchemaUnit {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaUnit &,
                                         const CoreWireSchemaUnit &) noexcept = default;
};
struct CoreWireSchemaBool {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaBool &,
                                         const CoreWireSchemaBool &) noexcept = default;
};
struct CoreWireSchemaInt {
    std::optional<std::pair<std::int64_t, std::int64_t>> bounds;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaInt &,
                                         const CoreWireSchemaInt &) noexcept = default;
};
struct CoreWireSchemaFloat {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaFloat &,
                                         const CoreWireSchemaFloat &) noexcept = default;
};
struct CoreWireSchemaString {
    std::optional<std::pair<std::int64_t, std::int64_t>> length_bounds;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaString &,
                                         const CoreWireSchemaString &) noexcept = default;
};
struct CoreWireSchemaDecimal {
    std::int64_t scale{0};
    [[nodiscard]] friend bool operator==(const CoreWireSchemaDecimal &,
                                         const CoreWireSchemaDecimal &) noexcept = default;
};
struct CoreWireSchemaDuration {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaDuration &,
                                         const CoreWireSchemaDuration &) noexcept = default;
};
struct CoreWireSchemaTimestamp {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaTimestamp &,
                                         const CoreWireSchemaTimestamp &) noexcept = default;
};
struct CoreWireSchemaUuid {
    [[nodiscard]] friend bool operator==(const CoreWireSchemaUuid &,
                                         const CoreWireSchemaUuid &) noexcept = default;
};

struct CoreWireSchemaOption {
    CoreWireSchemaNodeId value{};
    [[nodiscard]] friend bool operator==(const CoreWireSchemaOption &,
                                         const CoreWireSchemaOption &) noexcept = default;
};

enum class CoreWireSequenceKind { List, Set };
struct CoreWireSchemaSequence {
    CoreWireSequenceKind kind{CoreWireSequenceKind::List};
    CoreWireSchemaNodeId element{};
    std::optional<std::uint64_t> capacity;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaSequence &,
                                         const CoreWireSchemaSequence &) noexcept = default;
};

struct CoreWireSchemaMap {
    CoreWireSchemaNodeId key{};
    CoreWireSchemaNodeId value{};
    std::optional<std::uint64_t> capacity;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaMap &,
                                         const CoreWireSchemaMap &) noexcept = default;
};

struct CoreWireSchemaField {
    std::string wire_name;
    CoreWireSchemaNodeId type{};
    [[nodiscard]] friend bool operator==(const CoreWireSchemaField &,
                                         const CoreWireSchemaField &) noexcept = default;
};

struct CoreWireSchemaStruct {
    std::string wire_name;
    std::vector<CoreWireSchemaField> fields;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaStruct &,
                                         const CoreWireSchemaStruct &) noexcept = default;
};

enum class CoreWirePayloadKind { Unit, Tuple, Struct };
struct CoreWireSchemaVariant {
    std::string wire_name;
    CoreWirePayloadKind payload_kind{CoreWirePayloadKind::Unit};
    std::vector<CoreWireSchemaField> slots;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaVariant &,
                                         const CoreWireSchemaVariant &) noexcept = default;
};
struct CoreWireSchemaEnum {
    std::string wire_name;
    std::vector<CoreWireSchemaVariant> variants;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaEnum &,
                                         const CoreWireSchemaEnum &) noexcept = default;
};

struct CoreWireSchemaTuple {
    std::vector<CoreWireSchemaNodeId> elements;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaTuple &,
                                         const CoreWireSchemaTuple &) noexcept = default;
};

using CoreWireSchemaShape =
    std::variant<CoreWireSchemaUnit, CoreWireSchemaBool, CoreWireSchemaInt,
                 CoreWireSchemaFloat, CoreWireSchemaString, CoreWireSchemaDecimal,
                 CoreWireSchemaDuration, CoreWireSchemaTimestamp, CoreWireSchemaUuid,
                 CoreWireSchemaOption, CoreWireSchemaSequence, CoreWireSchemaMap,
                 CoreWireSchemaStruct, CoreWireSchemaEnum, CoreWireSchemaTuple>;

struct CoreWireSchemaNode {
    CoreWireSchemaShape shape{CoreWireSchemaUnit{}};
    [[nodiscard]] friend bool operator==(const CoreWireSchemaNode &,
                                         const CoreWireSchemaNode &) noexcept = default;
};

struct CoreWireCapabilitySchema {
    CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    std::vector<CoreWireSchemaNodeId> params;
    CoreWireSchemaNodeId result{};
    [[nodiscard]] friend bool operator==(const CoreWireCapabilitySchema &,
                                         const CoreWireCapabilitySchema &) noexcept = default;
};

// RFC 0026 P6-7: the agent BOUNDARY roots of a P6-frame module. A capability
// table has no frame roots (its roots are capability params/results); a
// P6-frame agent table carries exactly one input and one output root naming the
// agent's boundary nominals. The nodes these name are reachability roots of the
// local verifier, and the frame binding factory derives its binding root from
// them (never from a caller-supplied node id).
struct CoreWireFrameRoots {
    CoreWireSchemaNodeId input{};
    CoreWireSchemaNodeId output{};
    /// RFC 0026 P6-7 frame-bridge v2 D5 (rung V2-D): per-P6-node node
    /// boundary roots of a P6 workflow module (parallel to the workflow's
    /// P6-node table, matching node_blocks). Empty on an agent module and on an
    /// all-opaque workflow.
    std::vector<CoreWireSchemaNodeId> node_inputs;
    std::vector<CoreWireSchemaNodeId> node_outputs;
    [[nodiscard]] friend bool operator==(const CoreWireFrameRoots &,
                                         const CoreWireFrameRoots &) noexcept = default;
};

struct CoreWireSchemaTable {
    std::uint32_t format_version{1};
    std::vector<CoreWireSchemaNode> nodes;
    std::vector<CoreWireCapabilitySchema> capabilities;
    /// Present iff the table projects a P6-frame agent's boundary roots. The
    /// encoded section carries a trailing frame-root block exactly in that case,
    /// so a capability-only section stays byte-identical to the pre-P6-7 format.
    std::optional<CoreWireFrameRoots> frame_roots;
    [[nodiscard]] friend bool operator==(const CoreWireSchemaTable &,
                                         const CoreWireSchemaTable &) noexcept = default;
};

namespace wire_schema {
inline constexpr std::string_view kInvalidCore = "core.wire.INVALID_CORE";
inline constexpr std::string_view kInvalidSelection = "core.wire.INVALID_SELECTION";
inline constexpr std::string_view kInvalid = "core.wire.INVALID";
inline constexpr std::string_view kUnsupported = "core.wire.UNSUPPORTED";
inline constexpr std::string_view kUnsupportedMapKey = "core.wire.UNSUPPORTED_MAP_KEY";
inline constexpr std::string_view kOverflow = "core.wire.OVERFLOW";
} // namespace wire_schema

struct CoreWireSchemaBuildResult {
    std::optional<CoreWireSchemaTable> table;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return table.has_value() && !has_errors(); }
};

struct CoreWireSchemaEncodeResult {
    std::optional<std::vector<std::uint8_t>> bytes;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return bytes.has_value() && !has_errors(); }
};

struct CoreWireSchemaDecodeResult {
    std::optional<CoreWireSchemaTable> table;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return table.has_value() && !has_errors(); }
};

/// Project the transitive wire-schema closure of exactly `selected_capabilities`.
/// The selection must be strictly increasing and duplicate-free. Pure: the
/// verified CoreProgram is never mutated; P4-C member instantiation uses a
/// private value-type arena seeded from `program.value_types`.
///
/// RFC 0026 P6-7: `frame_boundary` optionally names a P6-frame agent's
/// input/output boundary value types. When present, their projections become
/// the table's frame roots (`CoreWireSchemaTable::frame_roots`) and reachability
/// roots of the local verifier, so a capability-free P6-frame agent gets a
/// schema section covering its boundary nominals. The capability selection may
/// be empty in that case.
[[nodiscard]] CoreWireSchemaBuildResult
project_core_wire_schema(const CoreProgram &program,
                         const std::vector<CoreCapabilityId> &selected_capabilities,
                         std::optional<std::pair<CoreValueTypeId, CoreValueTypeId>>
                             frame_boundary = std::nullopt,
                         std::optional<std::pair<std::vector<CoreValueTypeId>,
                                                std::vector<CoreValueTypeId>>>
                             node_boundaries = std::nullopt);

/// Verify both local graph invariants and exact deterministic reprojection from
/// the selected Core capability signatures. When `frame_boundary` is given the
/// reprojection includes the P6-frame agent boundary roots. When
/// `node_boundaries` is given the reprojection also includes the V2-D workflow
/// per-node input/output root tables; a caller verifying a transported
/// workflow frame table must pass the same pair `project_core_wire_schema`
/// received or the per-node roots cannot match the canonical projection.
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_core_wire_schema_table(const CoreProgram &program,
                              const std::vector<CoreCapabilityId> &selected_capabilities,
                              const CoreWireSchemaTable &table,
                              std::optional<std::pair<CoreValueTypeId, CoreValueTypeId>>
                                  frame_boundary = std::nullopt,
                              std::optional<std::pair<std::vector<CoreValueTypeId>,
                                                     std::vector<CoreValueTypeId>>>
                                  node_boundaries = std::nullopt);

/// Verify ONLY the local graph invariants of a wire-schema table (format version,
/// 32-bit id space, strictly-ordered/unique capability roots, per-node structural
/// legality, and cycle-SAFE reachability with no orphan node). This is the gate a
/// generic host (E4-B1 transport, or the E4-B0-C2 codec) applies to a table it
/// did not itself project: it accepts a legal root-reachable CYCLIC graph
/// (recursive nominals) and rejects only bad references, orphans, and illegal
/// shapes. It does NOT re-derive from a CoreProgram (that stronger reprojection
/// check is `verify_core_wire_schema_table`, available only when the Core source
/// is present). Empty diagnostics == locally valid.
///
/// Structural-legality note (RFC 0026 C2b): an `Option` node whose DIRECT child
/// itself encodes as JSON `null` — a `Unit`, or another `Option` (whose `None`
/// is `null`) — is rejected with `core.wire.UNSUPPORTED`. The canonical value
/// encoding writes `Option::None` as `null` and `Some(x)` as x's own encoding, so
/// such a child would make `None` and `Some(child-null)` indistinguishable on the
/// wire. `Option` of any non-null-encoding shape (including a recursive Struct,
/// e.g. `Node{next: Option<Node>}`) remains legal. This restriction lives ONLY
/// here, so both the source projector and a transported/hand-built table are
/// gated by the same single check.
///
/// Reserved wire-name note (RFC 0026 C2b P0-11): two names are writer-impossible
/// and rejected with `core.wire.UNSUPPORTED` (after all structural checks, so a
/// malformed table still reports its structural error first): a Struct field
/// named `_type` (the value_json writer uses `_type` as the struct discriminator,
/// so a same-named field would be a duplicate wire key) and an ORDINARY Enum
/// whose `wire_name` is `std::option::Option` (the value_json Option special-case
/// keys off that name and would encode it as `null`/inner, not the ordinary
/// `_enum` object). A genuine Option is a distinct `CoreWireSchemaOption` shape
/// and is unaffected. Normal source-derived Core never produces either; any
/// synthetic (projector-API) or transported attempt is fail-closed by this same
/// local verifier, so no such table is published or minted.
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_core_wire_schema_table_local(const CoreWireSchemaTable &table);

/// Encode the verified in-memory table into its deterministic section payload.
/// This does not append a Wasm custom section; transport remains E4-B1.
[[nodiscard]] CoreWireSchemaEncodeResult
encode_core_wire_schema_table(const CoreWireSchemaTable &table);

/// Decode a deterministic section payload (as produced by
/// `encode_core_wire_schema_table`) back into an in-memory table. This is the
/// canonical payload admission authority for a transported table: it mirrors the
/// encoder byte-for-byte, then in one entry point runs the local verifier AND a
/// canonical re-encode byte-equality check, so a non-canonical, truncated,
/// overlong, or otherwise tampered payload is rejected before any table is
/// returned. It is Wasm-independent — it neither knows nor parses module framing,
/// import sections, or bindings; E4-B1 transport wraps it. On any failure the
/// table is absent and only fixed schema diagnostics are emitted (never the raw
/// bytes, a decoded string, or a wire name).
[[nodiscard]] CoreWireSchemaDecodeResult
decode_core_wire_schema_table(std::span<const std::uint8_t> bytes);

} // namespace ahfl::ir::core
