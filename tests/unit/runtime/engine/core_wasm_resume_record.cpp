#include "runtime/engine/core_wasm_resume_record.hpp"

#include "ahfl/compiler/ir/core_wire_schema.hpp" // wire_schema::kInvalid
#include "base/support/sha256.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// RFC 0026 KR6.5 E4-B2-A1 permanent regression for the durable-resume control
// record codec (`ahfl.wasm-resume.v1`). Hand-rolled check()/main() to match the
// runtime-engine test style. Coverage:
//   * golden round-trip (encode -> decode equals model) + a fixed HMAC KAT;
//   * per-field / per-tag tamper (magic, version, alg, key_id, tag, digest,
//     trailing, node/memo bytes) all fail closed;
//   * wrong key + wrong key_id rejected;
//   * non-shortest LEB + trailing bytes rejected;
//   * count-before-reserve (attacker node/memo count on a short payload);
//   * full Suspended/Injected + dense/unique/frontier/sentinel invariants;
//   * encoder-reject / decoder-reject symmetry for out-of-set enums + bad digests;
//   * diagnostics never echo a payload byte, key, or digest (no-echo).
// First production caller of this codec remains the future B2-D host; this test is
// the only caller today.

namespace {

using namespace ahfl::runtime::core_wasm_resume;
using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

// A fixed 32-byte test key. Not a production key; only exercises the HMAC path.
std::vector<std::uint8_t> test_key() { return std::vector<std::uint8_t>(32, 0x2b); }

std::array<std::uint8_t, 16> test_key_id() {
    std::array<std::uint8_t, 16> id{};
    for (std::size_t i = 0; i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>(i + 1);
    }
    return id;
}

DigestHex hex_of(char fill) {
    DigestHex d{};
    d.fill(fill);
    return d;
}

// A minimal well-formed Suspended record: 2 nodes (identity #0, capability
// frontier #1 with one committed memo + a pending on the next ordinal).
CoreWasmResumeRecord make_suspended() {
    CoreWasmResumeRecord r;
    r.format_version = 1;
    r.guarantees = 0;
    r.module_sha256 = hex_of('a');
    r.wire_schema_sha256 = hex_of('b');
    r.exec_manifest_sha256 = hex_of('c');
    r.entry_kind = EntryKind::Workflow;
    r.entry_id = CoreWorkflowId{7};
    r.entry_input_slot = PayloadSlotId{9}; // distinct from every memo.result_slot
    r.suspended_node_id = CoreWorkflowNodeId{41};
    r.resume_state = ResumeState::Suspended;

    ResumeNode n0;
    n0.workflow_node_id = CoreWorkflowNodeId{40};
    n0.schedule_pos = 0;
    n0.node_kind = NodeKind::Identity;

    ResumeNode n1;
    n1.workflow_node_id = CoreWorkflowNodeId{41};
    n1.schedule_pos = 1;
    n1.node_kind = NodeKind::Capability;
    ResumeMemoEntry m0;
    m0.invocation_ordinal = InvocationOrdinal{0};
    m0.capability = CoreCapabilityId{0}; // 0 is legal
    m0.source_symbol = 0;                 // 0 is legal
    m0.arg_hash = 0x1122334455667788ULL;
    m0.result_slot = PayloadSlotId{5};
    n1.memo.push_back(m0);
    ResumePendingEntry p;
    p.invocation_ordinal = InvocationOrdinal{1}; // == frontier.memo.size()
    p.capability = CoreCapabilityId{3};
    p.source_symbol = 900;
    p.arg_hash = 0xdeadbeefULL;
    n1.pending = p;

    r.nodes.push_back(n0);
    r.nodes.push_back(n1);
    r.auth_header.alg_version = 1;
    r.auth_header.key_id = test_key_id();
    r.auth_header.generation = 12345;
    return r;
}

// An Injected variant: no pending; frontier memo non-empty.
CoreWasmResumeRecord make_injected() {
    CoreWasmResumeRecord r = make_suspended();
    r.resume_state = ResumeState::Injected;
    // Append the injected ordinal as the last committed memo entry and drop pending.
    ResumeMemoEntry m1;
    m1.invocation_ordinal = InvocationOrdinal{1};
    m1.capability = CoreCapabilityId{3};
    m1.source_symbol = 900;
    m1.arg_hash = 0xdeadbeefULL;
    m1.result_slot = PayloadSlotId{6};
    r.nodes.back().memo.push_back(m1);
    r.nodes.back().pending.reset();
    return r;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode(const CoreWasmResumeRecord &r) {
    auto enc = encode_and_authenticate(r, test_key());
    if (!enc.ok()) {
        return std::nullopt;
    }
    return enc.bytes;
}

bool decode_ok(const std::vector<std::uint8_t> &bytes) {
    const auto key_id = test_key_id();
    auto dec = decode_and_authenticate(bytes, std::span<const std::uint8_t, 16>(key_id),
                                       test_key());
    return dec.ok();
}

// Decode expecting failure. Also asserts the hard diagnostic contract on EVERY
// failure path: the record is absent, at least one Error is present, and every
// diagnostic uses the fixed wire_schema::kInvalid code with a null source range
// (no per-call catalogue drift, no source-range fabrication).
bool decode_fails(const std::vector<std::uint8_t> &bytes) {
    const auto key_id = test_key_id();
    auto dec = decode_and_authenticate(bytes, std::span<const std::uint8_t, 16>(key_id),
                                       test_key());
    if (dec.ok() || dec.record.has_value() || !dec.has_errors()) {
        return false;
    }
    for (const auto &d : dec.diagnostics) {
        if (d.code != std::string(ahfl::ir::core::wire_schema::kInvalid)) {
            return false;
        }
        if (d.source_range.has_value()) {
            return false;
        }
    }
    return true;
}

// Canonical ULEB writer (test-only), mirroring the codec's discipline so we can
// hand-build an authenticated body with an ATTACKER-inflated count and confirm the
// decoder's count-before-reserve gate rejects it without a giant allocation.
void put_uleb(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto b = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            b |= 0x80U;
        }
        out.push_back(b);
    } while (value != 0);
}

