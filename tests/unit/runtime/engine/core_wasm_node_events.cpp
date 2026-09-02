// RFC 0026 KR6.5 E4-B2-D1a-3 permanent regression for the runtime-owned node-event
// decoder. Hand-rolled check()/main(). The 88-byte golden buffer is CONTRACT-DERIVED
// from the wire grammar + the committed capability-workflow fixture semantics (NOT
// captured from the emitter / Node output, and NOT produced by the decoder under
// test): header event_count = 2, pad = 0; slot 0 = Capability(tag 1, node 0, sched
// 0, cap 0, source_symbol 1, ordinal 0, status OK); slot 1 = Identity(tag 0, node 1,
// sched 1, all zero, status OK); every pad / reserved byte is 0. The SAME hex is
// asserted byte-for-byte against real Node-executed memory[1024:1112] in
// tests/scripts/wasm_workflow_cap_node_host.py, forming a common-KAT bridge between
// the emitter/Node bytes and this decoder. FOUNDATION: framing only; NOT no-reinvoke,
// NOT B2-E, NOT a VM / durable resume.

#include "runtime/engine/core_wasm_node_events.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace ahfl::runtime::core_wasm_node_events;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::core_wasm_resume::NodeKind;
using ahfl::runtime::core_wasm_schema_module::ManifestNodeIndex;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

constexpr std::uint32_t kEventLogBase = 1024;
constexpr std::uint32_t kEventRecordsBase = 1032;
constexpr std::uint32_t kRecordBytes = 40;
constexpr std::size_t kPageBytes = 65536;

// The contract-derived 88-byte golden (memory[1024:1112]) as an exact hex literal.
// 176 hex chars. Byte-for-byte identical to the Node-script hardcoded golden.
constexpr std::string_view kGoldenHex =
    "02000000000000000100000000000000000000000000000001000000000000000000000000000000"
    "00000000000000000000000001000000010000000000000000000000000000000000000000000000"
    "0000000000000000";
static_assert(kGoldenHex.size() == 176, "golden hex is exactly 88 bytes (176 hex chars)");

[[nodiscard]] std::vector<std::uint8_t> from_hex(std::string_view hex) {
    std::vector<std::uint8_t> out;
    auto nib = [](char c) -> int {
        return (c >= '0' && c <= '9') ? c - '0' : (c - 'a') + 10;
    };
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nib(hex[i]) << 4) | nib(hex[i + 1])));
    }
    return out;
}

// A full linear-memory image of `kPageBytes` with the golden event region placed at
// byte 1024. Bytes outside [1024,1112) are 0 unless a test overwrites them.
[[nodiscard]] std::vector<std::uint8_t> golden_memory() {
    std::vector<std::uint8_t> mem(kPageBytes, 0);
    const auto golden = from_hex(kGoldenHex);
    for (std::size_t i = 0; i < golden.size(); ++i) {
        mem[kEventLogBase + i] = golden[i];
    }
    return mem;
}

// Offset of record `i` field-relative byte within the full memory image.
[[nodiscard]] std::size_t rec_off(std::uint32_t i, std::size_t field) {
    return kEventRecordsBase + static_cast<std::size_t>(i) * kRecordBytes + field;
}

} // namespace

