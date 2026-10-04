#pragma once

// RFC 0026 KR6.5 E4-B2-D2b (FOUNDATION): the cross-process exactly-once
// IdempotencyToken authority for Core-Wasm durable effects.
//
// The contract is LOCKED in
// docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md section 3
// ("Cross-process exactly-once dedup"); the preimage below is a byte-for-byte
// transcription:
//
//   "AHFL-IDEMPOTENCY-v1"                  (ASCII domain, 19 bytes, no NUL)
//   authority_id                           (IdempotencyAuthorityId, 16 bytes)
//   CoreWorkflowId                         (u32-LE)
//   ResumeCheckpointId                     (u64-LE)
//   CoreWorkflowNodeId                     (u32-LE)
//   InvocationOrdinal                      (u64-LE)
//   CoreCapabilityId                       (u32-LE)
//   source_symbol                          (u64-LE)
//   SHA-256(canonical typed Param bytes)   (32 raw bytes)
//
// Total preimage length: 103 fixed bytes. `generation` and `attempt` are
// EXCLUDED by construction (they change across replay).
//
// This is a PURE authority only: it computes a 32-byte SHA-256 token over the
// fixed-width coordinate aggregate. There is no VM, no store, no persistence,
// and no publication/dedup gate in this slice (those are the later D2b
// authority gates). The token is an identity/collision authority only -- never
// an authenticity or rollback authority. It is intentionally a distinct
// identity from the native process-local FNV-1a u64
// `compute_idempotency_key` in workflow_recovery.cpp: neither type converts to
// the other and the native path is not replaced.

#include <array>
#include <cstdint>
#include <span>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "base/support/sha256.hpp"                    // support::Sha256Digest
#include "runtime/engine/core_wasm_resume_record.hpp" // core_wasm_resume::InvocationOrdinal
#include "runtime/engine/payload_store_codec.hpp"     // payload_store::ResumeCheckpointId

namespace ahfl::runtime::core_wasm_idempotency_token {

/// Opaque fixed 16-byte identity of the dedup/intent backend. Constructed from
/// raw bytes ONLY: there is deliberately no key_id, path, hostname, or string
/// constructor -- the authority identity is never derived from any of those.
/// It is supplied by the typed backend authority or bound once at
/// host/controller construction and never switched within a checkpoint
/// lifetime.
struct IdempotencyAuthorityId {
    std::array<std::uint8_t, 16> bytes{};

    constexpr IdempotencyAuthorityId() noexcept = default;

    explicit constexpr IdempotencyAuthorityId(std::array<std::uint8_t, 16> raw_bytes) noexcept
        : bytes(raw_bytes) {}

    explicit IdempotencyAuthorityId(std::span<const std::uint8_t, 16> raw_bytes) noexcept {
        // Fixed-extent span: the length is 16 by the type system, no truncation
        // is possible, but copy through the bounded range rather than trusting
        // a memcpy length.
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = raw_bytes[i];
        }
    }

    [[nodiscard]] friend bool operator==(const IdempotencyAuthorityId &,
                                         const IdempotencyAuthorityId &) noexcept = default;
};

/// The opaque 32-byte idempotency token (SHA-256 of the fixed preimage). Raw
/// bytes only; it is never constructible from or comparable to a native u64.
struct IdempotencyToken {
    std::array<std::uint8_t, 32> bytes{};

    [[nodiscard]] friend bool operator==(const IdempotencyToken &,
                                         const IdempotencyToken &) noexcept = default;
};

/// The full coordinate the token binds. Every field is a strong index/id
/// type (Principle 2). There is deliberately NO `generation` and NO `attempt`
/// member: replay-changing values cannot enter the preimage.
struct IdempotencyCoordinate {
    IdempotencyAuthorityId authority;
    ir::core::CoreWorkflowId workflow{};
    payload_store::ResumeCheckpointId checkpoint{};
    ir::core::CoreWorkflowNodeId node{};
    core_wasm_resume::InvocationOrdinal ordinal{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    support::Sha256Digest param_digest{};
};

/// Compute the 32-byte IdempotencyToken for `coordinate`.
///
/// The preimage is a single fixed-size 103-byte stack buffer (no heap
/// allocation) laid out at compile-time-constant offsets, so every field is
/// fully contained by construction and no truncation path exists. All integers
/// are little-endian. Pure and deterministic: equal coordinates always yield
/// equal tokens; any one-bit coordinate change changes the token.
[[nodiscard]] IdempotencyToken
compute_idempotency_token(const IdempotencyCoordinate &coordinate) noexcept;

} // namespace ahfl::runtime::core_wasm_idempotency_token
