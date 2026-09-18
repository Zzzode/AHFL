#include "runtime/engine/core_wasm_resume_record.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

#include "ahfl/compiler/ir/core_wire_schema.hpp" // wire_schema::kInvalid
#include "base/support/sha256.hpp"

namespace ahfl::runtime::core_wasm_resume {
namespace {

using ir::core::CoreCapabilityId;
using ir::core::CoreDiagnosticSeverity;
using ir::core::CoreLowerDiagnostic;
using ir::core::CoreWorkflowId;
using ir::core::CoreWorkflowNodeId;

// Diagnostic code. Per the E4-B2-A reviewer ruling this reuses the existing
// `ir::core::wire_schema::kInvalid` catalogue entry (a record-specific code /
// namespace would be a separate diagnostic shared gate, not opened in this slice).
// Every diagnostic uses this code, a null range, and a fixed message that echoes
// no payload byte, key, digest, or count.

// Raw payload magic (distinct per authenticated artifact class; the domain
// separator for the record's HMAC lives in these leading authenticated bytes).
constexpr std::array<std::uint8_t, 6> kMagic = {'A', 'H', 'F', 'L', 'W', 'R'};
constexpr std::uint8_t kFormatVersion = 1;
constexpr std::uint8_t kAlgHmacSha256 = 1;

// Fixed tail: auth_header (alg_version u8 + key_id 16 + generation u64-LE) = 25,
// plus the 32-byte tag.
constexpr std::size_t kKeyIdBytes = 16;
constexpr std::size_t kGenerationBytes = 8;
constexpr std::size_t kAuthHeaderBytes = 1 + kKeyIdBytes + kGenerationBytes; // 25
constexpr std::size_t kTagBytes = 32;
constexpr std::size_t kTailBytes = kAuthHeaderBytes + kTagBytes; // 57

// Smallest possible body: magic(6) + format_version(1) + guarantees(>=1) +
// 3 * 64 hex digests + entry.kind(1) + entry.id(>=1) + entry_input_slot(>=1) +
// suspended_node_id(>=1) + resume_state(1) + node_count(>=1). Used only as a coarse
// pre-auth lower bound; the real structure is validated in pass 2 after
// authentication.
constexpr std::size_t kMinBodyBytes = 6 + 1 + 1 + (3 * 64) + 1 + 1 + 1 + 1 + 1 + 1;

[[nodiscard]] CoreLowerDiagnostic error(std::string message) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(ir::core::wire_schema::kInvalid), std::move(message),
                               std::nullopt};
}

// A DigestHex is well-formed iff every byte is lowercase-hex ASCII ([0-9a-f]).
[[nodiscard]] bool is_lowercase_hex(const DigestHex &hex) noexcept {
    for (const char c : hex) {
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_lower = (c >= 'a' && c <= 'f');
        if (!is_digit && !is_lower) {
            return false;
        }
    }
    return true;
}

// ---- canonical LEB128 writer -----------------------------------------------

void write_u64(std::vector<std::uint8_t> &out, std::uint64_t value) {
    do {
        auto current = static_cast<std::uint8_t>(value & 0x7fU);
        value >>= 7U;
        if (value != 0) {
            current |= 0x80U;
        }
        out.push_back(current);
    } while (value != 0);
}

void write_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    write_u64(out, value);
}

void write_hex(std::vector<std::uint8_t> &out, const DigestHex &hex) {
    for (const char c : hex) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
}