int main() {
    // ---- contract-derived golden shape lock -------------------------------------
    {
        const auto golden = from_hex(kGoldenHex);
        check(golden.size() == 88, "golden.size_88");
        // Header count = 2 little-endian; pad zero.
        check(golden[0] == 0x02 && golden[1] == 0 && golden[2] == 0 && golden[3] == 0 &&
                  golden[4] == 0 && golden[5] == 0 && golden[6] == 0 && golden[7] == 0,
              "golden.header_count_2_pad_0");
        // slot0 tag=1 (Capability); slot1 tag=0 (Identity).
        check(golden[8] == 0x01 && golden[48] == 0x00, "golden.slot_tags");
    }

    // ---- positive decode of the golden ------------------------------------------
    {
        const auto mem = golden_memory();
        auto r = decode_node_events(std::span<const std::uint8_t>(mem), 2);
        check(r.has_value(), "positive.decodes");
        if (r.has_value()) {
            const auto &recs = *r;
            check(recs.size() == 2, "positive.two_records");
            if (recs.size() == 2) {
                // slot0 = Capability(node 0, sched 0, cap 0, source_symbol 1, ord 0).
                check(recs[0].kind == NodeKind::Capability &&
                          recs[0].workflow_node_id == CoreWorkflowNodeId{0} &&
                          recs[0].schedule_pos == ManifestNodeIndex{0} &&
                          recs[0].capability == CoreCapabilityId{0} &&
                          recs[0].source_symbol == 1 &&
                          recs[0].invocation_ordinal == InvocationOrdinal{0} &&
                          recs[0].status == 0,
                      "positive.slot0_capability");
                // slot1 = Identity(node 1, sched 1, all zero).
                check(recs[1].kind == NodeKind::Identity &&
                          recs[1].workflow_node_id == CoreWorkflowNodeId{1} &&
                          recs[1].schedule_pos == ManifestNodeIndex{1} &&
                          recs[1].capability == CoreCapabilityId{0} &&
                          recs[1].source_symbol == 0 &&
                          recs[1].invocation_ordinal == InvocationOrdinal{0} &&
                          recs[1].status == 0,
                      "positive.slot1_identity");
            }
        }
    }

    // ---- Capability positive: source_symbol 0 legal, invocation_ordinal > 0 legal
    {
        auto mem = golden_memory();
        // slot0 capability: set source_symbol (bytes 16..23) = 0, invocation (24..31) = 5.
        for (std::size_t i = 0; i < 8; ++i) {
            mem[rec_off(0, 16 + i)] = 0;
        }
        mem[rec_off(0, 24)] = 0x05;
        auto r = decode_node_events(std::span<const std::uint8_t>(mem), 2);
        check(r.has_value() && r->size() == 2 && (*r)[0].source_symbol == 0 &&
                  (*r)[0].invocation_ordinal == InvocationOrdinal{5},
              "positive.cap_src0_ordinal_nonzero");
    }

    // ---- unpublished slot WITHIN the static region is ignored --------------------
    {
        auto mem = golden_memory();
        // node_count 2 but event_count 1; slot1 [1072:1112] filled with junk.
        mem[kEventLogBase] = 0x01; // event_count = 1
        for (std::size_t off = kEventRecordsBase + kRecordBytes;
             off < kEventRecordsBase + 2 * kRecordBytes; ++off) {
            mem[off] = 0xff;
        }
        auto r = decode_node_events(std::span<const std::uint8_t>(mem), 2);
        check(r.has_value() && r->size() == 1 && (*r)[0].kind == NodeKind::Capability,
              "positive.unpublished_slot_in_region_ignored");
    }

    // ---- arbitrary bytes at/after the heap region are ignored --------------------
    {
        auto mem = golden_memory();
        for (std::size_t off = kEventRecordsBase + 2 * kRecordBytes; off < mem.size(); ++off) {
            mem[off] = static_cast<std::uint8_t>(0xa5);
        }
        auto r = decode_node_events(std::span<const std::uint8_t>(mem), 2);
        check(r.has_value() && r->size() == 2, "positive.tail_bytes_ignored");
    }

    // ---- layout / capacity boundaries -------------------------------------------
    {
        // 1612 nodes fit in a 65536-byte page (heap_base 65512); 1613 do not (65552).
        std::vector<std::uint8_t> page(kPageBytes, 0); // event_count 0
        auto ok = decode_node_events(std::span<const std::uint8_t>(page), 1612);
        check(ok.has_value() && ok->empty(), "layout.1612_fits_65536");
        auto bad = decode_node_events(std::span<const std::uint8_t>(page), 1613);
        check(!bad.has_value() && bad.error() == NodeEventError::LayoutExceedsMemory,
              "layout.1613_exceeds_65536");
    }
    {
        // Truncated: memory too small to hold the header.
        std::vector<std::uint8_t> tiny(1031, 0);
        auto r = decode_node_events(std::span<const std::uint8_t>(tiny), 0);
        check(!r.has_value() && r.error() == NodeEventError::Truncated, "layout.truncated");
    }
    {
        // A node_count still inside the size_t/u32 domain whose checked region math
        // (1032 + N*40) exceeds UINT32_MAX -> LayoutOverflow. Portable: the threshold
        // is derived from the u32 domain, so it does not depend on sizeof(size_t).
        std::vector<std::uint8_t> page(kPageBytes, 0);
        const std::size_t over =
            (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1032u) / 40u +
            1u;
        auto r = decode_node_events(std::span<const std::uint8_t>(page), over);
        check(!r.has_value() && r.error() == NodeEventError::LayoutOverflow,
              "layout.region_math_overflow");
    }

    // ---- shared layout authority (event_region_heap_base) ------------------------
    // Layout-pure: takes only node_count, renders NO capacity verdict, and is the
    // single SSOT the decoder (and future D1b) call.
    static_assert(noexcept(event_region_heap_base(std::size_t{0})),
                  "event_region_heap_base must be noexcept");
    {
        // Pure-formula boundaries. node_count 0 -> 1032 (event_bytes 8); this is a
        // formula vector, NOT a no-capability emitter claim. 2 -> 1112 (common-KAT).
        auto z = event_region_heap_base(0);
        check(z.has_value() && *z == 1032, "authority.n0_1032");
        auto one = event_region_heap_base(1);
        check(one.has_value() && *one == 1072, "authority.n1_1072");
        auto two = event_region_heap_base(2);
        check(two.has_value() && *two == 1112, "authority.n2_1112");
        // 1612 fits a page (65512); 1613 SUCCEEDS at 65552 — the helper renders NO
        // capacity verdict, so a heap_base past one page is returned normally.
        auto fit = event_region_heap_base(1612);
        check(fit.has_value() && *fit == 65512, "authority.n1612_65512");
        auto past = event_region_heap_base(1613);
        check(past.has_value() && *past == 65552, "authority.n1613_65552_no_capacity_verdict");
    }
    {
        // max_fit = largest node_count whose region/align stays in the u32 domain.
        // max_fit succeeds; max_fit+1 is a layout Overflow (region math leaves u32).
        const std::size_t max_fit =
            (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1032u) / 40u;
        auto ok = event_region_heap_base(max_fit);
        check(ok.has_value(), "authority.max_fit_ok");
        auto over = event_region_heap_base(max_fit + 1u);
        check(!over.has_value() && over.error() == NodeEventLayoutError::Overflow,
              "authority.max_fit_plus1_overflow");
    }
    {
        // node_count-domain Overflow: only assert if size_t can represent
        // UINT32_MAX + 1 (64-bit); on a 32-bit size_t this value is unrepresentable,
        // so skip rather than construct a wrap.
        if constexpr (sizeof(std::size_t) > 4) {
            const std::size_t just_over_u32 =
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1u;
            auto r = event_region_heap_base(just_over_u32);
            check(!r.has_value() && r.error() == NodeEventLayoutError::Overflow,
                  "authority.node_count_domain_overflow");
        }
    }
    {
        // Decoder <-> authority: the decoder's LayoutExceedsMemory derives from the
        // SUPPLIED span vs the authority's heap_base (the span is the sole size
        // authority). span == heap_base proceeds; span == heap_base - 1 fails.
        auto hb = event_region_heap_base(2);
        check(hb.has_value() && *hb == 1112, "span_diff.heap_base_1112");
        if (hb.has_value()) {
            std::vector<std::uint8_t> exact(*hb, 0); // span == heap_base
            auto ok = decode_node_events(std::span<const std::uint8_t>(exact), 2);
            check(ok.has_value() && ok->empty(), "span_diff.exact_heap_base_ok");
            std::vector<std::uint8_t> minus1(*hb - 1, 0); // span == heap_base - 1
            auto bad = decode_node_events(std::span<const std::uint8_t>(minus1), 2);
            check(!bad.has_value() && bad.error() == NodeEventError::LayoutExceedsMemory,
                  "span_diff.heap_base_minus1_exceeds");
        }
    }
    {
        // Decoder-vs-helper sweep: for selected node_counts, prove the decoder (over a
        // zeroed one-page span) agrees with the shared authority on all three
        // branches: helper Overflow -> decoder LayoutOverflow; helper Ok with
        // heap_base > page -> decoder LayoutExceedsMemory; helper Ok with heap_base <=
        // page -> decoder success + empty records.
        const std::size_t max_fit =
            (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1032u) / 40u;
        const std::size_t counts[] = {0u,   1u,       2u,          800u,
                                      1612u, 1613u,   max_fit,     max_fit + 1u};
        std::vector<std::uint8_t> page(kPageBytes, 0); // event_count 0
        for (const std::size_t nc : counts) {
            const auto lay = event_region_heap_base(nc);
            const auto dec = decode_node_events(std::span<const std::uint8_t>(page), nc);
            if (!lay.has_value()) {
                check(!dec.has_value() && dec.error() == NodeEventError::LayoutOverflow,
                      "sweep.overflow_maps_layout_overflow");
            } else if (*lay > kPageBytes) {
                check(!dec.has_value() && dec.error() == NodeEventError::LayoutExceedsMemory,
                      "sweep.past_page_exceeds_memory");
            } else {
                check(dec.has_value() && dec->empty(), "sweep.fits_decodes_empty");
            }
        }
    }

    // ---- one-field tamper matrix (each asserts the exact no-echo error) ----------
    auto expect_error = [](std::vector<std::uint8_t> mem, std::size_t node_count,
                           NodeEventError want, std::string_view name) {
        auto r = decode_node_events(std::span<const std::uint8_t>(mem), node_count);
        check(!r.has_value() && r.error() == want, name);
    };
    {
        auto mem = golden_memory();
        mem[kEventLogBase + 4] = 0x01; // header pad nonzero
        expect_error(mem, 2, NodeEventError::BadHeaderPad, "tamper.header_pad");
    }
    {
        auto mem = golden_memory();
        mem[kEventLogBase] = 0x03; // event_count 3 > node_count 2
        expect_error(mem, 2, NodeEventError::CountExceedsNodeCount, "tamper.count_exceeds");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(0, 0)] = 0x02; // tag 2
        expect_error(mem, 2, NodeEventError::BadTag, "tamper.bad_tag");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(0, 1)] = 0x01; // record pad[1..3] nonzero
        expect_error(mem, 2, NodeEventError::BadRecordPad, "tamper.record_pad");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(0, 36)] = 0x01; // reserved nonzero
        expect_error(mem, 2, NodeEventError::BadReserved, "tamper.reserved");
    }
    {
        auto mem = golden_memory();
        for (std::size_t i = 0; i < 4; ++i) {
            mem[rec_off(0, 4 + i)] = 0xff; // workflow_node_id = UINT32_MAX
        }
        expect_error(mem, 2, NodeEventError::InvalidWorkflowNodeId, "tamper.invalid_node_id");
    }
    {
        auto mem = golden_memory();
        for (std::size_t i = 0; i < 4; ++i) {
            mem[rec_off(0, 12 + i)] = 0xff; // capability = UINT32_MAX on a tag1 record
        }
        expect_error(mem, 2, NodeEventError::InvalidCapabilityId, "tamper.invalid_capability");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(0, 8)] = 0x07; // schedule_pos 7 != index 0
        expect_error(mem, 2, NodeEventError::SchedulePosMismatch, "tamper.schedule_pos");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(1, 12)] = 0x01; // identity slot1 capability != 0
        expect_error(mem, 2, NodeEventError::IdentityFieldsNonZero, "tamper.identity_cap");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(1, 16)] = 0x01; // identity slot1 source_symbol != 0
        expect_error(mem, 2, NodeEventError::IdentityFieldsNonZero, "tamper.identity_src");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(1, 24)] = 0x01; // identity slot1 invocation_ordinal != 0
        expect_error(mem, 2, NodeEventError::IdentityFieldsNonZero, "tamper.identity_ordinal");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(0, 32)] = 0x01; // capability slot0 status ERROR
        expect_error(mem, 2, NodeEventError::StatusNotOk, "tamper.status_not_ok_capability");
    }
    {
        auto mem = golden_memory();
        mem[rec_off(1, 32)] = 0x01; // identity slot1 status ERROR
        expect_error(mem, 2, NodeEventError::StatusNotOk, "tamper.status_not_ok_identity");
    }

    // ---- fail-order priority witnesses (4, no combinatorics) --------------------
    // Each stacks two violations from DIFFERENT stages and asserts the EARLIER stage
    // wins, locking the documented order: header-min -> static layout -> header
    // pad/count -> per-record (tag first).
    {
        // span<1032 AND an overflow node_count -> Truncated (header-min beats layout).
        std::vector<std::uint8_t> tiny(1000, 0);
        const std::size_t over =
            (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1032u) / 40u +
            1u;
        expect_error(tiny, over, NodeEventError::Truncated, "priority.truncated_beats_layout");
    }
    {
        // valid-size span AND arithmetic-overflow node_count AND bad header pad ->
        // LayoutOverflow (static layout beats header pad).
        auto mem = golden_memory();
        mem[kEventLogBase + 4] = 0x01; // header pad nonzero (a later-stage violation)
        const std::size_t over =
            (static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 1032u) / 40u +
            1u;
        expect_error(mem, over, NodeEventError::LayoutOverflow, "priority.layout_beats_headerpad");
    }
    {
        // valid layout AND bad header pad AND event_count>node_count -> BadHeaderPad
        // (header pad beats count).
        auto mem = golden_memory();
        mem[kEventLogBase + 4] = 0x01; // header pad nonzero
        mem[kEventLogBase] = 0x05;     // event_count 5 > node_count 2
        expect_error(mem, 2, NodeEventError::BadHeaderPad, "priority.headerpad_beats_count");
    }
    {
        // first published record has BOTH a bad tag AND bad record pad/reserved ->
        // BadTag (tag is the first per-record gate).
        auto mem = golden_memory();
        mem[rec_off(0, 0)] = 0x02; // bad tag
        mem[rec_off(0, 1)] = 0x01; // bad record pad (later per-record gate)
        mem[rec_off(0, 36)] = 0x01; // bad reserved (later per-record gate)
        expect_error(mem, 2, NodeEventError::BadTag, "priority.badtag_first_record_gate");
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_node_events: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_node_events: " << g_failures << " failure(s)\n";
    return 1;
}
