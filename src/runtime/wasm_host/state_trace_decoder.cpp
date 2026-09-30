#include "runtime/wasm_host/state_trace_decoder.hpp"

#include <limits>

namespace ahfl::runtime::wasm_host {

namespace {

// Read a little-endian u32 from a bounds-checked address. The caller has
// already verified [addr, addr+4) fits the memory span.
[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::uint8_t> memory,
                                        std::size_t addr) noexcept {
    return static_cast<std::uint32_t>(memory[addr]) |
           (static_cast<std::uint32_t>(memory[addr + 1]) << 8) |
           (static_cast<std::uint32_t>(memory[addr + 2]) << 16) |
           (static_cast<std::uint32_t>(memory[addr + 3]) << 24);
}

} // namespace

std::expected<std::vector<StateTraceRecord>, StateTraceError>
decode_state_trace(std::span<const std::uint8_t> whole_memory,
                   std::uint32_t trace_base,
                   std::uint32_t trace_capacity) {
    // The 8-byte count header must fit.
    if (whole_memory.size() < static_cast<std::size_t>(trace_base) + 8u) {
        return std::unexpected(StateTraceError::Truncated);
    }
    // A zero capacity means the module carries no trace ring (an agent section
    // or an all-opaque workflow); the count is zero by construction.
    if (trace_capacity == 0u) {
        return std::vector<StateTraceRecord>{};
    }
    // The whole region [trace_base, trace_base + capacity) must fit the memory.
    // Checked u64 arithmetic so a corrupt base/capacity cannot wrap.
    const std::uint64_t region_end =
        static_cast<std::uint64_t>(trace_base) + static_cast<std::uint64_t>(trace_capacity);
    if (region_end > std::numeric_limits<std::uint32_t>::max() ||
        region_end > whole_memory.size()) {
        return std::unexpected(StateTraceError::BadCapacity);
    }

    const std::uint32_t count = read_u32_le(whole_memory, trace_base);
    // count*8 + 8 <= capacity (the guest traps on overflow, so a violating
    // count is fail-closed evidence of a corrupt module).
    const std::uint64_t records_end =
        8ull + static_cast<std::uint64_t>(count) * 8ull;
    if (records_end > static_cast<std::uint64_t>(trace_capacity)) {
        return std::unexpected(StateTraceError::CountExceedsCapacity);
    }

    std::vector<StateTraceRecord> records;
    records.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t addr = static_cast<std::size_t>(trace_base) + 8u +
                                 static_cast<std::size_t>(i) * 8u;
        records.push_back(StateTraceRecord{
            .runner = read_u32_le(whole_memory, addr),
            .state = read_u32_le(whole_memory, addr + 4u),
        });
    }
    return records;
}

std::expected<void, StateTraceError>
validate_state_trace_bounds(std::span<const StateTraceRecord> records,
                            std::uint32_t runner_count,
                            std::span<const std::uint32_t> state_counts_per_runner) {
    if (state_counts_per_runner.size() != static_cast<std::size_t>(runner_count)) {
        // A malformed bounds table is a host-side fault, not a module fault;
        // treat it as an out-of-range rejection so the caller fails closed.
        return std::unexpected(StateTraceError::RunnerStateOutOfRange);
    }
    for (const auto &record : records) {
        if (record.runner >= runner_count) {
            return std::unexpected(StateTraceError::RunnerStateOutOfRange);
        }
        if (record.state >= state_counts_per_runner[record.runner]) {
            return std::unexpected(StateTraceError::RunnerStateOutOfRange);
        }
    }
    return {};
}

} // namespace ahfl::runtime::wasm_host