// ---- canonical LEB128 reader (memory-safe; canonicality proven by re-encode) ---

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> data) noexcept : data_(data) {}

    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
    [[nodiscard]] bool at_end() const noexcept { return pos_ == data_.size(); }

    void fail() noexcept { failed_ = true; }

    [[nodiscard]] std::uint8_t byte() noexcept {
        if (failed_ || pos_ >= data_.size()) {
            failed_ = true;
            return 0;
        }
        return data_[pos_++];
    }

    [[nodiscard]] std::uint64_t u64() noexcept {
        std::uint64_t result = 0;
        std::uint32_t shift = 0;
        while (true) {
            if (failed_) {
                return 0;
            }
            if (shift >= 64) {
                failed_ = true; // overlong
                return 0;
            }
            const std::uint8_t current = byte();
            if (failed_) {
                return 0;
            }
            const std::uint64_t payload = current & 0x7fU;
            if (shift == 63 && payload > 1U) {
                failed_ = true; // out of range
                return 0;
            }
            result |= payload << shift;
            if ((current & 0x80U) == 0) {
                break;
            }
            shift += 7;
        }
        return result;
    }

    // A u32-domain ULEB. Rejects anything that does not fit in 32 bits.
    [[nodiscard]] std::uint32_t u32() noexcept {
        const std::uint64_t value = u64();
        if (failed_) {
            return 0;
        }
        if (value > std::numeric_limits<std::uint32_t>::max()) {
            failed_ = true;
            return 0;
        }
        return static_cast<std::uint32_t>(value);
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{0};
    bool failed_{false};
};

// ---- structural-invariant validation (shared by encoder and decoder) --------

