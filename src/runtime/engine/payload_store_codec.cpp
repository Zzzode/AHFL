#include "runtime/engine/payload_store_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

#include "base/support/sha256.hpp"

namespace ahfl::runtime::payload_store {
namespace {

using ir::core::CoreWorkflowId;

// Raw payload magics: one distinct 6-byte magic per artifact class. The leading
// authenticated `magic || format_version` is the HMAC domain separator, so distinct
// magics give the three classes distinct HMAC domains. These are distinct from the
// A1 "AHFLWR", A2 "AHFLXM", and wire-schema "AHFLWS" magics.
constexpr std::array<std::uint8_t, 6> kSlotMagic = {'A', 'H', 'F', 'L', 'P', 'S'};
constexpr std::array<std::uint8_t, 6> kManifestMagic = {'A', 'H', 'F', 'L', 'C', 'M'};
constexpr std::array<std::uint8_t, 6> kPointerMagic = {'A', 'H', 'F', 'L', 'G', 'P'};

constexpr std::uint8_t kFormatVersion = 1;
constexpr std::uint8_t kAlgHmacSha256 = 1;

// Manifest state discriminator bytes.
constexpr std::uint8_t kStateAvailable = 0;
constexpr std::uint8_t kStateConsumed = 1;

// Fixed tail: auth_header (alg_version u8 + key_id 16 + generation u64-LE) = 25,
// plus the 32-byte tag.
constexpr std::size_t kKeyIdBytes = 16;
constexpr std::size_t kGenerationBytes = 8;
constexpr std::size_t kAuthHeaderBytes = 1 + kKeyIdBytes + kGenerationBytes; // 25
constexpr std::size_t kTagBytes = 32;
constexpr std::size_t kTailBytes = kAuthHeaderBytes + kTagBytes; // 57
constexpr std::size_t kDigestHexBytes = 64;

// Coarse pre-auth lower bounds (magic + format_version + fixed body fields), used
// only to reject an impossibly short input before authentication; the real
// structure is validated in pass 2.
//   slot:     magic + fmt + wf(>=1) + ckpt(>=1) + gen(8) + slot(>=1) + len(>=1)
constexpr std::size_t kMinSlotBody = 6 + 1 + 1 + 1 + kGenerationBytes + 1 + 1;
//   manifest: magic + fmt + wf(>=1) + ckpt(>=1) + gen(8) + state(1)
constexpr std::size_t kMinManifestBody = 6 + 1 + 1 + 1 + kGenerationBytes + 1;
//   pointer:  magic + fmt + wf(>=1) + ckpt(>=1) + current_gen(8) + digest(64)
constexpr std::size_t kMinPointerBody = 6 + 1 + 1 + 1 + kGenerationBytes + kDigestHexBytes;

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

// The single fixed-width numeric: generation, u64 little-endian.
void write_u64_le(std::vector<std::uint8_t> &out, std::uint64_t value) {
    for (std::size_t i = 0; i < kGenerationBytes; ++i) {
        out.push_back(static_cast<std::uint8_t>(value & 0xffU));
        value >>= 8U;
    }
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
    [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
    [[nodiscard]] bool at_end() const noexcept { return pos_ == data_.size(); }

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

    // A fixed-width u64 little-endian (the generation encoding).
    [[nodiscard]] std::uint64_t u64_le() noexcept {
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < kGenerationBytes; ++i) {
            const std::uint8_t b = byte();
            if (failed_) {
                return 0;
            }
            value |= static_cast<std::uint64_t>(b) << (8U * i);
        }
        return value;
    }

  private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_{0};
    bool failed_{false};
};

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

// ---- shared header helpers -------------------------------------------------

void encode_auth_header(std::vector<std::uint8_t> &out, const AuthHeader &h) {
    out.push_back(h.alg_version);
    out.insert(out.end(), h.key_id.begin(), h.key_id.end());
    write_u64_le(out, h.generation);
}

// Compute the tag over the exact prefix and finish the artifact. Normal, supported
// inputs stay far below `hmac_sha256`'s length domain, so the length-guard throw is
// not expected in practice; the boundary is still handled explicitly. Both the
// encoders and decoders map ONLY `std::length_error` to a fail-closed IntegrityFailed
// and let any unrelated exception (e.g. bad_alloc) propagate.
[[nodiscard]] std::expected<void, PayloadStoreError>
append_tag(std::vector<std::uint8_t> &prefix, std::span<const std::uint8_t> key) {
    support::Sha256Digest tag;
    try {
        tag = support::hmac_sha256(key, prefix);
    } catch (const std::length_error &) {
        return std::unexpected(PayloadStoreError::IntegrityFailed);
    }
    prefix.insert(prefix.end(), tag.begin(), tag.end());
    return {};
}

// Pass 1 result: the authenticated body span `[0, auth_off)` and the reconstructed
// auth_header. Returns an error variant on any framing / key_id / tag failure.
struct FramingCheck {
    std::size_t auth_off{0};
    AuthHeader auth{};
};

// Two-pass framing + authentication shared by all three decoders. `magic` selects
// the artifact class (domain). On success the caller decodes the body span
// `bytes.first(result.auth_off)` in a trusted pass 2.
[[nodiscard]] std::expected<FramingCheck, PayloadStoreError>
authenticate(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, 6> &magic,
             std::size_t min_body, std::span<const std::uint8_t, 16> expected_key_id,
             std::span<const std::uint8_t> key) {
    if (bytes.size() < min_body + kTailBytes) {
        return std::unexpected(PayloadStoreError::Truncated);
    }
    for (std::size_t i = 0; i < magic.size(); ++i) {
        if (bytes[i] != magic[i]) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
    }
    if (bytes[magic.size()] != kFormatVersion) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    const std::size_t total = bytes.size();
    const std::size_t tag_off = total - kTagBytes;
    const std::size_t auth_off = total - kTailBytes;
    if (bytes[auth_off] != kAlgHmacSha256) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    const std::span<const std::uint8_t> record_key_id = bytes.subspan(auth_off + 1, kKeyIdBytes);
    if (!support::fixed_work_equal(record_key_id, expected_key_id)) {
        return std::unexpected(PayloadStoreError::KeyIdMismatch);
    }
    // Authenticity: single HMAC over the exact on-wire prefix [0, tag). Normal,
    // supported inputs stay far below the HMAC length domain; still map ONLY the
    // length-domain throw to a fail-closed error and let any unrelated exception
    // (e.g. bad_alloc) propagate.
    const std::span<const std::uint8_t> prefix = bytes.first(tag_off);
    support::Sha256Digest expected_tag;
    try {
        expected_tag = support::hmac_sha256(key, prefix);
    } catch (const std::length_error &) {
        return std::unexpected(PayloadStoreError::IntegrityFailed);
    }
    const std::span<const std::uint8_t> record_tag = bytes.subspan(tag_off, kTagBytes);
    if (!support::fixed_work_equal(expected_tag, record_tag)) {
        return std::unexpected(PayloadStoreError::IntegrityFailed);
    }
    AuthHeader auth;
    auth.alg_version = kAlgHmacSha256;
    for (std::size_t i = 0; i < kKeyIdBytes; ++i) {
        auth.key_id[i] = bytes[auth_off + 1 + i];
    }
    std::uint64_t gen = 0;
    for (std::size_t i = 0; i < kGenerationBytes; ++i) {
        gen |= static_cast<std::uint64_t>(bytes[auth_off + 1 + kKeyIdBytes + i]) << (8U * i);
    }
    auth.generation = gen;
    return FramingCheck{auth_off, auth};
}

// Consume the leading `magic || format_version` bytes so body offsets line up (both
// already validated by pass 1).
void consume_magic(Reader &reader, const std::array<std::uint8_t, 6> &magic) {
    for (std::size_t i = 0; i < magic.size(); ++i) {
        (void)reader.byte();
    }
    (void)reader.byte(); // format_version
}

// ---- structural validation (shared by encoder and decoder) -----------------

[[nodiscard]] std::optional<PayloadStoreError> validate_slot(const SlotArtifact &s) {
    if (s.auth.alg_version != kAlgHmacSha256) {
        return PayloadStoreError::Malformed;
    }
    if (s.wf.value == CoreWorkflowId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (s.ckpt.value == ResumeCheckpointId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (s.slot.value == PayloadSlotId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (s.generation == 0) {
        return PayloadStoreError::Malformed;
    }
    if (s.generation != s.auth.generation) {
        return PayloadStoreError::GenerationMismatch;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PayloadStoreError> validate_manifest(const CommitManifest &m) {
    if (m.auth.alg_version != kAlgHmacSha256) {
        return PayloadStoreError::Malformed;
    }
    if (m.wf.value == CoreWorkflowId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (m.ckpt.value == ResumeCheckpointId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (m.generation == 0) {
        return PayloadStoreError::Malformed;
    }
    if (m.generation != m.auth.generation) {
        return PayloadStoreError::GenerationMismatch;
    }
    if (const auto *avail = std::get_if<AvailableBody>(&m.state)) {
        if (!is_lowercase_hex(avail->record_sha256)) {
            return PayloadStoreError::Malformed;
        }
        if (avail->slots.size() > std::numeric_limits<std::uint32_t>::max()) {
            return PayloadStoreError::Malformed;
        }
        // slot_meta strictly ascending by slot id, no duplicates. This is a
        // self-contained B0 rule; exact set-equality against the A1-derived
        // expected set is a B1 cross-artifact admission check, not here.
        for (std::size_t i = 0; i < avail->slots.size(); ++i) {
            const SlotMeta &meta = avail->slots[i];
            if (meta.slot.value == PayloadSlotId::kInvalid) {
                return PayloadStoreError::Malformed;
            }
            if (!is_lowercase_hex(meta.slot_artifact_sha256)) {
                return PayloadStoreError::Malformed;
            }
            if (i > 0 && !(avail->slots[i - 1].slot.value < meta.slot.value)) {
                return PayloadStoreError::SlotSetMismatch;
            }
        }
    } else {
        const auto &consumed = std::get<ConsumedBody>(m.state);
        if (!is_lowercase_hex(consumed.consumed_manifest_sha256)) {
            return PayloadStoreError::Malformed;
        }
        // A Consumed tombstone binds a strictly earlier Available generation. The
        // tighter current-1 / live-digest binding is a B1 store rule.
        if (consumed.consumed_generation == 0 ||
            consumed.consumed_generation >= m.generation) {
            return PayloadStoreError::GenerationMismatch;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<PayloadStoreError> validate_pointer(const GenerationPointer &p) {
    if (p.auth.alg_version != kAlgHmacSha256) {
        return PayloadStoreError::Malformed;
    }
    if (p.wf.value == CoreWorkflowId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (p.ckpt.value == ResumeCheckpointId::kInvalid) {
        return PayloadStoreError::Malformed;
    }
    if (p.current_generation == 0) {
        return PayloadStoreError::Malformed;
    }
    if (p.current_generation != p.auth.generation) {
        return PayloadStoreError::GenerationMismatch;
    }
    if (!is_lowercase_hex(p.manifest_sha256)) {
        return PayloadStoreError::Malformed;
    }
    return std::nullopt;
}

// ---- body encoders ---------------------------------------------------------

void encode_slot_body(std::vector<std::uint8_t> &out, const SlotArtifact &s) {
    out.insert(out.end(), kSlotMagic.begin(), kSlotMagic.end());
    out.push_back(kFormatVersion);
    write_u32(out, s.wf.value);
    write_u64(out, s.ckpt.value);
    write_u64_le(out, s.generation);
    write_u64(out, s.slot.value);
    write_u64(out, static_cast<std::uint64_t>(s.payload.size()));
    out.insert(out.end(), s.payload.begin(), s.payload.end());
}

void encode_manifest_body(std::vector<std::uint8_t> &out, const CommitManifest &m) {
    out.insert(out.end(), kManifestMagic.begin(), kManifestMagic.end());
    out.push_back(kFormatVersion);
    write_u32(out, m.wf.value);
    write_u64(out, m.ckpt.value);
    write_u64_le(out, m.generation);
    if (const auto *avail = std::get_if<AvailableBody>(&m.state)) {
        out.push_back(kStateAvailable);
        write_hex(out, avail->record_sha256);
        write_u64(out, avail->record_len);
        write_u64(out, static_cast<std::uint64_t>(avail->slots.size()));
        for (const SlotMeta &meta : avail->slots) {
            write_u64(out, meta.slot.value);
            write_hex(out, meta.slot_artifact_sha256);
            write_u64(out, meta.payload_len);
        }
    } else {
        const auto &consumed = std::get<ConsumedBody>(m.state);
        out.push_back(kStateConsumed);
        write_u64_le(out, consumed.consumed_generation);
        write_hex(out, consumed.consumed_manifest_sha256);
    }
}

void encode_pointer_body(std::vector<std::uint8_t> &out, const GenerationPointer &p) {
    out.insert(out.end(), kPointerMagic.begin(), kPointerMagic.end());
    out.push_back(kFormatVersion);
    write_u32(out, p.wf.value);
    write_u64(out, p.ckpt.value);
    write_u64_le(out, p.current_generation);
    write_hex(out, p.manifest_sha256);
}

} // namespace

// ---- encoders --------------------------------------------------------------

std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_slot(const SlotArtifact &slot, std::span<const std::uint8_t> key) {
    if (auto bad = validate_slot(slot); bad.has_value()) {
        return std::unexpected(*bad);
    }
    std::vector<std::uint8_t> prefix;
    encode_slot_body(prefix, slot);
    encode_auth_header(prefix, slot.auth);
    if (auto sealed = append_tag(prefix, key); !sealed.has_value()) {
        return std::unexpected(sealed.error());
    }
    return prefix;
}

std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_manifest(const CommitManifest &manifest, std::span<const std::uint8_t> key) {
    if (auto bad = validate_manifest(manifest); bad.has_value()) {
        return std::unexpected(*bad);
    }
    std::vector<std::uint8_t> prefix;
    encode_manifest_body(prefix, manifest);
    encode_auth_header(prefix, manifest.auth);
    if (auto sealed = append_tag(prefix, key); !sealed.has_value()) {
        return std::unexpected(sealed.error());
    }
    return prefix;
}

std::expected<std::vector<std::uint8_t>, PayloadStoreError>
encode_pointer(const GenerationPointer &pointer, std::span<const std::uint8_t> key) {
    if (auto bad = validate_pointer(pointer); bad.has_value()) {
        return std::unexpected(*bad);
    }
    std::vector<std::uint8_t> prefix;
    encode_pointer_body(prefix, pointer);
    encode_auth_header(prefix, pointer.auth);
    if (auto sealed = append_tag(prefix, key); !sealed.has_value()) {
        return std::unexpected(sealed.error());
    }
    return prefix;
}

// ---- decoders --------------------------------------------------------------

std::expected<SlotArtifact, PayloadStoreError>
decode_slot(std::span<const std::uint8_t> bytes, std::span<const std::uint8_t, 16> expected_key_id,
            std::span<const std::uint8_t> key, std::uint64_t max_slot_artifact_bytes) {
    if (bytes.size() > max_slot_artifact_bytes) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    auto framing = authenticate(bytes, kSlotMagic, kMinSlotBody, expected_key_id, key);
    if (!framing.has_value()) {
        return std::unexpected(framing.error());
    }
    const std::span<const std::uint8_t> prefix = bytes.first(bytes.size() - kTagBytes);

    SlotArtifact slot;
    Reader reader(bytes.first(framing->auth_off)); // body only
    consume_magic(reader, kSlotMagic);
    slot.wf = CoreWorkflowId{reader.u32()};
    slot.ckpt = ResumeCheckpointId{reader.u64()};
    slot.generation = reader.u64_le();
    slot.slot = PayloadSlotId{reader.u64()};
    const std::uint64_t payload_len = reader.u64();
    if (reader.failed()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    // Count-before-reserve: the declared payload cannot exceed the remaining body,
    // and the u64 length must fit both size_t and the vector's own max_size before a
    // reserve so `reserve` itself can never throw length_error.
    if (payload_len > reader.remaining()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (payload_len > slot.payload.max_size()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    slot.payload.reserve(static_cast<std::size_t>(payload_len));
    for (std::uint64_t i = 0; i < payload_len; ++i) {
        slot.payload.push_back(reader.byte());
    }
    if (reader.failed()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (!reader.at_end()) {
        return std::unexpected(PayloadStoreError::TrailingBytes);
    }
    slot.auth = framing->auth;
    if (auto bad = validate_slot(slot); bad.has_value()) {
        return std::unexpected(*bad);
    }
    // Canonical admission WITHOUT a second HMAC.
    std::vector<std::uint8_t> canonical;
    encode_slot_body(canonical, slot);
    encode_auth_header(canonical, slot.auth);
    if (canonical.size() != prefix.size() ||
        !std::equal(canonical.begin(), canonical.end(), prefix.begin())) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    return slot;
}

std::expected<CommitManifest, PayloadStoreError>
decode_manifest(std::span<const std::uint8_t> bytes,
                std::span<const std::uint8_t, 16> expected_key_id,
                std::span<const std::uint8_t> key, std::uint64_t max_manifest_artifact_bytes) {
    if (bytes.size() > max_manifest_artifact_bytes) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    auto framing = authenticate(bytes, kManifestMagic, kMinManifestBody, expected_key_id, key);
    if (!framing.has_value()) {
        return std::unexpected(framing.error());
    }
    const std::span<const std::uint8_t> prefix = bytes.first(bytes.size() - kTagBytes);

    CommitManifest manifest;
    Reader reader(bytes.first(framing->auth_off)); // body only
    consume_magic(reader, kManifestMagic);
    manifest.wf = CoreWorkflowId{reader.u32()};
    manifest.ckpt = ResumeCheckpointId{reader.u64()};
    manifest.generation = reader.u64_le();
    const std::uint8_t state = reader.byte();
    if (reader.failed()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (state == kStateAvailable) {
        AvailableBody avail;
        if (!read_hex(reader, avail.record_sha256)) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        avail.record_len = reader.u64();
        const std::uint64_t slot_count = reader.u64();
        if (reader.failed()) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        // Count-before-reserve: a slot_meta costs at least kMinSlotMetaBytes bytes
        // (slot id >=1 ULEB, 64-byte digest, payload_len >=1 ULEB). Guard BEFORE
        // reserve so a huge count never triggers a large allocation even though the
        // prefix is authenticated, and require the count to fit the vector's own
        // max_size so `reserve` itself can never throw length_error.
        constexpr std::size_t kMinSlotMetaBytes = 1 + kDigestHexBytes + 1;
        if (slot_count > reader.remaining() / kMinSlotMetaBytes) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        if (slot_count > avail.slots.max_size()) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        avail.slots.reserve(static_cast<std::size_t>(slot_count));
        for (std::uint64_t i = 0; i < slot_count; ++i) {
            SlotMeta meta;
            meta.slot = PayloadSlotId{reader.u64()};
            if (!read_hex(reader, meta.slot_artifact_sha256)) {
                return std::unexpected(PayloadStoreError::Malformed);
            }
            meta.payload_len = reader.u64();
            if (reader.failed()) {
                return std::unexpected(PayloadStoreError::Malformed);
            }
            avail.slots.push_back(std::move(meta));
        }
        manifest.state = std::move(avail);
    } else if (state == kStateConsumed) {
        ConsumedBody consumed;
        consumed.consumed_generation = reader.u64_le();
        if (!read_hex(reader, consumed.consumed_manifest_sha256)) {
            return std::unexpected(PayloadStoreError::Malformed);
        }
        manifest.state = std::move(consumed);
    } else {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (reader.failed()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (!reader.at_end()) {
        return std::unexpected(PayloadStoreError::TrailingBytes);
    }
    manifest.auth = framing->auth;
    if (auto bad = validate_manifest(manifest); bad.has_value()) {
        return std::unexpected(*bad);
    }
    // Canonical admission WITHOUT a second HMAC.
    std::vector<std::uint8_t> canonical;
    encode_manifest_body(canonical, manifest);
    encode_auth_header(canonical, manifest.auth);
    if (canonical.size() != prefix.size() ||
        !std::equal(canonical.begin(), canonical.end(), prefix.begin())) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    return manifest;
}

std::expected<GenerationPointer, PayloadStoreError>
decode_pointer(std::span<const std::uint8_t> bytes,
               std::span<const std::uint8_t, 16> expected_key_id,
               std::span<const std::uint8_t> key) {
    if (bytes.size() > kMaxGenerationPointerBytes) {
        return std::unexpected(PayloadStoreError::SizeCapExceeded);
    }
    auto framing = authenticate(bytes, kPointerMagic, kMinPointerBody, expected_key_id, key);
    if (!framing.has_value()) {
        return std::unexpected(framing.error());
    }
    const std::span<const std::uint8_t> prefix = bytes.first(bytes.size() - kTagBytes);

    GenerationPointer pointer;
    Reader reader(bytes.first(framing->auth_off)); // body only
    consume_magic(reader, kPointerMagic);
    pointer.wf = CoreWorkflowId{reader.u32()};
    pointer.ckpt = ResumeCheckpointId{reader.u64()};
    pointer.current_generation = reader.u64_le();
    if (!read_hex(reader, pointer.manifest_sha256)) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (reader.failed()) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    if (!reader.at_end()) {
        return std::unexpected(PayloadStoreError::TrailingBytes);
    }
    pointer.auth = framing->auth;
    if (auto bad = validate_pointer(pointer); bad.has_value()) {
        return std::unexpected(*bad);
    }
    // Canonical admission WITHOUT a second HMAC.
    std::vector<std::uint8_t> canonical;
    encode_pointer_body(canonical, pointer);
    encode_auth_header(canonical, pointer.auth);
    if (canonical.size() != prefix.size() ||
        !std::equal(canonical.begin(), canonical.end(), prefix.begin())) {
        return std::unexpected(PayloadStoreError::Malformed);
    }
    return pointer;
}

} // namespace ahfl::runtime::payload_store
