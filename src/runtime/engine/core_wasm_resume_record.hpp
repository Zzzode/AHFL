#pragma once

// RFC 0026 KR6.5 E4-B2-A1: canonical, authenticated durable-resume control record
// codec (`ahfl.wasm-resume.v1`, raw payload magic "AHFLWR").
//
// This is the FOUNDATION record codec for the full-workflow per-node replay ledger
// designed in docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md §4.2. It is a
// pure byte<->model codec plus an HMAC authenticator; it holds no key material of
// its own and computes no Wasm digests. It is GREENFIELD and additive: it does not
// touch the existing `ahfl.workflow-recovery.v1|v2` JSON store, compiler_ir, the
// public C ABI, or any emitted Wasm bytes, and it has no production persistence
// caller yet (the first is the future B2-D host).
//
// Wire layout: `body || auth_header || tag`, where `auth_header` is a fixed 25
// bytes {alg_version u8, key_id 16 bytes, generation u64 little-endian} and `tag`
// is a fixed 32-byte HMAC-SHA256 -- a fixed 57-byte tail located from EOF. The
// trust root is a single HMAC over the exact on-wire prefix `[0, tag)`; the
// prefix's leading `magic || format_version` are the authenticated artifact-class /
// version domain separator. Admission is two-pass: an untrusted bounds-only pass 1
// (never reads a body count, never allocates on an attacker count) verifies the
// framing + key_id + tag, and only then does a trusted pass 2 decode the body.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// CoreWorkflowId/NodeId, CoreCapabilityId, CoreLowerDiagnostic:
#include "ahfl/compiler/ir/core_ir.hpp"

namespace ahfl::runtime::core_wasm_resume {

// Per-node invocation ordinal (RFC 0022): a per-node counter, distinct from a
// capability IMPORT ordinal and from the A2 manifest call-site index. Kept a
// distinct strong type so the three ordinal concepts can never share a variable.
struct InvocationOrdinal {
    std::uint64_t value{0};
    [[nodiscard]] friend bool operator==(InvocationOrdinal, InvocationOrdinal) noexcept = default;
};

// Reference to a host-owned confidential/integrity payload slot (the record never
// carries a raw value_json). Its own invalid sentinel is UINT64_MAX and admission
// rejects it (unlike CoreCapabilityId{0}/SymbolId{0}, which are legal).
struct PayloadSlotId {
    static constexpr std::uint64_t kInvalid = UINT64_MAX;
    std::uint64_t value{kInvalid};
    [[nodiscard]] friend bool operator==(PayloadSlotId, PayloadSlotId) noexcept = default;
};

// A raw SHA-256 digest field carried by the record as 64 lowercase-hex ASCII
// bytes. A1 only parses/holds these three fields; it never computes a Wasm digest
// and never compares them against a loaded artifact (that comparison is B2-D).
using DigestHex = std::array<char, 64>;

enum class ResumeState : std::uint8_t { Suspended = 0, Injected = 1 };
enum class EntryKind : std::uint8_t { Workflow = 0 };
enum class NodeKind : std::uint8_t { Identity = 0, Capability = 1 };

struct ResumeMemoEntry {
    InvocationOrdinal invocation_ordinal{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    std::uint64_t arg_hash{0};
    PayloadSlotId result_slot{};
    [[nodiscard]] friend bool operator==(const ResumeMemoEntry &,
                                         const ResumeMemoEntry &) noexcept = default;
};

struct ResumePendingEntry {
    InvocationOrdinal invocation_ordinal{};
    ir::core::CoreCapabilityId capability{};
    std::uint64_t source_symbol{0};
    std::uint64_t arg_hash{0};
    [[nodiscard]] friend bool operator==(const ResumePendingEntry &,
                                         const ResumePendingEntry &) noexcept = default;
};

struct ResumeNode {
    ir::core::CoreWorkflowNodeId workflow_node_id{};
    std::uint32_t schedule_pos{0};
    NodeKind node_kind{NodeKind::Identity};
    std::vector<ResumeMemoEntry> memo;
    std::optional<ResumePendingEntry> pending;
    [[nodiscard]] friend bool operator==(const ResumeNode &, const ResumeNode &) noexcept = default;
};

struct ResumeAuthHeader {
    std::uint8_t alg_version{1}; // 1 = HMAC-SHA256
    std::array<std::uint8_t, 16> key_id{};
    std::uint64_t generation{0};
    [[nodiscard]] friend bool operator==(const ResumeAuthHeader &,
                                         const ResumeAuthHeader &) noexcept = default;
};

// The full-workflow per-node replay ledger. `body` fields only; `auth_header` is
// carried alongside so a decoded record round-trips, and `tag`/HMAC live at the
// codec boundary (not stored in the model).
struct CoreWasmResumeRecord {
    std::uint32_t format_version{1};
    std::uint32_t guarantees{0}; // bit0 rollback_protected, bit1 confidential_at_rest
    DigestHex module_sha256{};
    DigestHex wire_schema_sha256{};
    DigestHex exec_manifest_sha256{};
    EntryKind entry_kind{EntryKind::Workflow};
    ir::core::CoreWorkflowId entry_id{};
    ir::core::CoreWorkflowNodeId suspended_node_id{};
    ResumeState resume_state{ResumeState::Suspended};
    std::vector<ResumeNode> nodes;
    ResumeAuthHeader auth_header{};
    [[nodiscard]] friend bool operator==(const CoreWasmResumeRecord &,
                                         const CoreWasmResumeRecord &) noexcept = default;
};

// Result of encoding a validated model into an authenticated on-wire record.
struct CoreWasmResumeEncodeResult {
    std::optional<std::vector<std::uint8_t>> bytes;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return bytes.has_value() && !has_errors(); }
};

// Result of admitting an on-wire record. `record` is populated ONLY after the HMAC
// tag verifies AND every structural invariant + canonical re-encode gate passes.
struct CoreWasmResumeDecodeResult {
    std::optional<CoreWasmResumeRecord> record;
    std::vector<ir::core::CoreLowerDiagnostic> diagnostics;

