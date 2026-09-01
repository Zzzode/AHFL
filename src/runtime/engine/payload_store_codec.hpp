#pragma once

// RFC 0026 KR6.5 E4-B2-B0: canonical, authenticated durable-resume payload-store
// artifact codecs (integrity-only). This slice is PURE byte<->model codec plus an
// HMAC authenticator for three on-wire artifact classes:
//   * slot            -- raw payload magic "AHFLPS"
//   * commit_manifest -- raw payload magic "AHFLCM" (Available / Consumed variants)
//   * generation ptr  -- raw payload magic "AHFLGP"
//
// It is GREENFIELD and additive: it holds no key material of its own, opens no
// files, performs no fsync/rename/locking, and has NO production caller yet. It does
// not touch `ahfl.workflow-recovery.v1|v2`, the A1 resume record / A2 exec-manifest
// codecs, `atomic_file`, compiler_ir, the public C ABI, or any emitted Wasm bytes.
// The POSIX store (publish/load/recovery, Consumed transition, fsync/lock/crash
// tests) is the SEPARATE B2-B1 slice; this file has none of it.
//
// This backend is INTEGRITY-ONLY. Its HMAC proves authenticity + tamper detection;
// it provides NO confidentiality (nothing is encrypted at rest) and NO rollback
// protection (an actor with filesystem write access can still replay a whole older
// authenticated artifact). Those honest limits are the backend's fixed guarantees
// and are enforced by the B1 store, not claimed here.
//
// Wire layout (all three classes): `body || auth_header || tag`, where `auth_header`
// is a fixed 25 bytes {alg_version u8, key_id 16 bytes, generation u64 little-endian}
// and `tag` is a fixed 32-byte HMAC-SHA256 -- a fixed 57-byte tail located from EOF.
// The trust root is a single HMAC over the exact on-wire prefix `[0, tag)`; the
// prefix's leading `magic || format_version` are the authenticated artifact-class /
// version domain separator (a distinct magic per class => a distinct HMAC domain).
// Admission is two-pass: an untrusted bounds-only pass 1 (never reads a body count,
// never allocates on an attacker count) verifies framing + key_id + tag, and only
// then does a trusted pass 2 decode the body and require a canonical re-encode.
//
// Numeric encoding, single locked convention: the `generation` fields (slot/manifest
// current generation, pointer current_generation, the Consumed `consumed_generation`,
// and the auth_header generation) are FIXED u64 little-endian, exactly matching the
// A1 auth_header; EVERY other numeric field is canonical LEB128 (overlong / overflow
// rejected). Digests are 64 lowercase-hex ASCII bytes.

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_ir.hpp"          // ir::core::CoreWorkflowId
#include "runtime/engine/core_wasm_resume_record.hpp" // core_wasm_resume::PayloadSlotId