// Validate the full record model per seam §4.2. Returns nullopt on success or a
// fixed diagnostic on the first violation. Applied by the encoder (a caller cannot
// mint an ill-formed record) and by the decoder after authentication.
[[nodiscard]] std::optional<CoreLowerDiagnostic> validate_model(const CoreWasmResumeRecord &r) {
    if (r.format_version != kFormatVersion) {
        return error("resume record has an unsupported format version");
    }
    // Enumerated allowed-set gates: an out-of-set enum value must never be minted
    // (so the encoder cannot produce a record the decoder would reject).
    if (r.entry_kind != EntryKind::Workflow) {
        return error("resume record has an unsupported entry kind");
    }
    if (r.resume_state != ResumeState::Suspended && r.resume_state != ResumeState::Injected) {
        return error("resume record has an unsupported resume state");
    }
    if ((r.guarantees & ~0x3U) != 0) {
        return error("resume record declares an unknown guarantees bit");
    }
    if (!is_lowercase_hex(r.module_sha256) || !is_lowercase_hex(r.wire_schema_sha256) ||
        !is_lowercase_hex(r.exec_manifest_sha256)) {
        return error("resume record digest field is not lowercase hex");
    }
    if (r.entry_id.value == CoreWorkflowId::kInvalid) {
        return error("resume record entry id is the invalid sentinel");
    }
    if (r.entry_input_slot.value == PayloadSlotId::kInvalid) {
        return error("resume record entry input slot is the invalid sentinel");
    }
    if (r.suspended_node_id.value == CoreWorkflowNodeId::kInvalid) {
        return error("resume record suspended node id is the invalid sentinel");
    }
    if (r.nodes.empty()) {
        return error("resume record has no nodes");
    }
    if (r.nodes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return error("resume record node count exceeds the 32-bit wire domain");
    }

    // Per-node structure + global node-id uniqueness. schedule_pos MUST equal the
    // array index (dense, gap-free, strictly ascending). node_id globally unique
    // via an average-O(1) hash set (no arbitrary node cap in the protocol).
    std::unordered_set<std::uint32_t> seen_ids;
    seen_ids.reserve(r.nodes.size());
    for (std::size_t i = 0; i < r.nodes.size(); ++i) {
        const ResumeNode &node = r.nodes[i];
        if (node.workflow_node_id.value == CoreWorkflowNodeId::kInvalid) {
            return error("resume record node id is the invalid sentinel");
        }
        if (node.schedule_pos != i) {
            return error("resume record schedule_pos is not equal to the node index");
        }
        if (!seen_ids.insert(node.workflow_node_id.value).second) {
            return error("resume record node id is not globally unique");
        }

        if (node.node_kind != NodeKind::Identity && node.node_kind != NodeKind::Capability) {
            return error("resume record node kind is out of range");
        }
        if (node.memo.size() > std::numeric_limits<std::uint32_t>::max()) {
            return error("resume record memo count exceeds the 32-bit wire domain");
        }

        // Per-node memo: ordinals exactly 0..memo.size()-1, entries valid.
        for (std::size_t m = 0; m < node.memo.size(); ++m) {
            const ResumeMemoEntry &entry = node.memo[m];
            if (entry.invocation_ordinal.value != m) {
                return error("resume record memo ordinal is not dense from zero");
            }
            if (entry.capability.value == CoreCapabilityId::kInvalid) {
                return error("resume record memo capability is the invalid sentinel");
            }
            if (entry.result_slot.value == PayloadSlotId::kInvalid) {
                return error("resume record memo result slot is the invalid sentinel");
            }
            // Input provenance never shares slot authority with result provenance:
            // a memo result slot must never equal the workflow entry input slot.
            // Checked inline while visiting each memo entry (no set allocation);
            // repeated memo references to the same result slot remain legal.
            if (entry.result_slot == r.entry_input_slot) {
                return error("resume record memo result slot equals the entry input slot");
            }
        }

        const bool is_frontier = node.workflow_node_id == r.suspended_node_id;
        if (node.node_kind == NodeKind::Identity && !node.memo.empty()) {
            return error("resume record identity node carries a memo");
        }
        if (node.node_kind == NodeKind::Identity && node.pending.has_value()) {
            return error("resume record identity node carries a pending");
        }
        if (node.pending.has_value() && !is_frontier) {
            return error("resume record pending is on a non-frontier node");
        }
    }

    // The frontier node is nodes.back() and must be a capability node.
    const ResumeNode &frontier = r.nodes.back();
    if (!(frontier.workflow_node_id == r.suspended_node_id)) {
        return error("resume record suspended node is not the last scheduled node");
    }
    if (frontier.node_kind != NodeKind::Capability) {
        return error("resume record frontier node is not a capability node");
    }

    // resume_state coupling.
    if (r.resume_state == ResumeState::Suspended) {
        if (!frontier.pending.has_value()) {
            return error("resume record is Suspended without a pending entry");
        }
        // Exactly one pending in the whole record (non-frontier pending already
        // rejected above), on the frontier, ordinal == frontier.memo.size(), and
        // that ordinal is not present in memo (guaranteed by the dense 0..n-1 rule).
        if (frontier.pending->invocation_ordinal.value != frontier.memo.size()) {
            return error("resume record pending ordinal does not follow the memo");
        }
        if (frontier.pending->capability.value == CoreCapabilityId::kInvalid) {
            return error("resume record pending capability is the invalid sentinel");
        }
    } else { // Injected
        for (const ResumeNode &node : r.nodes) {
            if (node.pending.has_value()) {
                return error("resume record is Injected but carries a pending entry");
            }
        }
        if (frontier.memo.empty()) {
            return error("resume record is Injected but the frontier memo is empty");
        }
    }
    return std::nullopt;
}

// ---- body + auth_header encoding -------------------------------------------

void encode_body(std::vector<std::uint8_t> &out, const CoreWasmResumeRecord &r) {
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    out.push_back(kFormatVersion);
    write_u32(out, r.guarantees);
    write_hex(out, r.module_sha256);
    write_hex(out, r.wire_schema_sha256);
    write_hex(out, r.exec_manifest_sha256);
    out.push_back(static_cast<std::uint8_t>(r.entry_kind));
    write_u32(out, r.entry_id.value);
    write_u64(out, r.entry_input_slot.value);
    write_u32(out, r.suspended_node_id.value);
    out.push_back(static_cast<std::uint8_t>(r.resume_state));
    write_u32(out, static_cast<std::uint32_t>(r.nodes.size()));
    for (const ResumeNode &node : r.nodes) {
        write_u32(out, node.workflow_node_id.value);
        write_u32(out, node.schedule_pos);
        out.push_back(static_cast<std::uint8_t>(node.node_kind));
        write_u32(out, static_cast<std::uint32_t>(node.memo.size()));
        for (const ResumeMemoEntry &entry : node.memo) {
            write_u64(out, entry.invocation_ordinal.value);
            write_u32(out, entry.capability.value);
            write_u64(out, entry.source_symbol);
            write_u64(out, entry.arg_hash);
            write_u64(out, entry.result_slot.value);
        }
        // pending presence is implied by resume_state (present iff Suspended AND
        // this is the frontier node); no separate presence byte.
        if (node.pending.has_value()) {
            write_u64(out, node.pending->invocation_ordinal.value);
            write_u32(out, node.pending->capability.value);
            write_u64(out, node.pending->source_symbol);
            write_u64(out, node.pending->arg_hash);
        }
    }
}

