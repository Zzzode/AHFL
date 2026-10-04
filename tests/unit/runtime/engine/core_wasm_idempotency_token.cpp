// RFC 0026 KR6.5 E4-B2-D2b foundation tests for the pure IdempotencyToken
// authority. Hand-rolled check()/main(). Pins the LOCKED seam-doc preimage
// (docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md section 3): the exact
// 103-byte layout, little-endian order, the 19-byte ASCII domain, one-bit
// sensitivity of every coordinate field, the raw-bytes-only 16-byte authority
// identity, legal zero coordinates, and the fixed-vector separation from the
// native process-local FNV-1a u64 idempotency identity. FOUNDATION only: no VM,
// no store, no persistence.

#include "runtime/engine/core_wasm_idempotency_token.hpp"

#include "base/support/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using ahfl::ir::core::CoreCapabilityId;
using ahfl::ir::core::CoreWorkflowId;
using ahfl::ir::core::CoreWorkflowNodeId;
using ahfl::runtime::core_wasm_idempotency_token::compute_idempotency_token;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyAuthorityId;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyCoordinate;
using ahfl::runtime::core_wasm_idempotency_token::IdempotencyToken;
using ahfl::runtime::core_wasm_resume::InvocationOrdinal;
using ahfl::runtime::payload_store::ResumeCheckpointId;
using ahfl::support::sha256;
using ahfl::support::Sha256Digest;

int g_failures = 0;

void check(bool ok, std::string_view name) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

constexpr std::string_view kDomain{"AHFL-IDEMPOTENCY-v1"};

void put_u32_le(std::array<std::uint8_t, 103> &buf, std::size_t off, std::uint32_t v) {
    buf[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
    buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

void put_u64_le(std::array<std::uint8_t, 103> &buf, std::size_t off, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) {
        buf[off + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu);
    }
}

// Independent hand-assembly of the exact seam-doc preimage.
Sha256Digest independent_preimage_hash(const IdempotencyCoordinate &c) {
    std::array<std::uint8_t, 103> preimage{};
    std::size_t pos = 0;
    for (char ch : kDomain) {
        preimage[pos++] = static_cast<std::uint8_t>(ch);
    }
    for (std::uint8_t b : c.authority.bytes) {
        preimage[pos++] = b;
    }
    put_u32_le(preimage, pos, c.workflow.value);
    pos += 4;
    put_u64_le(preimage, pos, c.checkpoint.value);
    pos += 8;
    put_u32_le(preimage, pos, c.node.value);
    pos += 4;
    put_u64_le(preimage, pos, c.ordinal.value);
    pos += 8;
    put_u32_le(preimage, pos, c.capability.value);
    pos += 4;
    put_u64_le(preimage, pos, c.source_symbol);
    pos += 8;
    for (std::uint8_t b : c.param_digest) {
        preimage[pos++] = b;
    }
    check(pos == 103, "independent_preimage_is_103_bytes");
    return sha256(std::span<const std::uint8_t>{preimage});
}

IdempotencyAuthorityId make_authority(std::uint8_t seed) {
    std::array<std::uint8_t, 16> raw{};
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 7);
    }
    return IdempotencyAuthorityId{raw};
}

Sha256Digest make_digest(std::uint8_t seed) {
    Sha256Digest d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(seed + static_cast<int>(i) * 13);
    }
    return d;
}

IdempotencyCoordinate baseline_coordinate() {
    return IdempotencyCoordinate{
        .authority = make_authority(0x11),
        .workflow = CoreWorkflowId{0x01020304u},
        .checkpoint = ResumeCheckpointId{0x1112131415161718ULL},
        .node = CoreWorkflowNodeId{0x21222324u},
        .ordinal = InvocationOrdinal{0x3132333435363738ULL},
        .capability = CoreCapabilityId{0x41424344u},
        .source_symbol = 0x5152535455565758ULL,
        .param_digest = make_digest(0x61),
    };
}

std::uint64_t load_u64_le(const IdempotencyToken &t, std::size_t off) {
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(t.bytes[off + i]) << (i * 8);
    }
    return v;
}

// Byte-for-byte copy of the native process-local FNV-1a mix
// (compute_idempotency_key in src/runtime/engine/workflow_recovery.cpp). Kept
// here only to prove the two identities never coincide; the production
// function is deliberately not reused.
std::uint64_t native_fnv_idempotency_key(std::size_t workflow_index,
                                         std::size_t node_index,
                                         std::uint64_t ordinal,
                                         std::size_t cap_symbol_id,
                                         std::uint64_t arg_hash) {
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFULL;
            hash *= kPrime;
        }
    };
    mix(static_cast<std::uint64_t>(workflow_index));
    mix(static_cast<std::uint64_t>(node_index));
    mix(ordinal);
    mix(static_cast<std::uint64_t>(cap_symbol_id));
    mix(arg_hash);
    return hash;
}

} // namespace

