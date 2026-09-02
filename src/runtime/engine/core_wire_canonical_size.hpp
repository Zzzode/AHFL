#pragma once

// RFC 0026 KR6.5 E4-B2-D1a-4: a runtime-owned CONSERVATIVE upper bound on the
// canonical wire-JSON size (in bytes) of any value that conforms to one Verified
// Result binding. The bound is NEVER an underestimate; it may be loose (Set/Map
// element distinctness and the fixed 20-byte Int width can leave slack). It is a
// pure function of the schema graph reachable from the binding root along
// PRODUCTIVE edges — it does not look at any concrete value.
//
// SCOPE (foundation only): this is the per-Verified-Result bound authority. It
// does NOT compute the TOTAL durable-resume reservation (event region + entry
// frame + memo result frames + live/injected Result + allocator framing) and it
// does NOT render the fixed-single-page capacity verdict; those, and any u32/host
// cast gate, belong to a future D1b host controller. This module emits no
// `resume.*` diagnostic string: a future controller maps `Unbounded` to
// `resume.preflight.unbounded` and `SizeOverflow` to
// `resume.preflight.resource_exhausted`. It is NOT a production host, NOT an
// effect / CAS gate, NOT durable resume, NOT B2-E, NOT a protected store.

#include <cstdint>
#include <expected>

#include "ahfl/compiler/ir/core_wire_migration.hpp"

namespace ahfl::runtime::core_wire_canonical_size {

// A bare, no-echo failure (carries no node id, byte, or value). Boundedness is
// resolved WHOLE-GRAPH before any size arithmetic, so `Unbounded` is reported
// strictly before `SizeOverflow` regardless of node-visit order.
enum class MaxCanonicalSizeError : std::uint8_t {
    Unbounded,   // the schema has NO finite canonical upper bound
    SizeOverflow // the schema is finite, but a checked u64 size step overflows
};

// Return a conservative, never-underestimating upper bound (in bytes) on the
// canonical wire JSON that `evaluator::value_to_json` can emit for any value
// conforming to `binding`'s verified schema. This D1a-4 authority is used only
// with a Result binding (the durable-resume injected/live result); it adds no
// runtime kind gate. Walks only the productive-edge subgraph reachable from
// `binding.root()` over `binding.table().nodes`; a zero-capacity Sequence/Set/Map
// cuts its child edges (its subtree cannot be instantiated). Two explicit-worklist
// passes, no recursion:
//   pass 1 proves the reachable productive graph finite (no unbounded scalar,
//          no productive recursive cycle) -> `Unbounded` otherwise;
//   pass 2 computes the memoized checked size over the proven finite DAG in
//          child-before-parent (DFS finish) order -> `SizeOverflow` on overflow.
// Complexity O(reachable nodes + edges + schema name bytes).
[[nodiscard]] std::expected<std::uint64_t, MaxCanonicalSizeError>
max_canonical_json_size(const ir::core::VerifiedWireSchemaBinding &binding);

} // namespace ahfl::runtime::core_wire_canonical_size
