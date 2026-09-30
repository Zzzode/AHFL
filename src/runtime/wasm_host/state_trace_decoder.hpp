#pragma once

// RFC 0026 KR6.8 WH-4: a runtime-owned decoder for the state-entry trace ring
// the V2-D P6 workflow emitter writes into a module's linear memory. This is
// the host-side reader for the ring the guest appends one (runner, state)
// 8-byte record to on EVERY dispatched state (core_wasm_codegen.cpp
// append_state_trace_record), giving the host real runtime state-sequence
// evidence across input-dependent computed handlers.
//
// This is a PURE STRUCTURAL FRAMING decoder: it mirrors the emitter's fixed
// byte grammar (u32 little-endian count + 8-byte records) and returns the
// published records as typed (runner, state) pairs. The ring coordinates are
// DYNAMIC — they come from the admitted CoreFrameLayoutSection's
// state_trace_base / state_trace_capacity, never an ABI constant — so the
// decoder takes them as parameters. The (runner,state) -> (agent,state-name)
// join is the session layer's job (it has the descriptor's runner-indexed
// walk table); validate_state_trace_bounds() is the typed out-of-range gate.
//
// Ring grammar (mirrors append_state_trace_record):
//   * u32 count at trace_base (little-endian);
//   * records at trace_base + 8, each 8 bytes: u32 runner (LE) + u32 state (LE);
//   * the guest writes a record THEN increments the count, so a prefix read at
//     an import boundary always sees a count consistent with fully-written
//     records;
//   * the guest traps on overflow (count*8 + 8 > capacity), so a decoded count
//     that violates count*8 + 8 <= capacity is fail-closed evidence of a
//     corrupt / non-AHFL module.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace ahfl::runtime::wasm_host {

// One decoded (runner, state) trace record. `runner` is the dense packaged-
// instance index (the descriptor's agents[] table); `state` is the dense
// agent state id (the descriptor's all_states[] index).
struct StateTraceRecord {
    std::uint32_t runner{0};
    std::uint32_t state{0};

    [[nodiscard]] friend bool
    operator==(const StateTraceRecord &, const StateTraceRecord &) noexcept = default;
};

// A bare, no-echo structural error (carries no byte / offset / value). The
// failure order is: header-minimum span -> full region arithmetic + memory fit
// -> count/capacity -> each published record in index order.
enum class StateTraceError : std::uint8_t {
    Truncated,             // linear memory too small to hold the 8-byte count header
    BadCapacity,           // trace_base + trace_capacity overflows u32 or exceeds memory
    CountExceedsCapacity,  // count*8 + 8 > trace_capacity
    RunnerStateOutOfRange, // a record's runner/state is outside the declared bounds
};

// Decode the state-entry trace ring from a read-only view of the module's
// WHOLE linear memory. `whole_memory.size()` is the SOLE memory-size authority.
// Returns the published records (0..count) in index order, or the first
// structural violation. Not `noexcept`: the returned vector allocates.
//
// Prefix reads: call this at a capability-import boundary with the live
// whole_memory and it decodes whatever count the guest has published so far
// (the guest writes records before bumping the count, so the prefix is always
// a consistent prefix of the final trace). The session layer tracks the
// last-seen count to fire hooks only for NEW records.
[[nodiscard]] std::expected<std::vector<StateTraceRecord>, StateTraceError>
decode_state_trace(std::span<const std::uint8_t> whole_memory,
                   std::uint32_t trace_base,
                   std::uint32_t trace_capacity);

// Validate decoded records against the declared runner/state bounds. This is
// the join-level out-of-range gate the JS oracle performs against
// lane.agents[runner].all_states: a record whose runner is >= runner_count or
// whose state is >= that runner's declared state count is fail-closed evidence
// of a corrupt module. `state_counts_per_runner` is indexed by runner and
// carries each packaged agent's declared state count (all_states.size()).
[[nodiscard]] std::expected<void, StateTraceError>
validate_state_trace_bounds(std::span<const StateTraceRecord> records,
                            std::uint32_t runner_count,
                            std::span<const std::uint32_t> state_counts_per_runner);

} // namespace ahfl::runtime::wasm_host