int main() {
    // ---- (a) KAT: independent hand-assembly of the exact preimage ----------
    {
        check(kDomain.size() == 19, "domain_is_exactly_19_bytes");
        check(kDomain == "AHFL-IDEMPOTENCY-v1", "domain_spelling");

        const IdempotencyCoordinate c = baseline_coordinate();
        const IdempotencyToken token = compute_idempotency_token(c);
        const Sha256Digest expected = independent_preimage_hash(c);
        check(token.bytes == expected, "kat_matches_independent_preimage_sha256");

        // Render the token to lowercase hex for the pinned known-answer value.
        std::string hex;
        static const char *kHex = "0123456789abcdef";
        for (std::uint8_t b : token.bytes) {
            hex.push_back(kHex[b >> 4]);
            hex.push_back(kHex[b & 0x0Fu]);
        }
        check(hex.size() == 64, "token_hex_is_64_chars");
        check(hex != std::string(64, '0'), "token_is_not_all_zero");

        // Pinned known-answer value for the fixed baseline vector, computed by
        // an independent SHA-256 implementation over the hand-assembled
        // preimage: a silent layout/order/domain change cannot pass on two
        // agreeing in-process implementations.
        check(hex == "589bbe289f16bb74dd7fa431d6b5483a1c2fba9e55d2e07c6a4576b0fd938127",
              "pinned_baseline_kat_digest");
    }

    // ---- (b) determinism ----------------------------------------------------
    {
        const IdempotencyCoordinate a = baseline_coordinate();
        const IdempotencyCoordinate b = baseline_coordinate();
        check(compute_idempotency_token(a) == compute_idempotency_token(b),
              "deterministic_equal_coordinates");
    }

    // ---- (c) one-bit difference vectors for every coordinate field ---------
    {
        const IdempotencyCoordinate base = baseline_coordinate();
        const IdempotencyToken base_token = compute_idempotency_token(base);

        IdempotencyCoordinate v = base;
        v.authority.bytes[7] ^= 0x01u;
        check(compute_idempotency_token(v) != base_token, "one_bit_authority_changes_token");

        v = base;
        v.workflow.value ^= 0x00010000u;
        check(compute_idempotency_token(v) != base_token, "one_bit_workflow_changes_token");

        v = base;
        v.checkpoint.value ^= 0x0000010000000000ULL;
        check(compute_idempotency_token(v) != base_token, "one_bit_checkpoint_changes_token");

        v = base;
        v.node.value ^= 0x00000001u;
        check(compute_idempotency_token(v) != base_token, "one_bit_node_changes_token");

        v = base;
        v.ordinal.value ^= 0x8000000000000000ULL;
        check(compute_idempotency_token(v) != base_token, "one_bit_ordinal_changes_token");

        v = base;
        v.capability.value ^= 0x00040000u;
        check(compute_idempotency_token(v) != base_token, "one_bit_capability_changes_token");

        v = base;
        v.source_symbol ^= 0x0000000000000001ULL;
        check(compute_idempotency_token(v) != base_token, "one_bit_source_symbol_changes_token");

        v = base;
        v.param_digest[31] ^= 0x80u;
        check(compute_idempotency_token(v) != base_token, "one_bit_param_digest_changes_token");

        // LE byte-order pin: flipping the low byte of a u32 lands at the field
        // start, and a separate preimage with the byte-swapped value differs.
        v = base;
        v.workflow.value = __builtin_bswap32(base.workflow.value);
        check(compute_idempotency_token(v) != base_token, "byte_swapped_workflow_differs");
    }

    // ---- (d) authority id is exactly 16 raw bytes, never a string ----------
    {
        static_assert(sizeof(IdempotencyAuthorityId) == 16,
                      "authority id must be exactly 16 bytes");
        static_assert(sizeof(IdempotencyToken) == 32, "token must be exactly 32 bytes");

        // Constructible from exactly the two raw-bytes shapes.
        std::array<std::uint8_t, 16> raw{};
        const IdempotencyAuthorityId from_array{raw};
        std::span<const std::uint8_t, 16> raw_span{raw};
        const IdempotencyAuthorityId from_span{raw_span};
        check(from_array == from_span, "array_and_span_authority_agree");

        // Identity is raw bytes only: no key_id/path/hostname/string/integer
        // constructor, and no dynamic-extent span (length would be unchecked).
        static_assert(!std::is_constructible_v<IdempotencyAuthorityId, std::string>,
                      "authority must have no string constructor");
        static_assert(!std::is_constructible_v<IdempotencyAuthorityId, const char *>,
                      "authority must have no C-string constructor");
        static_assert(!std::is_constructible_v<IdempotencyAuthorityId, std::string_view>,
                      "authority must have no string_view constructor");
        static_assert(!std::is_constructible_v<IdempotencyAuthorityId, std::uint64_t>,
                      "authority must have no integer constructor");
        static_assert(
            !std::is_constructible_v<IdempotencyAuthorityId, std::span<const std::uint8_t>>,
            "authority must reject dynamic-extent spans");

        // The 32-byte token is never constructible from or comparable to a
        // native u64 identity.
        static_assert(!std::is_constructible_v<IdempotencyToken, std::uint64_t>,
                      "token must have no u64 constructor");
        static_assert(!std::is_convertible_v<std::uint64_t, IdempotencyToken>,
                      "u64 must not convert to token");
    }

    // ---- (e) zero cap id / symbol / checkpoint are legal -------------------
    {
        IdempotencyCoordinate c = baseline_coordinate();
        c.capability = CoreCapabilityId{0};
        c.source_symbol = 0;
        c.checkpoint = ResumeCheckpointId{0};
        // Computes successfully and agrees with the independent preimage.
        check(compute_idempotency_token(c).bytes == independent_preimage_hash(c),
              "zero_cap_symbol_checkpoint_is_legal");

        // An all-zero coordinate (only authority set) computes deterministically.
        IdempotencyCoordinate z{};
        z.authority = make_authority(0x11);
        const IdempotencyToken t2 = compute_idempotency_token(z);
        check(t2.bytes == independent_preimage_hash(z), "all_zero_coordinate_is_legal");

        // Distinct zero-coordinate vectors under different authorities differ.
        IdempotencyCoordinate z2 = z;
        z2.authority = make_authority(0x22);
        check(compute_idempotency_token(z2) != t2, "zero_coordinate_respects_authority");
    }

    // ---- (f) fixed vector: 32B token never coincides with native FNV u64 ---
    {
        struct Vec {
            std::uint32_t wf;
            std::uint64_t ckpt;
            std::uint32_t node;
            std::uint64_t ordinal;
            std::uint32_t cap;
            std::uint64_t symbol;
        };
        const Vec vectors[] = {
            {0, 0, 0, 0, 0, 0},
            {1, 2, 3, 4, 5, 6},
            {0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFULL, 0x80000000u, 0x8000000000000000ULL, 7, 42},
            {0x01020304u,
             0x1112131415161718ULL,
             0x21222324u,
             0x3132333435363738ULL,
             0x41424344u,
             0x5152535455565758ULL},
            {777, 1, 99, 1000000, 13, 0xDEADBEEFCAFEULL},
        };
        for (std::size_t i = 0; i < std::size(vectors); ++i) {
            const Vec &x = vectors[i];
            IdempotencyCoordinate c{};
            c.authority = make_authority(static_cast<std::uint8_t>(i + 1));
            c.workflow = CoreWorkflowId{x.wf};
            c.checkpoint = ResumeCheckpointId{x.ckpt};
            c.node = CoreWorkflowNodeId{x.node};
            c.ordinal = InvocationOrdinal{x.ordinal};
            c.capability = CoreCapabilityId{x.cap};
            c.source_symbol = x.symbol;
            c.param_digest = make_digest(static_cast<std::uint8_t>(0x80 + i));

            const IdempotencyToken token = compute_idempotency_token(c);

            // The native key folds (workflow, node, ordinal, cap symbol,
            // arg_hash); reduce the same coordinates using the leading
            // little-endian u64 lane of the param digest as the arg hash.
            std::uint64_t digest_lane = 0;
            for (std::size_t k = 0; k < 8; ++k) {
                digest_lane |= static_cast<std::uint64_t>(c.param_digest[k]) << (k * 8);
            }
            const std::uint64_t fnv = native_fnv_idempotency_key(
                x.wf, x.node, x.ordinal, static_cast<std::size_t>(x.symbol), digest_lane);

            // None of the token's four little-endian u64 lanes coincides with
            // the native FNV identity for the same coordinates.
            bool lane_collision = false;
            for (std::size_t lane = 0; lane < 32; lane += 8) {
                if (load_u64_le(token, lane) == fnv) {
                    lane_collision = true;
                }
            }
            check(!lane_collision, "token_never_coincides_with_fnv_u64");

            // The token is also never the FNV value zero-padded into 32 bytes
            // in either byte order.
            IdempotencyToken fnv_le{};
            IdempotencyToken fnv_be{};
            for (std::size_t k = 0; k < 8; ++k) {
                fnv_le.bytes[k] = static_cast<std::uint8_t>((fnv >> (k * 8)) & 0xFFu);
                fnv_be.bytes[7 - k] = static_cast<std::uint8_t>((fnv >> (k * 8)) & 0xFFu);
            }
            check(token != fnv_le && token != fnv_be, "token_is_not_fnv_value_padded_to_32_bytes");
        }
    }

    if (g_failures == 0) {
        std::cout << "all idempotency token tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " idempotency token test(s) failed\n";
    return 1;
}