namespace ahfl::runtime::payload_store {

// Per-checkpoint identity of one durable-resume checkpoint within a workflow. A new
// strong wire-identity type: the existing `ahfl::runtime::CheckpointId` is a size_t
// runtime index (unstable across processes) and unfit as a persisted u64 identity.
// Its invalid sentinel is UINT64_MAX and admission rejects it; 0 is legal.
struct ResumeCheckpointId {
    static constexpr std::uint64_t kInvalid = UINT64_MAX;
    std::uint64_t value{kInvalid};
    [[nodiscard]] friend bool operator==(ResumeCheckpointId, ResumeCheckpointId) noexcept = default;
};

// The A1 payload-slot reference, consumed here by value (never re-parsed from A1
// record bytes). Its invalid sentinel is UINT64_MAX and admission rejects it.
using core_wasm_resume::PayloadSlotId;

// A SHA-256 digest field carried as 64 lowercase-hex ASCII bytes.
using DigestHex = std::array<char, 64>;

// The single shared store error contract. The codec (B0) produces ONLY:
// SizeCapExceeded, Malformed, TrailingBytes, IntegrityFailed, KeyIdMismatch,
// GenerationMismatch, and SlotSetMismatch. The remaining variants are reserved for
// the B1 store layer that builds on this codec; every codec error is fixed and
// echoes no payload byte, key, digest, id, length, or count.
enum class PayloadStoreError {
    NotFound,              // (B1 store) no generation is published for the checkpoint
    Consumed,             // (B1 store) the live generation is a Consumed tombstone
    IntegrityFailed,       // HMAC tag mismatch
    KeyIdMismatch,         // record key_id != caller expected key_id
    GenerationMismatch,    // body generation != auth generation, or consumed range bad
    StateMismatch,         // (B1 store) cross-artifact state disagreement
    SizeCapExceeded,       // input span exceeds the caller-supplied artifact byte cap
    Truncated,             // input shorter than the minimum framing
    TrailingBytes,         // body has bytes after the last field
    Malformed,             // any other structural / canonical-form violation
    SlotSetMismatch,       // manifest slot_meta unsorted or duplicated (self-contained)
    CrossCheckpointRejected, // (B1 store) artifact namespace != caller namespace
    WriteFailed,           // (B1 store) durable write failed
    CommitInterrupted,     // (B1 store) publish interrupted before commit
    UnsupportedPlatform,   // (B1 store) non-POSIX platform
    UnsupportedFilesystem, // (B1 store) filesystem outside the supported allowlist
};

// The authenticated header carried alongside every artifact so a decoded model
// round-trips. `generation` is written u64 little-endian; `key_id` is the caller's
// candidate key identity (there is no keyring / KMS / resolver in this slice).
struct AuthHeader {
    std::uint8_t alg_version{1}; // 1 = HMAC-SHA256
    std::array<std::uint8_t, 16> key_id{};
    std::uint64_t generation{0};
    [[nodiscard]] friend bool operator==(const AuthHeader &, const AuthHeader &) noexcept = default;
};

// ---- slot artifact (magic "AHFLPS") ----------------------------------------

// One immutable payload slot. `generation` (body) MUST equal `auth.generation`;
// `payload` holds the opaque application bytes and `payload_len` on the wire MUST
// equal `payload.size()`. The slot BINDS (wf, ckpt, generation, slot) inside the
// authenticated body so a slot file copied to another checkpoint/generation fails
// admission at the B1 cross-check.
struct SlotArtifact {
    ir::core::CoreWorkflowId wf{};
    ResumeCheckpointId ckpt{};
    std::uint64_t generation{0};
    PayloadSlotId slot{};
    std::vector<std::uint8_t> payload;
    AuthHeader auth{};
    [[nodiscard]] friend bool operator==(const SlotArtifact &,
                                         const SlotArtifact &) noexcept = default;
};

// ---- commit_manifest (magic "AHFLCM") --------------------------------------

// One entry of the manifest slot set: the DISTINCT slot id, the SHA-256 of that
// slot's WHOLE artifact bytes (not just its tag), and the decoded payload length.
struct SlotMeta {
    PayloadSlotId slot{};
    DigestHex slot_artifact_sha256{};
    std::uint64_t payload_len{0};
    [[nodiscard]] friend bool operator==(const SlotMeta &, const SlotMeta &) noexcept = default;
};

// Available manifest body. `record_sha256`/`record_len` bind the WHOLE A1 record
// artifact byte span; `slots` is the manifest's own slot set, strictly ascending by
// slot id with no duplicates (a B0-local self-contained rule). Exact set-equality
// against the A1-record-derived expected set is a B1 cross-artifact check, NOT here.
struct AvailableBody {
    DigestHex record_sha256{};
    std::uint64_t record_len{0};
    std::vector<SlotMeta> slots;
    [[nodiscard]] friend bool operator==(const AvailableBody &,
                                         const AvailableBody &) noexcept = default;
};

// Consumed tombstone body: a manifest-only generation that binds the previous
// Available generation it consumes. `consumed_generation` is written u64
// little-endian and, in B0, must satisfy 1 <= consumed_generation < generation (the
// tighter current-1 / live-digest binding is a B1 store rule).
struct ConsumedBody {
    std::uint64_t consumed_generation{0};
    DigestHex consumed_manifest_sha256{};
    [[nodiscard]] friend bool operator==(const ConsumedBody &,
                                         const ConsumedBody &) noexcept = default;
};

// The commit_manifest. `generation` (body) MUST equal `auth.generation`. `state`
// discriminates Available (state byte 0) from Consumed (state byte 1); any other
// state byte is Malformed.
struct CommitManifest {
    ir::core::CoreWorkflowId wf{};
    ResumeCheckpointId ckpt{};
    std::uint64_t generation{0};
    std::variant<AvailableBody, ConsumedBody> state{AvailableBody{}};
    AuthHeader auth{};
    [[nodiscard]] friend bool operator==(const CommitManifest &,
                                         const CommitManifest &) noexcept = default;
};

// ---- generation pointer (magic "AHFLGP") -----------------------------------

// The authenticated "current generation" head for a checkpoint. `current_generation`
// (body) MUST equal `auth.generation`; `manifest_sha256` binds which manifest
// artifact is live. The pointer carries current-generation + manifest binding ONLY;
// Consumed state lives in the manifest, never here.
struct GenerationPointer {
    ir::core::CoreWorkflowId wf{};
    ResumeCheckpointId ckpt{};
    std::uint64_t current_generation{0};
    DigestHex manifest_sha256{};
    AuthHeader auth{};
    [[nodiscard]] friend bool operator==(const GenerationPointer &,
                                         const GenerationPointer &) noexcept = default;
};

// The exact maximum on-wire length of a generation pointer:
// magic(6) + format_version(1) + wf(<=5 ULEB) + ckpt(<=10 ULEB) +
// current_generation(8 LE) + manifest_sha256(64) + auth_header(25) + tag(32).
inline constexpr std::uint64_t kMaxGenerationPointerBytes = 6 + 1 + 5 + 10 + 8 + 64 + 25 + 32;
static_assert(kMaxGenerationPointerBytes == 151);

// ---- encode: validate-first, then authenticate -----------------------------
//
// Each encoder validates the model for its full structural invariant set FIRST (a
// caller cannot mint an artifact its decoder would reject), then emits
// `body || auth_header || tag` with tag = a single HMAC-SHA256 over the exact
// `body || auth_header` prefix under `key`. The `key_id` is taken ONLY from
// `model.auth.key_id` (single authority). `key` is borrowed for the one HMAC call
// and never stored or echoed.

[[nodiscard]] std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_slot(const SlotArtifact &slot, std::span<const std::uint8_t> key);

[[nodiscard]] std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_manifest(const CommitManifest &manifest, std::span<const std::uint8_t> key);

[[nodiscard]] std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_pointer(const GenerationPointer &pointer, std::span<const std::uint8_t> key);

// ---- decode: capped, two-pass, authenticated -------------------------------
//
// Each decoder first rejects `bytes.size() > cap` (SizeCapExceeded) before any
// framing work, then runs the two-pass admission: pass 1 (untrusted, bounds-only)
// checks minimum length, magic + format_version, the fixed 57-byte tail offsets,
// alg_version, and key_id equality (fixed-work), then recomputes the HMAC over
// `[0, tag)` and compares the tag (fixed-work); only after the tag verifies does
// pass 2 decode the body, enforce every structural invariant, reject trailing
// bytes, and require a canonical re-encode byte-equal to the authenticated prefix.
// The caller supplies the candidate `expected_key_id` and borrowed `key`; there is
// no keyring / KMS / resolver. On any failure a fixed PayloadStoreError is returned.
//
// `max_*_artifact_bytes` caps the WHOLE input artifact span (not just the payload).

[[nodiscard]] std::expected<SlotArtifact, PayloadStoreError>
decode_slot(std::span<const std::uint8_t> bytes,
            std::span<const std::uint8_t, 16> expected_key_id, std::span<const std::uint8_t> key,
            std::uint64_t max_slot_artifact_bytes);

[[nodiscard]] std::expected<CommitManifest, PayloadStoreError>
decode_manifest(std::span<const std::uint8_t> bytes,
                std::span<const std::uint8_t, 16> expected_key_id,
                std::span<const std::uint8_t> key, std::uint64_t max_manifest_artifact_bytes);

// The pointer cap is the fixed grammar-derived maximum `kMaxGenerationPointerBytes`.
[[nodiscard]] std::expected<GenerationPointer, PayloadStoreError>
decode_pointer(std::span<const std::uint8_t> bytes,
               std::span<const std::uint8_t, 16> expected_key_id,
               std::span<const std::uint8_t> key);

} // namespace ahfl::runtime::payload_store