void encode_auth_header(std::vector<std::uint8_t> &out, const ResumeAuthHeader &h) {
    out.push_back(h.alg_version);
    out.insert(out.end(), h.key_id.begin(), h.key_id.end());
    std::uint64_t gen = h.generation;
    for (std::size_t i = 0; i < kGenerationBytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(gen & 0xffU));
        gen >>= 8U;
    }
}

// ---- body + auth_header decoding (trusted pass 2) --------------------------

[[nodiscard]] bool read_hex(Reader &reader, DigestHex &out) {
    for (char &c : out) {
        const std::uint8_t b = reader.byte();
        if (reader.failed()) {
            return false;
        }
        const bool is_digit = (b >= '0' && b <= '9');
        const bool is_lower_hex = (b >= 'a' && b <= 'f');
        if (!is_digit && !is_lower_hex) {
            return false; // digests are lowercase-hex ASCII only
        }
        c = static_cast<char>(b);
    }
    return true;
}

} // namespace

bool CoreWasmResumeEncodeResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const CoreLowerDiagnostic &d) {
                           return d.severity == CoreDiagnosticSeverity::Error;
                       });
}

bool CoreWasmResumeDecodeResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const CoreLowerDiagnostic &d) {
                           return d.severity == CoreDiagnosticSeverity::Error;
                       });
}

CoreWasmResumeEncodeResult encode_and_authenticate(const CoreWasmResumeRecord &record,
                                                   std::span<const std::uint8_t> key_bytes) {
    CoreWasmResumeEncodeResult result;
    if (record.auth_header.alg_version != kAlgHmacSha256) {
        result.diagnostics.push_back(error("resume record auth header alg is unsupported"));
        return result;
    }
    if (auto bad = validate_model(record); bad.has_value()) {
        result.diagnostics.push_back(std::move(*bad));
        return result;
    }
    std::vector<std::uint8_t> prefix;
    encode_body(prefix, record);
    encode_auth_header(prefix, record.auth_header);
    const support::Sha256Digest tag = support::hmac_sha256(key_bytes, prefix);
    prefix.insert(prefix.end(), tag.begin(), tag.end());
    result.bytes = std::move(prefix);
    return result;
}