// Build a body whose declared node_count is `node_count` but which then stops, sign
// it with the real key, and return the full authenticated record. The decoder must
// reject on the min-entry bound BEFORE reserving `node_count` node slots.
std::vector<std::uint8_t> forged_inflated_node_count(std::uint32_t node_count) {
    std::vector<std::uint8_t> body;
    const char magic[6] = {'A', 'H', 'F', 'L', 'W', 'R'};
    for (char c : magic) {
        body.push_back(static_cast<std::uint8_t>(c));
    }
    body.push_back(1);       // format_version
    put_uleb(body, 0);       // guarantees
    for (int d = 0; d < 3; ++d) {
        for (int i = 0; i < 64; ++i) {
            body.push_back(static_cast<std::uint8_t>('a')); // valid lowercase-hex digest
        }
    }
    body.push_back(0);       // entry_kind = Workflow
    put_uleb(body, 7);       // entry_id
    put_uleb(body, 9);       // entry_input_slot
    put_uleb(body, 41);      // suspended_node_id
    body.push_back(0);       // resume_state = Suspended
    put_uleb(body, node_count); // attacker-inflated count; body then STOPS
    // auth_header
    body.push_back(1);       // alg_version
    const auto key_id = test_key_id();
    for (std::uint8_t b : key_id) {
        body.push_back(b);
    }
    for (int i = 0; i < 8; ++i) {
        body.push_back(0);   // generation = 0
    }
    // sign the exact prefix [0,tag)
    const auto tag = ahfl::support::hmac_sha256(test_key(), body);
    body.insert(body.end(), tag.begin(), tag.end());
    return body;
}

// Assemble a body from a caller-provided "body-core" (everything from magic through
// the nodes), append the standard auth_header, HMAC-sign the exact prefix, and
// append the tag. This lets a test inject a decoder-side violation (bad enum,
// non-hex digest, trailing byte, ...) into an otherwise HMAC-VALID record, so the
// decoder's pass-2 gates are genuinely exercised (not just the encoder).
std::vector<std::uint8_t> sign_body_core(std::vector<std::uint8_t> core) {
    core.push_back(1); // alg_version
    const auto key_id = test_key_id();
    for (std::uint8_t b : key_id) {
        core.push_back(b);
    }
    for (int i = 0; i < 8; ++i) {
        core.push_back(0); // generation = 0
    }
    const auto tag = ahfl::support::hmac_sha256(test_key(), core);
    core.insert(core.end(), tag.begin(), tag.end());
    return core;
}