    [[nodiscard]] bool has_errors() const noexcept;
    [[nodiscard]] bool ok() const noexcept { return record.has_value() && !has_errors(); }
};

// Encode a record and append its authentication tag. The model is validated for the
// full set of structural invariants FIRST (a caller cannot mint an ill-formed
// record); on success `bytes` is `body || auth_header || tag` with the tag = a
// single HMAC-SHA256 over the exact `body || auth_header` prefix under `key_bytes`.
// `key_bytes` is borrowed for the one HMAC call and never stored or echoed.
[[nodiscard]] CoreWasmResumeEncodeResult
encode_and_authenticate(const CoreWasmResumeRecord &record,
                        std::span<const std::uint8_t> key_bytes);

// Two-pass admission of an on-wire record. Pass 1 (untrusted, bounds-only) checks
// minimum length, magic + format_version, the fixed 57-byte tail offsets,
// `alg_version`, and equality of the record `key_id` against `expected_key_id`
// (fixed-work, no-early-exit best-effort comparison) -- it NEVER reads a body count
// and NEVER allocates on an attacker-supplied count. The HMAC is then recomputed
// over the exact prefix `[0, tag)` and compared to the record tag (again fixed-work
// best-effort). Only after the tag verifies does pass 2 decode the body's ULEB
// fields, enforce every structural invariant, and require a canonical re-encode of
// `body || auth_header` byte-equal to the authenticated prefix. The caller supplies
// the candidate `expected_key_id` and borrowed `key_bytes`; there is no keyring,
// KMS, or resolver in this slice. On any failure `record` is absent and only fixed
// no-echo diagnostics are returned.
[[nodiscard]] CoreWasmResumeDecodeResult
decode_and_authenticate(std::span<const std::uint8_t> record_bytes,
                        std::span<const std::uint8_t, 16> expected_key_id,
                        std::span<const std::uint8_t> key_bytes);

} // namespace ahfl::runtime::core_wasm_resume