CoreWasmResumeDecodeResult
decode_and_authenticate(std::span<const std::uint8_t> record_bytes,
                        std::span<const std::uint8_t, 16> expected_key_id,
                        std::span<const std::uint8_t> key_bytes) {
    CoreWasmResumeDecodeResult result;

    // ---- PASS 1: untrusted, bounds-only. No body count is read; nothing is
    // allocated on any attacker-supplied value. ----
    if (record_bytes.size() < kMinBodyBytes + kTailBytes) {
        result.diagnostics.push_back(error("resume record is shorter than the minimum framing"));
        return result;
    }
    // Magic + format version at the front.
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (record_bytes[i] != kMagic[i]) {
            result.diagnostics.push_back(error("resume record has a bad magic header"));
            return result;
        }
    }
    if (record_bytes[kMagic.size()] != kFormatVersion) {
        result.diagnostics.push_back(error("resume record has an unsupported format version"));
        return result;
    }
    // Fixed 57-byte tail located from EOF.
    const std::size_t total = record_bytes.size();
    const std::size_t tag_off = total - kTagBytes;
    const std::size_t auth_off = total - kTailBytes;
    if (record_bytes[auth_off] != kAlgHmacSha256) {
        result.diagnostics.push_back(error("resume record auth header alg is unsupported"));
        return result;
    }
    const std::span<const std::uint8_t> record_key_id =
        record_bytes.subspan(auth_off + 1, kKeyIdBytes);
    if (!support::fixed_work_equal(record_key_id, expected_key_id)) {
        result.diagnostics.push_back(error("resume record key id does not match the expected key"));
        return result;
    }
    // Authenticity: single HMAC over the exact on-wire prefix [0, tag).
    const std::span<const std::uint8_t> prefix = record_bytes.first(tag_off);
    const support::Sha256Digest expected_tag = support::hmac_sha256(key_bytes, prefix);
    const std::span<const std::uint8_t> record_tag = record_bytes.subspan(tag_off, kTagBytes);
    if (!support::fixed_work_equal(expected_tag, record_tag)) {
        result.diagnostics.push_back(error("resume record authentication tag is invalid"));
        return result;
    }

    // ---- PASS 2: trusted. The prefix is authenticated; decode the body. ----
    CoreWasmResumeRecord record;
    Reader reader(record_bytes.first(auth_off)); // body only
    // magic + version (already checked; consume them so offsets line up)
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        (void)reader.byte();
    }
    (void)reader.byte(); // format_version
    record.format_version = kFormatVersion;
    record.guarantees = reader.u32();
    if (!read_hex(reader, record.module_sha256) || !read_hex(reader, record.wire_schema_sha256) ||
        !read_hex(reader, record.exec_manifest_sha256)) {
        result.diagnostics.push_back(error("resume record digest field is malformed"));
        return result;
    }
    const std::uint8_t entry_kind = reader.byte();
    if (reader.failed() || entry_kind != static_cast<std::uint8_t>(EntryKind::Workflow)) {
        result.diagnostics.push_back(error("resume record has an unsupported entry kind"));
        return result;
    }
    record.entry_kind = EntryKind::Workflow;
    record.entry_id = CoreWorkflowId{reader.u32()};
    record.entry_input_slot = PayloadSlotId{reader.u64()};
    record.suspended_node_id = CoreWorkflowNodeId{reader.u32()};
    const std::uint8_t resume_state = reader.byte();
    if (reader.failed() || resume_state > static_cast<std::uint8_t>(ResumeState::Injected)) {
        result.diagnostics.push_back(error("resume record has an unsupported resume state"));
        return result;
    }
    record.resume_state = static_cast<ResumeState>(resume_state);

    const std::uint32_t node_count = reader.u32();
    if (reader.failed()) {
        result.diagnostics.push_back(error("resume record node count is malformed"));
        return result;
    }
    // Count-before-reserve: a node entry costs at least kMinNodeEntryBytes bytes
    // (workflow_node_id >=1, schedule_pos >=1, node_kind 1, memo_count >=1), so a
    // count exceeding remaining()/kMinNodeEntryBytes cannot be real. Guard BEFORE
    // reserve so a huge count never triggers a large allocation even though the
    // prefix is authenticated.
    constexpr std::size_t kMinNodeEntryBytes = 4;
    if (node_count > reader.remaining() / kMinNodeEntryBytes) {
        result.diagnostics.push_back(
            error("resume record declares more nodes than remaining bytes"));
        return result;
    }
    record.nodes.reserve(node_count);
    for (std::uint32_t n = 0; n < node_count; ++n) {
        ResumeNode node;
        node.workflow_node_id = CoreWorkflowNodeId{reader.u32()};
        node.schedule_pos = reader.u32();
        const std::uint8_t node_kind = reader.byte();
        if (reader.failed() || node_kind > static_cast<std::uint8_t>(NodeKind::Capability)) {
            result.diagnostics.push_back(error("resume record node kind is malformed"));
            return result;
        }
        node.node_kind = static_cast<NodeKind>(node_kind);
        const std::uint32_t memo_count = reader.u32();
        // A memo entry costs at least kMinMemoEntryBytes bytes (each of
        // invocation_ordinal/capability/source_symbol/arg_hash/result_slot is >=1
        // ULEB byte). Guard before reserve.
        constexpr std::size_t kMinMemoEntryBytes = 5;
        if (reader.failed() || memo_count > reader.remaining() / kMinMemoEntryBytes) {
            result.diagnostics.push_back(
                error("resume record declares more memo entries than remaining bytes"));
            return result;
        }
        node.memo.reserve(memo_count);
        for (std::uint32_t m = 0; m < memo_count; ++m) {
            ResumeMemoEntry entry;
            entry.invocation_ordinal = InvocationOrdinal{reader.u64()};
            entry.capability = CoreCapabilityId{reader.u32()};
            entry.source_symbol = reader.u64();
            entry.arg_hash = reader.u64();
            entry.result_slot = PayloadSlotId{reader.u64()};
            if (reader.failed()) {
                result.diagnostics.push_back(error("resume record memo entry is malformed"));
                return result;
            }
            node.memo.push_back(std::move(entry));
        }
        // pending is present iff resume_state == Suspended AND this is the frontier
        // node (the last scheduled node). The presence is structural, not a byte.
        const bool is_frontier = (n + 1 == node_count);
        if (record.resume_state == ResumeState::Suspended && is_frontier) {
            ResumePendingEntry pending;
            pending.invocation_ordinal = InvocationOrdinal{reader.u64()};
            pending.capability = CoreCapabilityId{reader.u32()};
            pending.source_symbol = reader.u64();
            pending.arg_hash = reader.u64();
            if (reader.failed()) {
                result.diagnostics.push_back(error("resume record pending entry is malformed"));
                return result;
            }
            node.pending = std::move(pending);
        }
        record.nodes.push_back(std::move(node));
    }
    if (reader.failed()) {
        result.diagnostics.push_back(error("resume record body is malformed"));
        return result;
    }
    if (!reader.at_end()) {
        result.diagnostics.push_back(error("resume record body has trailing bytes"));
        return result;
    }

    // Reconstruct the authenticated auth_header into the model.
    ResumeAuthHeader header;
    header.alg_version = kAlgHmacSha256;
    for (std::size_t i = 0; i < kKeyIdBytes; ++i) {
        header.key_id[i] = record_bytes[auth_off + 1 + i];
    }
    std::uint64_t gen = 0;
    for (std::size_t i = 0; i < kGenerationBytes; ++i) {
        gen |= static_cast<std::uint64_t>(record_bytes[auth_off + 1 + kKeyIdBytes + i]) << (8U * i);
    }
    header.generation = gen;
    record.auth_header = header;

    // Structural invariants (fail-closed).
    if (auto bad = validate_model(record); bad.has_value()) {
        result.diagnostics.push_back(std::move(*bad));
        return result;
    }

    // Canonical admission WITHOUT a second HMAC: re-encode the parsed model's
    // canonical body + auth_header and require byte-equality with the already-
    // authenticated prefix [0, tag). The fixed-width auth_header/tag have no
    // alternative canonical form, so the tag verified in pass 1 still holds.
    std::vector<std::uint8_t> canonical;
    encode_body(canonical, record);
    encode_auth_header(canonical, record.auth_header);
    if (canonical.size() != prefix.size() ||
        !std::equal(canonical.begin(), canonical.end(), prefix.begin())) {
        result.diagnostics.push_back(error("resume record is not canonical"));
        return result;
    }

    result.record = std::move(record);
    return result;
}

} // namespace ahfl::runtime::core_wasm_resume