// A well-formed single-node Injected body-core (magic..nodes), suitable as a base
// for decoder-side mutation. One capability frontier node with one committed memo.
std::vector<std::uint8_t> injected_single_node_core(char digest_fill = 'a',
                                                    std::uint8_t entry_kind = 0,
                                                    std::uint8_t resume_state = 1,
                                                    std::uint8_t node_kind = 1,
                                                    std::uint32_t guarantees = 0) {
    std::vector<std::uint8_t> b;
    const char magic[6] = {'A', 'H', 'F', 'L', 'W', 'R'};
    for (char c : magic) {
        b.push_back(static_cast<std::uint8_t>(c));
    }
    b.push_back(1);              // format_version
    put_uleb(b, guarantees);     // guarantees
    for (int d = 0; d < 3; ++d) {
        for (int i = 0; i < 64; ++i) {
            b.push_back(static_cast<std::uint8_t>(digest_fill));
        }
    }
    b.push_back(entry_kind);     // entry_kind
    put_uleb(b, 7);              // entry_id
    put_uleb(b, 9);              // entry_input_slot (distinct from memo result_slot 6)
    put_uleb(b, 41);             // suspended_node_id
    b.push_back(resume_state);   // resume_state
    put_uleb(b, 1);              // node_count = 1
    // node 0 (== frontier)
    put_uleb(b, 41);             // workflow_node_id (== suspended)
    put_uleb(b, 0);              // schedule_pos == index 0
    b.push_back(node_kind);      // node_kind
    put_uleb(b, 1);              // memo_count = 1
    put_uleb(b, 0);              // memo[0].invocation_ordinal
    put_uleb(b, 3);              // memo[0].capability
    put_uleb(b, 900);            // memo[0].source_symbol
    put_uleb(b, 0xdead);         // memo[0].arg_hash
    put_uleb(b, 6);              // memo[0].result_slot
    return b;
}

} // namespace

