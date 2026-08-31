#pragma once

// Deterministic logical wire schemas for selected Core capability imports.
// This is a side artifact: it never lives in or mutates CoreProgram, and it is
// deliberately independent of P4-D physical layouts.

#include <cstdint>
#include <optional>
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

struct CoreWireSchemaTable {
    std::uint32_t format_version{1};
    std::vector<CoreWireSchemaNode> nodes;
    std::vector<CoreWireCapabilitySchema> capabilities;
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

/// Project the transitive wire-schema closure of exactly `selected_capabilities`.
/// The selection must be strictly increasing and duplicate-free. Pure: the
/// verified CoreProgram is never mutated; P4-C member instantiation uses a
/// private value-type arena seeded from `program.value_types`.
[[nodiscard]] CoreWireSchemaBuildResult
project_core_wire_schema(const CoreProgram &program,
                         const std::vector<CoreCapabilityId> &selected_capabilities);

/// Verify both local graph invariants and exact deterministic reprojection from
/// the selected Core capability signatures.
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_core_wire_schema_table(const CoreProgram &program,
                              const std::vector<CoreCapabilityId> &selected_capabilities,
                              const CoreWireSchemaTable &table);

/// Verify ONLY the local graph invariants of a wire-schema table (format version,
/// 32-bit id space, strictly-ordered/unique capability roots, per-node structural
/// legality, and cycle-SAFE reachability with no orphan node). This is the gate a
/// generic host (E4-B1 transport, or the E4-B0-C2 codec) applies to a table it
/// did not itself project: it accepts a legal root-reachable CYCLIC graph
/// (recursive nominals) and rejects only bad references, orphans, and illegal
/// shapes. It does NOT re-derive from a CoreProgram (that stronger reprojection
/// check is `verify_core_wire_schema_table`, available only when the Core source
/// is present). Empty diagnostics == locally valid.
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_core_wire_schema_table_local(const CoreWireSchemaTable &table);

/// Encode the verified in-memory table into its deterministic section payload.
/// This does not append a Wasm custom section; transport remains E4-B1.
[[nodiscard]] CoreWireSchemaEncodeResult
encode_core_wire_schema_table(const CoreWireSchemaTable &table);

} // namespace ahfl::ir::core
