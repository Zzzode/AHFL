#include "runtime/engine/core_wasm_node_events.hpp"

#include <cstdint>
#include <limits>

namespace ahfl::runtime::core_wasm_node_events {
namespace {

// Buffer grammar constants, mirroring the B2-C emitter
// (src/compiler/backends/infra/core_wasm_codegen.cpp): the header is at byte 1024,
// records start at 1032, and each record is 40 bytes. The current emitter baseline
// uses one fixed 64 KiB linear-memory page, but the decoder does NOT hardcode that
// capacity: it accepts the supplied whole-memory span as the sole size authority.
// The emitter is the byte-grammar authority; this decoder re-states the same
// constants (no runtime<->compiler link).
constexpr std::uint32_t kEventLogBase = 1024;
constexpr std::uint32_t kEventHeaderBytes = 8;
constexpr std::uint32_t kEventRecordBytes = 40;
constexpr std::uint32_t kEventRecordsBase = kEventLogBase + kEventHeaderBytes; // 1032
constexpr std::uint8_t kTagIdentity = 0;
constexpr std::uint8_t kTagCapability = 1;
constexpr std::uint32_t kStatusOk = 0; // AHFL_CAP_OK

// Little-endian scalar loads from an already-bounds-checked offset.
[[nodiscard]] std::uint32_t load_u32(std::span<const std::uint8_t> m, std::size_t off) {
    return static_cast<std::uint32_t>(m[off]) | (static_cast<std::uint32_t>(m[off + 1]) << 8) |
           (static_cast<std::uint32_t>(m[off + 2]) << 16) |
           (static_cast<std::uint32_t>(m[off + 3]) << 24);
}
[[nodiscard]] std::uint64_t load_u64(std::span<const std::uint8_t> m, std::size_t off) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(m[off + i]) << (8 * i);
    }
    return v;
}

} // namespace

std::expected<std::vector<NodeEventRecord>, NodeEventError>
decode_node_events(std::span<const std::uint8_t> linear_memory, std::size_t node_count) {
    // (1) The header must fit.
    if (linear_memory.size() < kEventRecordsBase) {
        return std::unexpected(NodeEventError::Truncated);
    }
    // (2) Full static layout: node_count in the u32 domain, then the checked region
    // arithmetic and align_up(...,8), then the heap must fit the linear memory. Every
    // step is widened to u64 before comparison so nothing wraps.
    if (node_count > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(NodeEventError::LayoutOverflow);
    }
    const auto n = static_cast<std::uint64_t>(node_count);
    const std::uint64_t region = static_cast<std::uint64_t>(kEventHeaderBytes) +
                                 n * static_cast<std::uint64_t>(kEventRecordBytes);
    const std::uint64_t unaligned = static_cast<std::uint64_t>(kEventLogBase) + region;
    const std::uint64_t heap_base = (unaligned + 7u) & ~static_cast<std::uint64_t>(7u);
    // Anything beyond the wasm32 domain is a layout overflow (the emitter guards the
    // identical bound with a BINARY_OVERFLOW; here it is a decoder-side fail-closed).
    if (heap_base > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(NodeEventError::LayoutOverflow);
    }
    if (heap_base > linear_memory.size()) {
        return std::unexpected(NodeEventError::LayoutExceedsMemory);
    }
    // (3) Header: pad[4..7] == 0, then event_count <= node_count.
    for (std::size_t i = 4; i < kEventHeaderBytes; ++i) {
        if (linear_memory[kEventLogBase + i] != 0) {
            return std::unexpected(NodeEventError::BadHeaderPad);
        }
    }
    const std::uint32_t event_count = load_u32(linear_memory, kEventLogBase);
    if (event_count > node_count) {
        return std::unexpected(NodeEventError::CountExceedsNodeCount);
    }
    // (4) Each published record in index order. The whole [records_base,
    // records_base + event_count*40) lies inside the heap_base region proven above,
    // so every load below is in-bounds; unpublished slots and any bytes at/after the
    // published region are ignored (no trailing-bytes rule).
    std::vector<NodeEventRecord> records;
    records.reserve(event_count);
    for (std::uint32_t i = 0; i < event_count; ++i) {
        const std::size_t base =
            static_cast<std::size_t>(kEventRecordsBase) + static_cast<std::size_t>(i) *
                                                              static_cast<std::size_t>(
                                                                  kEventRecordBytes);
        const std::uint8_t tag = linear_memory[base + 0];
        if (tag != kTagIdentity && tag != kTagCapability) {
            return std::unexpected(NodeEventError::BadTag);
        }
        if (linear_memory[base + 1] != 0 || linear_memory[base + 2] != 0 ||
            linear_memory[base + 3] != 0) {
            return std::unexpected(NodeEventError::BadRecordPad);
        }
        for (std::size_t r = 36; r < 40; ++r) {
            if (linear_memory[base + r] != 0) {
                return std::unexpected(NodeEventError::BadReserved);
            }
        }
        const std::uint32_t workflow_node_id = load_u32(linear_memory, base + 4);
        if (workflow_node_id == ir::core::CoreWorkflowNodeId::kInvalid) {
            return std::unexpected(NodeEventError::InvalidWorkflowNodeId);
        }
        const std::uint32_t schedule_pos = load_u32(linear_memory, base + 8);
        if (schedule_pos != i) {
            return std::unexpected(NodeEventError::SchedulePosMismatch);
        }
        const std::uint32_t capability = load_u32(linear_memory, base + 12);
        const std::uint64_t source_symbol = load_u64(linear_memory, base + 16);
        const std::uint64_t invocation_ordinal = load_u64(linear_memory, base + 24);
        const std::uint32_t status = load_u32(linear_memory, base + 32);
        if (status != kStatusOk) {
            return std::unexpected(NodeEventError::StatusNotOk);
        }
        if (tag == kTagIdentity) {
            if (capability != 0 || source_symbol != 0 || invocation_ordinal != 0) {
                return std::unexpected(NodeEventError::IdentityFieldsNonZero);
            }
        } else { // kTagCapability
            if (capability == ir::core::CoreCapabilityId::kInvalid) {
                return std::unexpected(NodeEventError::InvalidCapabilityId);
            }
        }
        NodeEventRecord record;
        record.kind = (tag == kTagCapability) ? core_wasm_resume::NodeKind::Capability
                                              : core_wasm_resume::NodeKind::Identity;
        record.workflow_node_id = ir::core::CoreWorkflowNodeId{workflow_node_id};
        record.schedule_pos = core_wasm_schema_module::ManifestNodeIndex{schedule_pos};
        record.capability = ir::core::CoreCapabilityId{capability};
        record.source_symbol = source_symbol;
        record.invocation_ordinal = core_wasm_resume::InvocationOrdinal{invocation_ordinal};
        record.status = status;
        records.push_back(record);
    }
    return records;
}

} // namespace ahfl::runtime::core_wasm_node_events