int main() {
    // ---- golden round-trip: Suspended + Injected ----
    {
        const auto r = make_suspended();
        auto bytes = encode(r);
        check(bytes.has_value(), "suspended.encode_ok");
        if (bytes) {
            const auto key_id = test_key_id();
            auto dec = decode_and_authenticate(*bytes, std::span<const std::uint8_t, 16>(key_id),
                                               test_key());
            check(dec.ok(), "suspended.decode_ok");
            check(dec.record.has_value() && *dec.record == r, "suspended.round_trip_equal");
        }
    }
    {
        const auto r = make_injected();
        auto bytes = encode(r);
        check(bytes.has_value(), "injected.encode_ok");
        if (bytes) {
            const auto key_id = test_key_id();
            auto dec = decode_and_authenticate(*bytes, std::span<const std::uint8_t, 16>(key_id),
                                               test_key());
            check(dec.ok() && dec.record.has_value() && *dec.record == r,
                  "injected.round_trip_equal");
        }
    }

    // ---- external HMAC KAT: the golden record bytes for make_suspended() are
    //      fixed and independently cross-checked. The 293-byte record and its
    //      32-byte tail tag were verified with BOTH python hmac.new(key, prefix,
    //      sha256) AND `openssl dgst -sha256 -mac HMAC -macopt hexkey:<2b*32>` over
    //      the exact 261-byte prefix; both produced the tag 76fd6bf6...4e4e97bf.
    //      Reproduce: encode make_suspended() with a 32-byte 0x2b key, then
    //      `openssl dgst -sha256 -mac HMAC -macopt hexkey:$(python3 -c "print('2b'*32)")`
    //      over bytes [0, len-32). This golden is the D1a `entry_input_slot`-bearing
    //      grammar; the pre-D1a golden is permanently rejected below. ----
    {
        static constexpr std::string_view kGoldenHex =
            "4148464c5752010061616161616161616161616161616161616161616161616161616161"
            "616161616161616161616161616161616161616161616161616161616161616161616161"
            "626262626262626262626262626262626262626262626262626262626262626262626262"
            "626262626262626262626262626262626262626262626262626262626363636363636363"
            "636363636363636363636363636363636363636363636363636363636363636363636363"
            "63636363636363636363636363636363636363630007092900022800000029010101"
            "00000088ef99abc5e88c91110501038407effdb6f50d010102030405060708090a0b0c0d"
            "0e0f10393000000000000076fd6bf6607c78762b26a4cd4ad946cafd99826ae2b2c296b1"
            "9237694e4e97bf";
        std::vector<std::uint8_t> golden;
        for (std::size_t i = 0; i + 1 < kGoldenHex.size(); i += 2) {
            const auto hi = kGoldenHex[i];
            const auto lo = kGoldenHex[i + 1];
            auto nib = [](char c) -> int {
                return (c >= '0' && c <= '9') ? c - '0' : (c - 'a') + 10;
            };
            golden.push_back(static_cast<std::uint8_t>((nib(hi) << 4) | nib(lo)));
        }
        // Encoder produces exactly the externally-verified golden bytes.
        auto produced = encode(make_suspended());
        check(produced.has_value() && *produced == golden, "kat.encoder_matches_golden_bytes");
        // Decoder admits the externally-verified golden bytes.
        check(decode_ok(golden), "kat.decoder_admits_golden_bytes");
        // The last 32 bytes are the externally-cross-checked tag.
        check(golden.size() == 293, "kat.golden_length");
    }

    // ---- permanent pre-D1a golden REJECT: the ORIGINAL frozen 292-byte,
    //      HMAC-VALID pre-D1a make_suspended() record (no `entry_input_slot` field)
    //      must be rejected by the D1a decoder. This is a lineage gate proving the
    //      new decoder's fixed field order is the admission authority: the frozen
    //      bytes below were a VALID record under the pre-D1a grammar (entry_id 7
    //      immediately followed by suspended_node_id 41), and are kept verbatim as a
    //      hardcoded constant -- NOT re-signed here -- so the tested HMAC path never
    //      participates in this evidence. Because the D1a decoder reads a ULEB
    //      `entry_input_slot` where this stream has `suspended_node_id`, the
    //      downstream fields desynchronize and admission fails closed. ----
    {
        static constexpr std::string_view kPreD1aGoldenHex =
            "4148464c5752010061616161616161616161616161616161616161616161616161616161"
            "616161616161616161616161616161616161616161616161616161616161616161616161"
            "626262626262626262626262626262626262626262626262626262626262626262626262"
            "626262626262626262626262626262626262626262626262626262626363636363636363"
            "636363636363636363636363636363636363636363636363636363636363636363636363"
            "636363636363636363636363636363636363636300072900022800000029010101000000"
            "88ef99abc5e88c91110501038407effdb6f50d010102030405060708090a0b0c0d0e0f10"
            "3930000000000000ded6367ee4494cfef20304310671abd158f0ddfd9abf8255d929a15c"
            "fb041068";
        std::vector<std::uint8_t> pre_d1a;
        for (std::size_t i = 0; i + 1 < kPreD1aGoldenHex.size(); i += 2) {
            const auto hi = kPreD1aGoldenHex[i];
            const auto lo = kPreD1aGoldenHex[i + 1];
            auto nib = [](char c) -> int {
                return (c >= '0' && c <= '9') ? c - '0' : (c - 'a') + 10;
            };
            pre_d1a.push_back(static_cast<std::uint8_t>((nib(hi) << 4) | nib(lo)));
        }
        check(pre_d1a.size() == 292, "lineage.pre_d1a_golden_length");
        check(!decode_ok(pre_d1a), "lineage.pre_d1a_golden_is_rejected");
    }

    // ---- wrong key / wrong key_id ----
    {
        auto bytes = encode(make_suspended());
        check(bytes.has_value(), "wrongkey.encode_ok");
        if (bytes) {
            const auto key_id = test_key_id();
            std::vector<std::uint8_t> bad_key(32, 0x2c); // one bit off
            auto dec = decode_and_authenticate(*bytes, std::span<const std::uint8_t, 16>(key_id),
                                               bad_key);
            check(!dec.ok() && dec.has_errors(), "wrongkey.rejected");

            std::array<std::uint8_t, 16> bad_id = test_key_id();
            bad_id[0] ^= 0xff;
            auto dec2 = decode_and_authenticate(*bytes, std::span<const std::uint8_t, 16>(bad_id),
                                                test_key());
            check(!dec2.ok() && dec2.has_errors(), "wrongkeyid.rejected");
        }
    }

    // ---- single-byte tamper across framing + tail fields (magic, version, a body
    //      digest byte, alg, key_id, generation, every tag byte, trailing,
    //      truncated) all fail closed ----
    {
        auto bytes = encode(make_suspended());
        check(bytes.has_value(), "tamper.encode_ok");
        if (bytes) {
            // magic
            {
                auto b = *bytes;
                b[0] ^= 0xff;
                check(decode_fails(b), "tamper.magic");
            }
            // format_version
            {
                auto b = *bytes;
                b[6] = 2;
                check(decode_fails(b), "tamper.version");
            }
            // a digest byte (in body -> breaks HMAC)
            {
                auto b = *bytes;
                b[10] ^= 0x20; // flip a hex-region byte
                check(decode_fails(b), "tamper.digest_byte");
            }
            // entry_input_slot wire byte (in body -> breaks HMAC). Offset:
            // magic(6)+fmt(1)+guarantees(1)+3*64 digests(192)+entry_kind(1)+
            // entry_id(1,=7) = 202, so byte[202] is the entry_input_slot ULEB (value
            // 9, single byte). Assert the position first so fixture drift cannot
            // silently flip the wrong byte, then tamper it.
            {
                auto b = *bytes;
                check(b.size() > 202 && b[202] == 0x09, "tamper.entry_input_slot_offset");
                if (b.size() > 202 && b[202] == 0x09) {
                    b[202] = 0x0a; // change entry_input_slot 9 -> 10 (still 1 ULEB byte)
                    check(decode_fails(b), "tamper.entry_input_slot");
                }
            }
            // alg_version in the tail
            {
                auto b = *bytes;
                b[b.size() - 57] = 0xff;
                check(decode_fails(b), "tamper.alg");
            }
            // key_id byte in the tail
            {
                auto b = *bytes;
                b[b.size() - 57 + 1] ^= 0xff;
                check(decode_fails(b), "tamper.keyid_byte");
            }
            // generation byte in the tail
            {
                auto b = *bytes;
                b[b.size() - 57 + 1 + 16] ^= 0xff;
                check(decode_fails(b), "tamper.generation_byte");
            }
            // every tag byte flipped independently must fail (constant-time path)
            {
                bool all_reject = true;
                for (std::size_t i = 0; i < 32; ++i) {
                    auto b = *bytes;
                    b[b.size() - 32 + i] ^= 0xff;
                    if (decode_ok(b)) {
                        all_reject = false;
                    }
                }
                check(all_reject, "tamper.every_tag_byte");
            }
            // trailing byte after the tag
            {
                auto b = *bytes;
                b.push_back(0x00);
                check(decode_fails(b), "tamper.trailing_after_tag");
            }
            // truncated (drop the last byte)
            {
                auto b = *bytes;
                b.pop_back();
                check(decode_fails(b), "tamper.truncated");
            }
        }
    }

    // ---- too-short input never reads a count / never allocates ----
    {
        std::vector<std::uint8_t> tiny(10, 0x00);
        check(decode_fails(tiny), "short.min_framing");
        std::vector<std::uint8_t> empty;
        check(decode_fails(empty), "short.empty");
    }

    // ---- count-before-reserve: an AUTHENTICATED body with an attacker-inflated
    //      node_count on a short payload must be rejected on the min-entry bound,
    //      never a giant reserve (ASan/UBSan would catch a bad alloc/overflow). ----
    {
        auto forged = forged_inflated_node_count(0xFFFFFFF0U);
        check(decode_fails(forged), "count.inflated_node_count_rejected");
        // Also a moderately large but still-impossible count.
        auto forged2 = forged_inflated_node_count(1000000U);
        check(decode_fails(forged2), "count.large_node_count_rejected");
        // A count of 0 is also invalid (nodes non-empty) and must fail closed.
        auto forged0 = forged_inflated_node_count(0U);
        check(decode_fails(forged0), "count.zero_node_count_rejected");
    }

    // ---- non-shortest (overlong) LEB rejected by the canonical re-encode gate.
    //      Take a fully-valid record, drop its tag, turn the canonical single-byte
    //      guarantees `0x00` at body offset 7 into an overlong-zero ULEB
    //      `0x80 0x00`, re-HMAC the new prefix, and append the tag. The record now
    //      authenticates and PARSES (value 0), passes every invariant, and is
    //      rejected ONLY by the canonical body byte-equality gate (the reader is
    //      memory-safe and does not itself judge shortest-form). This is the gate
    //      the min-entry bound cannot pre-empt. ----
    {
        auto bytes = encode(make_suspended());
        check(bytes.has_value(), "noncanon.encode_ok");
        if (bytes) {
            auto b = *bytes;
            b.resize(b.size() - 32); // strip the old tag; tail is now the auth_header
            // body offset 6 = format_version, offset 7 = guarantees (canonical 0x00).
            check(b[7] == 0x00, "noncanon.guarantees_is_canonical_zero");
            b[7] = 0x80;                       // continuation bit set...
            b.insert(b.begin() + 8, 0x00);     // ...followed by a redundant 0x00 group
            const auto tag = ahfl::support::hmac_sha256(test_key(), b);
            b.insert(b.end(), tag.begin(), tag.end());
            check(decode_fails(b), "noncanon.overlong_guarantees_rejected");
        }
    }

    // ---- Injected with an empty frontier memo is invalid (encoder + symmetry) ----
    {
        auto r = make_injected();
        r.nodes.back().memo.clear();
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_injected_empty_frontier_memo");
    }

    // ---- DECODER-SIDE violations on HMAC-VALID forged input (pass-2 gates) ----
    {
        // Sanity: the hand-built injected single-node core decodes OK when signed.
        check(decode_ok(sign_body_core(injected_single_node_core())), "forge.baseline_decodes");

        // memo_count inflated -> min-entry gate rejects before reserve.
        {
            auto core = injected_single_node_core();
            // Rewrite the memo_count byte: it is the byte right after node_kind. We
            // rebuild a core with node_count=1 up through node_kind, then a huge
            // memo_count and stop.
            std::vector<std::uint8_t> b;
            const char magic[6] = {'A', 'H', 'F', 'L', 'W', 'R'};
            for (char c : magic) {
                b.push_back(static_cast<std::uint8_t>(c));
            }
            b.push_back(1);
            put_uleb(b, 0);
            for (int d = 0; d < 3; ++d) {
                for (int i = 0; i < 64; ++i) {
                    b.push_back(static_cast<std::uint8_t>('a'));
                }
            }
            b.push_back(0);   // entry_kind
            put_uleb(b, 7);
            put_uleb(b, 41);
            b.push_back(1);   // resume_state = Injected
            put_uleb(b, 1);   // node_count
            put_uleb(b, 41);  // node id
            put_uleb(b, 0);   // schedule_pos
            b.push_back(1);   // node_kind = capability
            put_uleb(b, 0xFFFFFFF0U); // memo_count inflated; body STOPS
            check(decode_fails(sign_body_core(b)), "forge.inflated_memo_count_rejected");
        }

        // body trailing byte AFTER a complete record body -> exact-EOF gate.
        {
            auto core = injected_single_node_core();
            core.push_back(0x00); // extra body byte before auth_header
            check(decode_fails(sign_body_core(core)), "forge.body_trailing_byte_rejected");
        }

        // out-of-set resume_state (2) on a signed record -> decoder allowed-set gate.
        check(decode_fails(sign_body_core(injected_single_node_core('a', 0, 2, 1, 0))),
              "forge.decoder_rejects_bad_resume_state");
        // out-of-set node_kind (2).
        check(decode_fails(sign_body_core(injected_single_node_core('a', 0, 1, 2, 0))),
              "forge.decoder_rejects_bad_node_kind");
        // out-of-set entry_kind (1).
        check(decode_fails(sign_body_core(injected_single_node_core('a', 1, 1, 1, 0))),
              "forge.decoder_rejects_bad_entry_kind");
        // non-hex digest (uppercase 'A').
        check(decode_fails(sign_body_core(injected_single_node_core('A', 0, 1, 1, 0))),
              "forge.decoder_rejects_nonhex_digest");
        // unknown guarantees bit (bit 2 = value 4).
        check(decode_fails(sign_body_core(injected_single_node_core('a', 0, 1, 1, 4))),
              "forge.decoder_rejects_unknown_guarantees_bit");
    }

    // ---- encoder-reject / decoder-reject symmetry: out-of-set enums + bad digest ----
    {
        // resume_state out of set
        auto r = make_suspended();
        r.resume_state = static_cast<ResumeState>(0xff);
        auto enc = encode_and_authenticate(r, test_key());
        check(!enc.ok(), "encoder.rejects_bad_resume_state");
    }
    {
        // node_kind out of set
        auto r = make_suspended();
        r.nodes.back().node_kind = static_cast<NodeKind>(0xff);
        auto enc = encode_and_authenticate(r, test_key());
        check(!enc.ok(), "encoder.rejects_bad_node_kind");
    }
    {
        // entry_kind out of set
        auto r = make_suspended();
        r.entry_kind = static_cast<EntryKind>(0xff);
        auto enc = encode_and_authenticate(r, test_key());
        check(!enc.ok(), "encoder.rejects_bad_entry_kind");
    }
    {
        // uppercase / non-hex digest field, one negative per digest field.
        auto r = make_suspended();
        r.module_sha256 = hex_of('A'); // uppercase
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_uppercase_module_digest");
        auto r2 = make_suspended();
        r2.wire_schema_sha256 = hex_of('z'); // non-hex letter
        check(!encode_and_authenticate(r2, test_key()).ok(), "encoder.rejects_nonhex_wire_digest");
        auto r3 = make_suspended();
        r3.exec_manifest_sha256 = hex_of('G'); // out-of-range letter
        check(!encode_and_authenticate(r3, test_key()).ok(), "encoder.rejects_bad_exec_digest");
    }

    // ---- structural invariant rejections (encoder side) ----
    {
        auto r = make_suspended();
        r.nodes.clear();
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_empty_nodes");
    }
    {
        auto r = make_suspended();
        r.nodes[0].schedule_pos = 5; // != index
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_schedule_pos_gap");
    }
    {
        auto r = make_suspended();
        r.nodes[0].workflow_node_id = r.nodes[1].workflow_node_id; // duplicate id
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_dup_node_id");
    }
    {
        auto r = make_suspended();
        r.entry_id = CoreWorkflowId{CoreWorkflowId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_entry_sentinel");
    }
    {
        auto r = make_suspended();
        r.nodes[1].workflow_node_id = CoreWorkflowNodeId{CoreWorkflowNodeId::kInvalid};
        r.suspended_node_id = CoreWorkflowNodeId{CoreWorkflowNodeId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_node_sentinel");
    }
    {
        auto r = make_suspended();
        r.nodes[1].memo[0].result_slot = PayloadSlotId{PayloadSlotId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_slot_sentinel");
    }
    {
        auto r = make_suspended();
        r.entry_input_slot = PayloadSlotId{PayloadSlotId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_entry_input_slot_sentinel");
    }
    {
        // entry_input_slot must be DISTINCT from every memo.result_slot.
        auto r = make_suspended();
        r.entry_input_slot = r.nodes[1].memo[0].result_slot; // collide with memo slot 5
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_entry_input_slot_equals_memo_slot");
    }
    {
        // Repeated memo references to the SAME result slot stay legal (positive), as
        // long as that slot is not the entry_input_slot.
        auto r = make_injected();
        r.nodes[1].memo[1].result_slot = r.nodes[1].memo[0].result_slot; // slot 5 twice
        check(encode_and_authenticate(r, test_key()).ok(),
              "encoder.allows_repeated_memo_result_slot");
    }
    {
        // frontier not a capability node
        auto r = make_suspended();
        r.nodes[1].node_kind = NodeKind::Identity;
        r.nodes[1].memo.clear();
        r.nodes[1].pending.reset();
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_frontier_not_capability");
    }
    {
        // WH-5b.2: an identity node carrying a memo is now A1-valid. The
        // record-level check cannot distinguish a P6 bridge node (identity
        // node_kind + in-runner bridge-site table) from a pure identity
        // node; the coordinate gate in the resume controller enforces that
        // a pure identity node has empty memo. The A1 check only verifies
        // structural invariants (dense ordinals, valid slots, etc.).
        auto r = make_suspended();
        r.nodes[0].node_kind = NodeKind::Identity;
        r.nodes[0].memo.push_back(r.nodes[1].memo[0]);
        check(encode_and_authenticate(r, test_key()).ok(),
              "encoder.allows_identity_with_memo");
    }
    {
        // Suspended pending ordinal not following memo
        auto r = make_suspended();
        r.nodes[1].pending->invocation_ordinal = InvocationOrdinal{9};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_bad_pending_ordinal");
    }
    {
        // Injected but carries a pending
        auto r = make_injected();
        ResumePendingEntry p;
        p.invocation_ordinal = InvocationOrdinal{2};
        p.capability = CoreCapabilityId{3};
        r.nodes.back().pending = p;
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_injected_with_pending");
    }
    {
        // non-frontier node carrying a pending
        auto r = make_suspended();
        ResumePendingEntry p;
        p.invocation_ordinal = InvocationOrdinal{0};
        p.capability = CoreCapabilityId{1};
        r.nodes[0].pending = p; // node 0 is not the frontier
        r.nodes[0].node_kind = NodeKind::Capability;
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_non_frontier_pending");
    }

    // ---- extra structural invariants (encoder-side, P1) ----
    {
        // Suspended missing pending on the frontier.
        auto r = make_suspended();
        r.nodes.back().pending.reset();
        check(!encode_and_authenticate(r, test_key()).ok(),
              "encoder.rejects_suspended_missing_pending");
    }
    {
        // memo ordinal not dense from zero.
        auto r = make_injected();
        r.nodes.back().memo[0].invocation_ordinal = InvocationOrdinal{5};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_memo_ordinal_gap");
    }
    {
        // memo capability sentinel.
        auto r = make_injected();
        r.nodes.back().memo[0].capability = CoreCapabilityId{CoreCapabilityId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_memo_cap_sentinel");
    }
    {
        // pending capability sentinel.
        auto r = make_suspended();
        r.nodes.back().pending->capability = CoreCapabilityId{CoreCapabilityId::kInvalid};
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_pending_cap_sentinel");
    }
    {
        // suspended_node_id not equal to the last scheduled node.
        auto r = make_suspended();
        r.suspended_node_id = r.nodes.front().workflow_node_id; // points at node 0, not the last
        check(!encode_and_authenticate(r, test_key()).ok(), "encoder.rejects_suspended_not_last");
    }

    // ---- no-echo: an explicit ASCII marker in the key must never surface in any
    //      diagnostic; every diagnostic uses the fixed wire_schema::kInvalid code
    //      and a null range; the record is absent on failure. ----
    {
        static constexpr std::string_view kMarker = "RESUME_SECRET_MARKER_ZZZ";
        auto bytes = encode(make_suspended());
        check(bytes.has_value(), "noecho.encode_ok");
        if (bytes) {
            // Force a tag failure by decoding under a WRONG key whose bytes contain
            // the marker; the key must never be echoed.
            std::vector<std::uint8_t> marked_key(kMarker.begin(), kMarker.end());
            marked_key.resize(32, 0x00);
            const auto key_id = test_key_id();
            auto dec = decode_and_authenticate(*bytes, std::span<const std::uint8_t, 16>(key_id),
                                               marked_key);
            check(!dec.ok() && !dec.record.has_value(), "noecho.record_absent_on_fail");
            check(dec.has_errors(), "noecho.has_error");
            bool all_fixed = true;
            for (const auto &d : dec.diagnostics) {
                if (d.code != std::string(ahfl::ir::core::wire_schema::kInvalid)) {
                    all_fixed = false;
                }
                if (d.source_range.has_value()) {
                    all_fixed = false;
                }
                if (d.message.find(kMarker) != std::string::npos) {
                    all_fixed = false;
                }
            }
            check(all_fixed, "noecho.fixed_code_null_range_no_marker");
        }
    }

    if (g_failures == 0) {
        std::cout << "core_wasm_resume_record: all checks passed\n";
        return 0;
    }
    std::cerr << "core_wasm_resume_record: " << g_failures << " failure(s)\n";
    return 1;
}
