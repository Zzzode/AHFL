#pragma once

// Target-specific physical layouts for Core-IR logical value types (RFC 0026
// P4-D). Layouts are a deterministic side artifact: they never live in or
// mutate CoreProgram.

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::ir::core {

enum class TargetDataLayoutId { Wasm32 };

struct TargetDataLayout {
    TargetDataLayoutId id{TargetDataLayoutId::Wasm32};
    std::uint32_t pointer_size{4};
    std::uint32_t pointer_align{4};
    std::uint32_t function_index_size{4};
    std::uint32_t function_index_align{4};
    [[nodiscard]] friend bool operator==(const TargetDataLayout &,
                                         const TargetDataLayout &) noexcept = default;
};

struct CoreLayoutId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreLayoutId, CoreLayoutId) noexcept = default;
};

enum class CoreScalarRepr { I32, I64, F64 };

struct CoreLayoutPending {
    [[nodiscard]] friend bool operator==(const CoreLayoutPending &,
                                         const CoreLayoutPending &) noexcept = default;
};
struct CoreLayoutScalar {
    CoreScalarRepr repr{CoreScalarRepr::I32};
    [[nodiscard]] friend bool operator==(const CoreLayoutScalar &,
                                         const CoreLayoutScalar &) noexcept = default;
};
struct CoreLayoutBytes {
    std::uint64_t byte_count{0};
    [[nodiscard]] friend bool operator==(const CoreLayoutBytes &,
                                         const CoreLayoutBytes &) noexcept = default;
};
struct CoreLayoutPtrLen {
    [[nodiscard]] friend bool operator==(const CoreLayoutPtrLen &,
                                         const CoreLayoutPtrLen &) noexcept = default;
};
// RFC 0026 FB-1 D-FNREP (CORE-FNBODY-DESIGN §3.1.1): the former standalone
// four-byte FnRef layout was DELETED. Every first-class callable VALUE now has
// ONE physical representation — the eight-byte `CoreLayoutClosure` shape
// `(func_index:i32, env_ptr:i32)`. `CoreVtFn` survives only as a logical
// SIGNATURE type (it no longer owns a layout); a bare static-fn reference is a
// zero-capture closure `(table_slot, env_ptr=0)`. This variant alternative is
// kept in the layout-shape X-set (so wire/reader indices do not shift) but is
// now UNREACHABLE from a well-formed finalized table.
struct CoreLayoutFnRef {
    [[nodiscard]] friend bool operator==(const CoreLayoutFnRef &,
                                         const CoreLayoutFnRef &) noexcept = default;
};
/// RFC 0026 P6-8a (P4-D D2): a closure's runtime representation is
/// `(func_index:i32, env_ptr:i32)` — a wasm32 function-table index followed by
/// the address of its captured environment — so the shape is ALWAYS size 8 /
/// alignment 4 on the canonical wasm32 target (a `FnRef` word plus a pointer
/// word, never a third unrelated one), independent of how much the closure
/// captures.
///
/// The environment itself is a separate aggregate layout reached by an INDIRECT
/// edge (design §3.3), holding one field per capture slot in canonical env-slot
/// order; `environment` is absent iff the closure captures nothing. Keeping the
/// edge indirect is what makes the closure's own size independent of its
/// captures: `struct S { f: Closure<capturing S> }` finalises (the closure field
/// is an 8-byte word pair; the env is a separate 8-byte aggregate that inlines
/// S), and a closure capturing a `List<S>(n)` is finite because the collection's
/// element edge is already indirect.
struct CoreLayoutClosure {
    std::optional<CoreLayoutId> environment; // Indirect edge; absent iff no captures.
    [[nodiscard]] friend bool operator==(const CoreLayoutClosure &,
                                         const CoreLayoutClosure &) noexcept = default;
};
struct CoreLayoutStruct {
    std::vector<std::uint64_t> field_offsets;
    std::vector<CoreLayoutId> field_layouts; // Inline edges, declaration/index order.
    [[nodiscard]] friend bool operator==(const CoreLayoutStruct &,
                                         const CoreLayoutStruct &) noexcept = default;
};
struct CoreLayoutEnum {
    std::uint32_t tag_size{4};
    std::uint64_t payload_offset{0};
    std::vector<CoreLayoutId> variant_payload_layouts; // Inline aggregate edges.
    std::vector<std::uint64_t> variant_payload_sizes;
    [[nodiscard]] friend bool operator==(const CoreLayoutEnum &,
                                         const CoreLayoutEnum &) noexcept = default;
};
struct CoreLayoutContainer {
    CoreLayoutId element{};                 // Indirect backing edge (Map key).
    std::optional<CoreLayoutId> value;      // Indirect Map-value edge.
    std::uint64_t capacity{0};
    std::uint64_t stride{0};
    std::uint64_t value_offset{0};
    std::uint64_t backing_size{0};
    [[nodiscard]] friend bool operator==(const CoreLayoutContainer &,
                                         const CoreLayoutContainer &) noexcept = default;
};
struct CoreLayoutUninhabited {
    [[nodiscard]] friend bool operator==(const CoreLayoutUninhabited &,
                                         const CoreLayoutUninhabited &) noexcept = default;
};

using CoreLayoutShape =
    std::variant<CoreLayoutPending, CoreLayoutScalar, CoreLayoutBytes, CoreLayoutPtrLen,
                 CoreLayoutFnRef, CoreLayoutClosure, CoreLayoutStruct, CoreLayoutEnum,
                 CoreLayoutContainer, CoreLayoutUninhabited>;

struct CoreLayout {
    std::uint64_t size{0};
    std::uint32_t align{1};
    bool is_zero_sized{true};
    CoreLayoutShape shape{CoreLayoutPending{}};
    [[nodiscard]] friend bool operator==(const CoreLayout &,
                                         const CoreLayout &) noexcept = default;
};

struct CoreLayoutTable {
    TargetDataLayout target{};
    std::vector<CoreLayout> layouts;          // index == CoreLayoutId
    std::vector<CoreLayoutId> value_layouts;  // parallel to input CoreProgram::value_types
    [[nodiscard]] friend bool operator==(const CoreLayoutTable &,
                                         const CoreLayoutTable &) noexcept = default;
};

namespace layout {
inline constexpr std::string_view kUnsupported = "core.layout.UNSUPPORTED";
inline constexpr std::string_view kUnbounded = "core.layout.UNBOUNDED";
inline constexpr std::string_view kOverflow = "core.layout.OVERFLOW";
inline constexpr std::string_view kInfiniteRecursion = "core.layout.INFINITE_RECURSION";
inline constexpr std::string_view kInvalid = "core.layout.INVALID";
} // namespace layout

struct CoreLayoutBuildResult {
    std::optional<CoreLayoutTable> table;
    std::vector<CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return table.has_value() && !has_errors(); }
};

/// Compute the wasm32 physical layout side artifact without mutating `program`.
/// The returned table exists iff all roots and layout-private member types were
/// finalized and the standalone layout verifier accepted the result.
[[nodiscard]] CoreLayoutBuildResult
compute_core_layouts(const CoreProgram &program, TargetDataLayout target = {});

/// Pure structural verification of a completed layout graph. It validates the
/// target ABI, root mapping, per-shape arithmetic, id bounds/reachability,
/// Inline-edge acyclicity, the absence of unfinished placeholders, and exact
/// equality with the deterministic projection of `program` (including its
/// layout-private P4-C member-type closure).
[[nodiscard]] std::vector<CoreLowerDiagnostic>
verify_core_layout_table(const CoreProgram &program, const CoreLayoutTable &table);

/// Cycle-safe structural equivalence within one finalized layout table.
[[nodiscard]] bool layouts_equivalent(const CoreLayoutTable &table,
                                      CoreLayoutId lhs,
                                      CoreLayoutId rhs);

/// Physical equivalence for two logical value types. Equal logical ids are the
/// fast path; otherwise the corresponding layout graphs are compared.
[[nodiscard]] bool value_layouts_equivalent(const CoreLayoutTable &table,
                                            CoreValueTypeId lhs,
                                            CoreValueTypeId rhs);

} // namespace ahfl::ir::core
