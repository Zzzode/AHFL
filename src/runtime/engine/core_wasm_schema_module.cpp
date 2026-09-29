#include "runtime/engine/core_wasm_schema_module.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/support/sha256.hpp"

namespace ahfl::runtime::core_wasm_schema_module {

using ir::core::CoreCapabilityId;
using ir::core::CoreDiagnosticSeverity;
using ir::core::CoreLowerDiagnostic;
using ir::core::CoreWireCapabilitySchema;
using ir::core::CoreWireRootKind;
using ir::core::CoreWireRootSelector;
using ir::core::CoreWireSchemaTable;
using ir::core::CoreWorkflowId;
using ir::core::CoreWorkflowNodeId;
using ir::core::VerifiedWireSchemaBinding;

namespace {

// Custom section names (payloads begin with the magics below). Kept in sync with
// the E4-B1 writer / a future B2-C emitter; this reader validates the observed name
// against these constants.
constexpr std::string_view kExecManifestSectionName = "ahfl.wasm-exec-manifest.v1";
constexpr std::string_view kWireSchemaSectionName = "ahfl.wire-schema.v1";
constexpr std::array<std::uint8_t, 6> kExecManifestMagic = {'A', 'H', 'F', 'L', 'X', 'M'};
constexpr std::uint8_t kExecManifestVersion = 1;

// The exact capability import contract (mirrors the C3 inspector).
constexpr std::string_view kCapabilityImportModule = "ahfl_cap";
constexpr std::string_view kCapabilityImportFieldPrefix = "cap_";
constexpr std::uint8_t kImportKindFunction = 0;
constexpr std::uint8_t kFuncTypeForm = 0x60;
constexpr std::uint8_t kValueTypeI32 = 0x7f;

constexpr std::uint8_t kSectionCustom = 0;
constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionImport = 2;
constexpr std::uint8_t kSectionMemory = 5;

// Memory-section limits flag 0 = minimum only (no declared maximum). The fixed
// single-page Core-Wasm contract forbids the flag-1 (min + max) form.
constexpr std::uint8_t kMemoryLimitsFlagNoMax = 0;
constexpr std::uint8_t kMemoryLimitsFlagWithMax = 1;

constexpr std::uint8_t kEntryKindWorkflow = 0;

// Every diagnostic this file raises uses the shared fixed code, a null range, and a
// fixed message that echoes no observed byte, name, digest, or manifest field.
[[nodiscard]] CoreLowerDiagnostic error(std::string message) {
    return CoreLowerDiagnostic{CoreDiagnosticSeverity::Error,
                               std::string(ir::core::wire_schema::kInvalid), std::move(message),
                               std::nullopt};
}

// A bounds-checked forward cursor with a canonical u32 LEB reader (same discipline
// as the E4-B1 C3 inspector; the C3 helpers are private to their TU, so A2 carries
// its own per the "no speculative shared framer" ruling).
class ByteCursor {
  public:
    explicit ByteCursor(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ == bytes_.size(); }

    [[nodiscard]] std::optional<std::uint8_t> byte() noexcept {
        if (remaining() == 0) {
            return std::nullopt;
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] bool match(std::span<const std::uint8_t> literal) noexcept {
        if (remaining() < literal.size()) {
            return false;
        }
        for (std::size_t i = 0; i < literal.size(); ++i) {
            if (bytes_[offset_ + i] != literal[i]) {
                return false;
            }
        }
        offset_ += literal.size();
        return true;
    }

    [[nodiscard]] std::optional<std::uint32_t> u32() noexcept {
        std::uint64_t value = 0;
        std::uint32_t shift = 0;
        while (true) {
            const auto b = byte();
            if (!b.has_value()) {
                return std::nullopt; // truncated
            }
            const std::uint8_t group = *b;
            if (shift == 28 && (group & 0x70u) != 0) {
                return std::nullopt; // does not fit in 32 bits
            }
            value |= static_cast<std::uint64_t>(group & 0x7fu) << shift;
            if ((group & 0x80u) == 0) {
                if (group == 0 && shift != 0) {
                    return std::nullopt; // overlong
                }
                return static_cast<std::uint32_t>(value);
            }
            shift += 7;
            if (shift >= 35) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<std::uint64_t> u64() noexcept {
        std::uint64_t value = 0;
        std::uint32_t shift = 0;
        while (true) {
            const auto b = byte();
            if (!b.has_value()) {
                return std::nullopt; // truncated
            }
            const std::uint8_t group = *b;
            if (shift == 63 && (group & 0x7eu) != 0) {
                return std::nullopt; // does not fit in 64 bits
            }
            value |= static_cast<std::uint64_t>(group & 0x7fu) << shift;
            if ((group & 0x80u) == 0) {
                if (group == 0 && shift != 0) {
                    return std::nullopt; // overlong
                }
                return value;
            }
            shift += 7;
            if (shift >= 70) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<std::span<const std::uint8_t>> take(std::size_t count) noexcept {
        if (remaining() < count) {
            return std::nullopt;
        }
        const auto view = bytes_.subspan(offset_, count);
        offset_ += count;
        return view;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] std::optional<std::uint64_t>
parse_capability_field(std::span<const std::uint8_t> field) noexcept {
    const std::string_view text(reinterpret_cast<const char *>(field.data()), field.size());
    if (text.substr(0, kCapabilityImportFieldPrefix.size()) != kCapabilityImportFieldPrefix) {
        return std::nullopt;
    }
    const std::string_view digits = text.substr(kCapabilityImportFieldPrefix.size());
    if (digits.empty()) {
        return std::nullopt;
    }
    if (digits.size() > 1 && digits.front() == '0') {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : digits) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return std::nullopt;
        }
        value = value * 10u + digit;
    }
    return value;
}

struct ParsedImport {
    std::uint64_t source_symbol = 0;
    std::uint32_t type_index = 0;
};

// One Memory (wasm section id 5) section, structurally parsed during framing. The
// fixed single-page contract is NOT applied here (a section that violates it is
// still framing-valid); the module getter maps it to a typed `MemoryDeclError`.
// `flags` / `min_pages` / `max_pages` are meaningful only when `count == 1`.
struct ParsedMemorySection {
    std::uint32_t count = 0;
    std::uint8_t flags = 0; // 0 = min only, 1 = min + max (MVP limits)
    std::uint32_t min_pages = 0;
    std::uint32_t max_pages = 0; // valid only when flags == 1
};

// Framing facts: per Type entry whether it is the ahfl_cap tuple signature or the
// bridge (i32)->(i32,i32) signature, the capability imports, the raw bytes of
// BOTH target custom sections (borrowed), and the three raw SHA-256 artifact
// digests computed once here after framing succeeds.
struct ModuleFraming {
    std::vector<bool> type_is_capability_tuple;
    // WH-3: the bridge (i32)->(i32,i32) functype, accepted alongside the opaque
    // capability tuple (decision doc section 11.3). The schema table carries no
    // mode field, so A2 cannot distinguish a bridge capability from an opaque
    // one; it accepts BOTH functypes and rejects anything else. The mode
    // cross-check (bridge vs opaque) lives in the frame-section
    // verify_frame_bridge_sites, which sees the bridge_call_sites.
    std::vector<bool> type_is_bridge_signature;
    std::vector<ParsedImport> imports;
    bool have_memory_section = false;
    ParsedMemorySection memory;                        // valid only when have_memory_section
    std::span<const std::uint8_t> exec_manifest_bytes; // raw payload after the name framing
    std::span<const std::uint8_t> wire_schema_bytes;   // raw payload after the name framing
    ArtifactDigest module_sha256{};       // whole emitted module
    ArtifactDigest wire_schema_sha256{};  // raw AHFLWS payload (no custom-name framing)
    ArtifactDigest exec_manifest_sha256{}; // raw AHFLXM payload (no custom-name framing)
};

[[nodiscard]] bool spans_are_capability_tuple(std::span<const std::uint8_t> params,
                                              std::span<const std::uint8_t> results) noexcept {
    static constexpr std::array<std::uint8_t, 2> kParams{kValueTypeI32, kValueTypeI32};
    static constexpr std::array<std::uint8_t, 3> kResults{kValueTypeI32, kValueTypeI32,
                                                          kValueTypeI32};
    if (params.size() != kParams.size() || results.size() != kResults.size()) {
        return false;
    }
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (params[i] != kParams[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kResults.size(); ++i) {
        if (results[i] != kResults[i]) {
            return false;
        }
    }
    return true;
}

// The bridge ahfl_cap function signature: (i32) -> (i32, i32). The P6-frame
// bridge lane (WH-2 String region authorization trusts the declared bridge
// call sites/placements) uses this 2-result shape; A2 admission must accept it
// alongside the opaque 3-result tuple (decision doc section 11.3). Parallel
// copy of the C3 predicate per the "no speculative shared framer" ruling.
[[nodiscard]] bool
spans_are_bridge_signature(std::span<const std::uint8_t> params,
                           std::span<const std::uint8_t> results) noexcept {
    static constexpr std::array<std::uint8_t, 1> kParams{kValueTypeI32};
    static constexpr std::array<std::uint8_t, 2> kResults{kValueTypeI32, kValueTypeI32};
    if (params.size() != kParams.size() || results.size() != kResults.size()) {
        return false;
    }
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        if (params[i] != kParams[i]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < kResults.size(); ++i) {
        if (results[i] != kResults[i]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool parse_type_section(std::span<const std::uint8_t> payload,
                                      ModuleFraming &framing) {
    ByteCursor cursor(payload);
    const auto count = cursor.u32();
    if (!count.has_value()) {
        return false;
    }
    constexpr std::size_t kMinTypeEntryBytes = 3;
    if (*count > cursor.remaining() / kMinTypeEntryBytes) {
        return false;
    }
    framing.type_is_capability_tuple.reserve(*count);
    framing.type_is_bridge_signature.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto form = cursor.byte();
        if (!form.has_value() || *form != kFuncTypeForm) {
            return false;
        }
        const auto param_count = cursor.u32();
        if (!param_count.has_value()) {
            return false;
        }
        const auto params = cursor.take(*param_count);
        if (!params.has_value()) {
            return false;
        }
        const auto result_count = cursor.u32();
        if (!result_count.has_value()) {
            return false;
        }
        const auto results = cursor.take(*result_count);
        if (!results.has_value()) {
            return false;
        }
        framing.type_is_capability_tuple.push_back(spans_are_capability_tuple(*params, *results));
        framing.type_is_bridge_signature.push_back(spans_are_bridge_signature(*params, *results));
    }
    return cursor.at_end();
}

[[nodiscard]] bool parse_import_section(std::span<const std::uint8_t> payload,
                                        ModuleFraming &framing) {
    ByteCursor cursor(payload);
    const auto count = cursor.u32();
    if (!count.has_value()) {
        return false;
    }
    constexpr std::size_t kMinImportEntryBytes = 4;
    if (*count > cursor.remaining() / kMinImportEntryBytes) {
        return false;
    }
    framing.imports.reserve(*count);
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto module_len = cursor.u32();
        if (!module_len.has_value()) {
            return false;
        }
        const auto module_name = cursor.take(*module_len);
        if (!module_name.has_value()) {
            return false;
        }
        const std::string_view module_view(reinterpret_cast<const char *>(module_name->data()),
                                           module_name->size());
        if (module_view != kCapabilityImportModule) {
            return false;
        }
        const auto field_len = cursor.u32();
        if (!field_len.has_value()) {
            return false;
        }
        const auto field_name = cursor.take(*field_len);
        if (!field_name.has_value()) {
            return false;
        }
        const auto kind = cursor.byte();
        if (!kind.has_value() || *kind != kImportKindFunction) {
            return false;
        }
        const auto type_index = cursor.u32();
        if (!type_index.has_value()) {
            return false;
        }
        const auto source_symbol = parse_capability_field(*field_name);
        if (!source_symbol.has_value()) {
            return false;
        }
        framing.imports.push_back(ParsedImport{*source_symbol, *type_index});
    }
    return cursor.at_end();
}

// Parse the Memory (wasm section id 5) section structurally: a vector count
// followed by one limits entry each (MVP flags 0 = min only, 1 = min + max).
// Only the first entry is retained because the fixed single-page contract
// requires exactly one memory; the section is still exact-consumed so a count
// above one with well-formed entries frames successfully and is rejected later
// by the getter with MemoryCountNotOne, while ANY malformed encoding fails
// framing like the Type/Import sections.
[[nodiscard]] bool parse_memory_section(std::span<const std::uint8_t> payload,
                                        ModuleFraming &framing) {
    ByteCursor cursor(payload);
    const auto count = cursor.u32();
    if (!count.has_value()) {
        return false;
    }
    constexpr std::size_t kMinMemoryEntryBytes = 2; // flags byte + >=1 min byte
    if (*count > cursor.remaining() / kMinMemoryEntryBytes) {
        return false;
    }
    for (std::uint32_t i = 0; i < *count; ++i) {
        const auto flags = cursor.byte();
        if (!flags.has_value() ||
            (*flags != kMemoryLimitsFlagNoMax && *flags != kMemoryLimitsFlagWithMax)) {
            return false; // reserved/shared flags (2..) or truncation
        }
        const auto min_pages = cursor.u32();
        if (!min_pages.has_value()) {
            return false;
        }
        std::uint32_t max_pages = 0;
        if (*flags == kMemoryLimitsFlagWithMax) {
            const auto parsed_max = cursor.u32();
            if (!parsed_max.has_value() || *parsed_max < *min_pages) {
                return false; // wasm validation: max >= min
            }
            max_pages = *parsed_max;
        }
        if (i == 0) {
            framing.memory.count = *count;
            framing.memory.flags = *flags;
            framing.memory.min_pages = *min_pages;
            framing.memory.max_pages = max_pages;
        }
    }
    return cursor.at_end();
}

// Walk the module: header + Type/Import/Memory (exact-consume, in wasm section
// order) + size-skip every other non-custom section. The exec-manifest custom section
// (`ahfl.wasm-exec-manifest.v1`) must appear EXACTLY ONCE IMMEDIATELY BEFORE the
// wire-schema custom section (`ahfl.wire-schema.v1`), which must be the module's
// FINAL section at EOF. A missing, duplicated, misordered, misnamed, or
// bytes-after-target section fails closed.
[[nodiscard]] std::optional<ModuleFraming>
frame_module(std::span<const std::uint8_t> module_bytes,
             std::vector<CoreLowerDiagnostic> &diagnostics) {
    static constexpr std::array<std::uint8_t, 8> kModuleHeader{0x00, 0x61, 0x73, 0x6d,
                                                              0x01, 0x00, 0x00, 0x00};
    ByteCursor cursor(module_bytes);
    if (!cursor.match(kModuleHeader)) {
        diagnostics.push_back(error("module header is not wasm v1"));
        return std::nullopt;
    }

    ModuleFraming framing;
    bool have_type = false;
    bool have_import = false;
    bool have_manifest = false;
    bool have_schema = false;
    std::span<const std::uint8_t> exec_manifest_bytes;
    std::span<const std::uint8_t> wire_schema_bytes;

    while (!cursor.at_end()) {
        // The wire-schema custom section must be the module's FINAL section: any
        // bytes after it fail before the next header is read.
        if (have_schema) {
            diagnostics.push_back(error("a section follows the wire-schema custom section"));
            return std::nullopt;
        }
        const auto section_id = cursor.byte();
        if (!section_id.has_value()) {
            diagnostics.push_back(error("truncated module section id"));
            return std::nullopt;
        }
        const auto size = cursor.u32();
        if (!size.has_value()) {
            diagnostics.push_back(error("module section size is not a canonical u32"));
            return std::nullopt;
        }
        const auto payload = cursor.take(*size);
        if (!payload.has_value()) {
            diagnostics.push_back(error("module section exceeds its bounds"));
            return std::nullopt;
        }

        if (*section_id == kSectionCustom) {
            ByteCursor name_cursor(*payload);
            const auto name_len = name_cursor.u32();
            if (!name_len.has_value()) {
                diagnostics.push_back(error("custom section name length is not a canonical u32"));
                return std::nullopt;
            }
            const auto name = name_cursor.take(*name_len);
            if (!name.has_value()) {
                diagnostics.push_back(error("custom section name exceeds its section bounds"));
                return std::nullopt;
            }
            const std::string_view name_view(reinterpret_cast<const char *>(name->data()),
                                             name->size());
            const auto rest = name_cursor.take(name_cursor.remaining());
            if (!rest.has_value()) {
                diagnostics.push_back(error("custom section payload is malformed"));
                return std::nullopt;
            }
            if (name_view == kExecManifestSectionName) {
                if (have_manifest) {
                    diagnostics.push_back(error("module has more than one exec-manifest section"));
                    return std::nullopt;
                }
                exec_manifest_bytes = *rest;
                have_manifest = true;
                continue;
            }
            if (name_view == kWireSchemaSectionName) {
                // Placement: the exec-manifest must appear IMMEDIATELY BEFORE the
                // wire-schema section (no other custom section between them).
                if (!have_manifest) {
                    diagnostics.push_back(
                        error("wire-schema section is not immediately preceded by the "
                              "exec-manifest section"));
                    return std::nullopt;
                }
                wire_schema_bytes = *rest;
                have_schema = true;
                continue;
            }
            // An unknown custom section between the manifest and the schema would
            // break the immediately-before invariant; reject it once the manifest is
            // seen. Before the manifest, an unknown custom is skipped (canonical
            // framing already validated).
            if (have_manifest) {
                diagnostics.push_back(
                    error("an unexpected custom section separates the manifest and schema"));
                return std::nullopt;
            }
            continue;
        }

        if (*section_id == kSectionType) {
            if (have_type) {
                diagnostics.push_back(error("module has more than one Type section"));
                return std::nullopt;
            }
            if (have_import) {
                diagnostics.push_back(error("module Type section follows its Import section"));
                return std::nullopt;
            }
            if (have_manifest) {
                diagnostics.push_back(
                    error("module Type section follows the exec-manifest section"));
                return std::nullopt;
            }
            if (!parse_type_section(*payload, framing)) {
                diagnostics.push_back(error("module Type section is malformed"));
                return std::nullopt;
            }
            have_type = true;
            continue;
        }

        if (*section_id == kSectionImport) {
            if (have_import) {
                diagnostics.push_back(error("module has more than one Import section"));
                return std::nullopt;
            }
            if (have_manifest) {
                diagnostics.push_back(
                    error("module Import section follows the exec-manifest section"));
                return std::nullopt;
            }
            if (!parse_import_section(*payload, framing)) {
                diagnostics.push_back(error("module Import section is malformed"));
                return std::nullopt;
            }
            have_import = true;
            continue;
        }

        if (*section_id == kSectionMemory) {
            if (framing.have_memory_section) {
                diagnostics.push_back(error("module has more than one Memory section"));
                return std::nullopt;
            }
            if (have_manifest) {
                diagnostics.push_back(
                    error("module Memory section follows the exec-manifest section"));
                return std::nullopt;
            }
            if (!parse_memory_section(*payload, framing)) {
                diagnostics.push_back(error("module Memory section is malformed"));
                return std::nullopt;
            }
            framing.have_memory_section = true;
            continue;
        }

        // Any other non-custom section is size-skipped. A standard section that
        // appears after the manifest would violate the manifest-immediately-before-
        // schema placement, so reject it.
        if (have_manifest) {
            diagnostics.push_back(error("a section follows the exec-manifest section"));
            return std::nullopt;
        }
    }

    if (!have_type) {
        diagnostics.push_back(error("module has no Type section"));
        return std::nullopt;
    }
    if (!have_import) {
        diagnostics.push_back(error("module has no capability Import section"));
        return std::nullopt;
    }
    if (!have_manifest) {
        diagnostics.push_back(error("module has no exec-manifest section"));
        return std::nullopt;
    }
    if (!have_schema) {
        diagnostics.push_back(error("module has no wire-schema custom section"));
        return std::nullopt;
    }
    framing.exec_manifest_bytes = exec_manifest_bytes;
    framing.wire_schema_bytes = wire_schema_bytes;
    // Compute the three raw SHA-256 artifact digests ONCE here, now that type /
    // import / AHFLXM / AHFLWS framing have all succeeded, in the fixed order whole
    // module -> raw AHFLWS payload -> raw AHFLXM payload. support::sha256 throws
    // std::length_error only for a span outside the SHA-256 input domain (>= 2^61
    // bytes), which a real module can never reach; map it to a fixed no-echo
    // fail-closed diagnostic so the exception never escapes admission.
    try {
        framing.module_sha256 = support::sha256(module_bytes);
        framing.wire_schema_sha256 = support::sha256(wire_schema_bytes);
        framing.exec_manifest_sha256 = support::sha256(exec_manifest_bytes);
    } catch (const std::length_error &) {
        diagnostics.push_back(error("module artifact exceeds the SHA-256 input domain"));
        return std::nullopt;
    }
    return framing;
}

// One decoded exec-manifest node.
struct ManifestNode {
    CoreWorkflowNodeId workflow_node_id{};
    std::uint32_t schedule_pos = 0;
    std::uint8_t cap_call_count = 0;
    CoreCapabilityId capability{}; // valid only when cap_call_count == 1
    std::uint64_t source_symbol = 0;
};

struct DecodedManifest {
    CoreWorkflowId entry_id{};
    std::vector<ManifestNode> nodes;
};

// Decode + canonically re-encode-check the exec-manifest payload (AHFLXM). Mirrors
// the wire-schema / resume-record discipline: canonical ULEB, count-before-reserve,
// exact EOF. Structural self-invariants (schedule_pos dense, node id unique, enums
// in set, sentinels rejected) are enforced here; cross-check against schema/import
// happens in the factory.
[[nodiscard]] std::optional<DecodedManifest>
decode_exec_manifest(std::span<const std::uint8_t> bytes,
                     std::vector<CoreLowerDiagnostic> &diagnostics) {
    ByteCursor cursor(bytes);
    static constexpr std::span<const std::uint8_t> kMagicSpan(kExecManifestMagic);
    if (!cursor.match(kMagicSpan)) {
        diagnostics.push_back(error("exec-manifest has a bad magic header"));
        return std::nullopt;
    }
    const auto version = cursor.byte();
    if (!version.has_value() || *version != kExecManifestVersion) {
        diagnostics.push_back(error("exec-manifest has an unsupported version"));
        return std::nullopt;
    }
    const auto entry_kind = cursor.byte();
    if (!entry_kind.has_value() || *entry_kind != kEntryKindWorkflow) {
        diagnostics.push_back(error("exec-manifest has an unsupported entry kind"));
        return std::nullopt;
    }
    const auto entry_id = cursor.u32();
    if (!entry_id.has_value() || *entry_id == CoreWorkflowId::kInvalid) {
        diagnostics.push_back(error("exec-manifest entry id is missing or the invalid sentinel"));
        return std::nullopt;
    }
    const auto node_count = cursor.u32();
    if (!node_count.has_value()) {
        diagnostics.push_back(error("exec-manifest node count is malformed"));
        return std::nullopt;
    }
    // Count-before-reserve: min node entry = workflow_node_id + schedule_pos +
    // cap_call_count >= 3 bytes.
    constexpr std::size_t kMinNodeEntryBytes = 3;
    if (*node_count > cursor.remaining() / kMinNodeEntryBytes) {
        diagnostics.push_back(error("exec-manifest declares more nodes than remaining bytes"));
        return std::nullopt;
    }
    DecodedManifest manifest;
    manifest.entry_id = CoreWorkflowId{*entry_id};
    manifest.nodes.reserve(*node_count);
    std::unordered_set<std::uint32_t> seen_ids;
    seen_ids.reserve(*node_count);
    for (std::uint32_t i = 0; i < *node_count; ++i) {
        ManifestNode node;
        const auto node_id = cursor.u32();
        if (!node_id.has_value() || *node_id == CoreWorkflowNodeId::kInvalid) {
            diagnostics.push_back(
                error("exec-manifest node id is missing or the invalid sentinel"));
            return std::nullopt;
        }
        node.workflow_node_id = CoreWorkflowNodeId{*node_id};
        const auto schedule_pos = cursor.u32();
        if (!schedule_pos.has_value() || *schedule_pos != i) {
            diagnostics.push_back(
                error("exec-manifest schedule_pos is not equal to the node index"));
            return std::nullopt;
        }
        node.schedule_pos = *schedule_pos;
        if (!seen_ids.insert(*node_id).second) {
            diagnostics.push_back(error("exec-manifest node id is not unique"));
            return std::nullopt;
        }
        const auto cap_call_count = cursor.byte();
        if (!cap_call_count.has_value() || *cap_call_count > 1) {
            diagnostics.push_back(error("exec-manifest cap_call_count is out of range"));
            return std::nullopt;
        }
        node.cap_call_count = *cap_call_count;
        if (*cap_call_count == 1) {
            const auto capability = cursor.u32();
            if (!capability.has_value() || *capability == CoreCapabilityId::kInvalid) {
                diagnostics.push_back(
                    error("exec-manifest capability is missing or the invalid sentinel"));
                return std::nullopt;
            }
            node.capability = CoreCapabilityId{*capability};
            const auto source_symbol = cursor.u64();
            if (!source_symbol.has_value()) {
                diagnostics.push_back(error("exec-manifest source symbol is malformed"));
                return std::nullopt;
            }
            node.source_symbol = *source_symbol;
        }
        manifest.nodes.push_back(node);
    }
    if (!cursor.at_end()) {
        diagnostics.push_back(error("exec-manifest has trailing bytes"));
        return std::nullopt;
    }
    return manifest;
}

// Canonical re-encode of a decoded manifest (mirror of the grammar). Used for the
// byte-equality gate so shortest-form / field-order canonicality is authoritative.
void encode_exec_manifest(std::vector<std::uint8_t> &out, const DecodedManifest &m);

} // namespace

// ---- shared immutable payload -----------------------------------------------

namespace detail {

// One pre-minted capability call site.
struct CallSiteRecord {
    std::size_t node_index = 0; // index into nodes
    CapabilityImportOrdinal import_ordinal{};
    VerifiedWireSchemaBinding param;
    VerifiedWireSchemaBinding result;
};

// The single immutable payload shared by the module handle and every node /
// call-site token it resolves. All three handles hold a
// `std::shared_ptr<const SchemaModulePayload>` to THIS one type.
struct SchemaModulePayload {
    CoreWorkflowId entry_id{};
    std::vector<ManifestNode> nodes;
    std::vector<CallSiteRecord> call_sites;
    // The structurally-parsed Memory (id 5) section, plus whether the module
    // carries one. `have_memory_section == false` yields MissingMemorySection
    // from the capacity getter; a present-but-nonconforming declaration yields
    // the matching MemoryDeclError. Admission itself does not require it.
    bool have_memory_section = false;
    ParsedMemorySection memory;
    // The three raw SHA-256 artifact digests, computed once during framing and
    // shared (never re-hashed) by every token resolved from this payload.
    ArtifactDigest module_sha256{};
    ArtifactDigest wire_schema_sha256{};
    ArtifactDigest exec_manifest_sha256{};
};

} // namespace detail

namespace {

void encode_exec_manifest(std::vector<std::uint8_t> &out, const DecodedManifest &m) {
    const auto put_u32 = [&out](std::uint32_t v) {
        do {
            auto b = static_cast<std::uint8_t>(v & 0x7fU);
            v >>= 7U;
            if (v != 0) {
                b |= 0x80U;
            }
            out.push_back(b);
        } while (v != 0);
    };
    const auto put_u64 = [&out](std::uint64_t v) {
        do {
            auto b = static_cast<std::uint8_t>(v & 0x7fU);
            v >>= 7U;
            if (v != 0) {
                b |= 0x80U;
            }
            out.push_back(b);
        } while (v != 0);
    };
    out.insert(out.end(), kExecManifestMagic.begin(), kExecManifestMagic.end());
    out.push_back(kExecManifestVersion);
    out.push_back(kEntryKindWorkflow);
    put_u32(m.entry_id.value);
    put_u32(static_cast<std::uint32_t>(m.nodes.size()));
    for (const ManifestNode &node : m.nodes) {
        put_u32(node.workflow_node_id.value);
        put_u32(node.schedule_pos);
        out.push_back(node.cap_call_count);
        if (node.cap_call_count == 1) {
            put_u32(node.capability.value);
            put_u64(node.source_symbol);
        }
    }
}

} // namespace

// ---- result has_errors ------------------------------------------------------

namespace {
[[nodiscard]] bool any_error(const std::vector<CoreLowerDiagnostic> &d) noexcept {
    return std::any_of(d.begin(), d.end(), [](const CoreLowerDiagnostic &x) {
        return x.severity == CoreDiagnosticSeverity::Error;
    });
}
} // namespace

bool VerifiedCoreWasmNodeResult::has_errors() const noexcept { return any_error(diagnostics); }
bool VerifiedCoreWasmCallSiteResult::has_errors() const noexcept { return any_error(diagnostics); }
bool VerifiedCoreWasmSchemaModuleResult::has_errors() const noexcept {
    return any_error(diagnostics);
}

// ---- node / call-site accessors ---------------------------------------------

ir::core::CoreWorkflowNodeId VerifiedCoreWasmNode::workflow_node_id() const noexcept {
    return payload_->nodes[node_index_].workflow_node_id;
}
ManifestNodeIndex VerifiedCoreWasmNode::schedule_pos() const noexcept {
    return ManifestNodeIndex{payload_->nodes[node_index_].schedule_pos};
}
std::uint8_t VerifiedCoreWasmNode::cap_call_count() const noexcept {
    return payload_->nodes[node_index_].cap_call_count;
}

ir::core::CoreWorkflowNodeId VerifiedCoreWasmCallSite::workflow_node_id() const noexcept {
    return payload_->nodes[payload_->call_sites[call_site_index_].node_index].workflow_node_id;
}
ManifestNodeIndex VerifiedCoreWasmCallSite::schedule_pos() const noexcept {
    return ManifestNodeIndex{payload_->call_sites[call_site_index_].node_index};
}
core_wasm_resume::InvocationOrdinal VerifiedCoreWasmCallSite::invocation_ordinal() const noexcept {
    // Today a capability node has exactly one call (cap_call_count == 1), so the
    // per-node invocation ordinal of that single call is 0. The manifest grammar
    // carries no ordinal field; it is derived here.
    return core_wasm_resume::InvocationOrdinal{0};
}
ir::core::CoreCapabilityId VerifiedCoreWasmCallSite::capability() const noexcept {
    return payload_->nodes[payload_->call_sites[call_site_index_].node_index].capability;
}
std::uint64_t VerifiedCoreWasmCallSite::source_symbol() const noexcept {
    return payload_->nodes[payload_->call_sites[call_site_index_].node_index].source_symbol;
}
CapabilityImportOrdinal VerifiedCoreWasmCallSite::import_ordinal() const noexcept {
    return payload_->call_sites[call_site_index_].import_ordinal;
}
VerifiedWireSchemaBinding VerifiedCoreWasmCallSite::param_binding() const {
    return payload_->call_sites[call_site_index_].param; // by value (cheap shared_ptr copy)
}
VerifiedWireSchemaBinding VerifiedCoreWasmCallSite::result_binding() const {
    return payload_->call_sites[call_site_index_].result;
}

// ---- module accessors + resolve ---------------------------------------------

ir::core::CoreWorkflowId VerifiedCoreWasmSchemaModule::entry_id() const noexcept {
    return payload_->entry_id;
}
std::size_t VerifiedCoreWasmSchemaModule::node_count() const noexcept {
    return payload_->nodes.size();
}
std::size_t VerifiedCoreWasmSchemaModule::call_site_count() const noexcept {
    return payload_->call_sites.size();
}
ArtifactDigest VerifiedCoreWasmSchemaModule::module_sha256() const noexcept {
    return payload_->module_sha256;
}
ArtifactDigest VerifiedCoreWasmSchemaModule::wire_schema_sha256() const noexcept {
    return payload_->wire_schema_sha256;
}
ArtifactDigest VerifiedCoreWasmSchemaModule::exec_manifest_sha256() const noexcept {
    return payload_->exec_manifest_sha256;
}

std::expected<ArtifactMemoryCapacity, MemoryDeclError>
VerifiedCoreWasmSchemaModule::declared_linear_memory_capacity() const noexcept {
    if (!payload_->have_memory_section) {
        return std::unexpected(MemoryDeclError::MissingMemorySection);
    }
    const ParsedMemorySection &memory = payload_->memory;
    if (memory.count != 1) {
        return std::unexpected(MemoryDeclError::MemoryCountNotOne);
    }
    if (memory.flags != kMemoryLimitsFlagNoMax) {
        return std::unexpected(MemoryDeclError::DeclaredMaximum);
    }
    if (memory.min_pages != ir::core::kCoreWasmFixedLinearMemoryMinPages) {
        return std::unexpected(MemoryDeclError::MinPagesNotOne);
    }
    // The conforming declaration is exactly one page with no maximum, so its
    // declared floor is the F1 fixed single-page capacity SSOT (never a
    // re-declared 65536).
    return ArtifactMemoryCapacity{ir::core::kCoreWasmFixedLinearMemoryCapacityBytes,
                                  ir::core::kCoreWasmFixedLinearMemoryMinPages,
                                  false};
}

VerifiedCoreWasmNodeResult
VerifiedCoreWasmSchemaModule::resolve_node(ManifestNodeIndex index) const {
    VerifiedCoreWasmNodeResult result;
    if (index.value >= payload_->nodes.size()) {
        result.diagnostics.push_back(error("resolve_node index is out of range"));
        return result;
    }
    result.node = VerifiedCoreWasmNode(payload_, index.value);
    return result;
}

VerifiedCoreWasmCallSiteResult
VerifiedCoreWasmSchemaModule::resolve(ManifestCallSiteIndex index) const {
    VerifiedCoreWasmCallSiteResult result;
    if (index.value >= payload_->call_sites.size()) {
        result.diagnostics.push_back(error("resolve call-site index is out of range"));
        return result;
    }
    result.call_site = VerifiedCoreWasmCallSite(payload_, index.value);
    return result;
}

// ---- factory ----------------------------------------------------------------

struct SchemaModuleFactory {
    [[nodiscard]] static VerifiedCoreWasmSchemaModuleResult
    make(std::span<const std::uint8_t> module_bytes) {
        VerifiedCoreWasmSchemaModuleResult result;

        // 1. Frame the module (header + Type/Import + manifest immediately before
        //    the EOF wire-schema section).
        auto framing = frame_module(module_bytes, result.diagnostics);
        if (!framing.has_value()) {
            return result;
        }

        // 2. Admit the wire-schema table through the C1 authority.
        auto decoded_table = ir::core::decode_core_wire_schema_table(framing->wire_schema_bytes);
        if (!decoded_table.ok() || !decoded_table.table.has_value()) {
            result.diagnostics = std::move(decoded_table.diagnostics);
            if (!result.has_errors()) {
                result.diagnostics.push_back(error("wire-schema table failed C1 admission"));
            }
            return result;
        }
        const CoreWireSchemaTable &table = *decoded_table.table;

        // 3. Decode the exec-manifest.
        auto manifest = decode_exec_manifest(framing->exec_manifest_bytes, result.diagnostics);
        if (!manifest.has_value()) {
            return result;
        }
        // Canonical re-encode byte-equality gate for the manifest.
        std::vector<std::uint8_t> reencoded;
        encode_exec_manifest(reencoded, *manifest);
        if (reencoded.size() != framing->exec_manifest_bytes.size() ||
            !std::equal(reencoded.begin(), reencoded.end(),
                        framing->exec_manifest_bytes.begin())) {
            result.diagnostics.push_back(error("exec-manifest is not canonical"));
            return result;
        }

        // 4. Import <-> schema strict one-to-one cross-check (mirrors C3).
        if (framing->imports.empty()) {
            result.diagnostics.push_back(error("module has a wire-schema section but no imports"));
            return result;
        }
        if (table.capabilities.empty()) {
            result.diagnostics.push_back(error("wire-schema table carries no capability"));
            return result;
        }
        if (framing->imports.size() != table.capabilities.size()) {
            result.diagnostics.push_back(error("module import count does not match the table"));
            return result;
        }
        for (std::size_t i = 0; i < framing->imports.size(); ++i) {
            const auto &imported = framing->imports[i];
            if (imported.source_symbol != table.capabilities[i].source_symbol) {
                result.diagnostics.push_back(
                    error("module import source symbol does not match the table"));
                return result;
            }
            // Every ordinal's capability-tuple/bridge signature must agree before
            // we honor the import. The schema table carries no mode field, so A2
            // accepts BOTH the opaque (i32,i32)->(i32,i32,i32) tuple and the
            // bridge (i32)->(i32,i32) shape; anything else is rejected (decision
            // doc section 11.3).
            const bool type_in_range =
                imported.type_index < framing->type_is_capability_tuple.size();
            const bool is_tuple =
                type_in_range && framing->type_is_capability_tuple[imported.type_index];
            const bool is_bridge =
                type_in_range && framing->type_is_bridge_signature[imported.type_index];
            if (!is_tuple && !is_bridge) {
                result.diagnostics.push_back(error(
                    "module capability import does not use the ahfl_cap tuple or bridge signature"));
                return result;
            }
        }

        // 5. EXACT set equality: the unique manifest capability identities must equal
        //    the schema/import capability authority set. Build the authority set from
        //    the verified table, then account each cap-bearing manifest node against
        //    it (sparse ids resolved by lower_bound to the exact entry; the import
        //    ordinal is that entry's position, never cap-id-as-index). Any manifest
        //    cap absent from the authority, or any authority cap unreferenced by the
        //    manifest, fails closed.
        std::unordered_set<std::uint32_t> referenced;
        auto verified_table = ir::core::make_verified_wire_schema_table(*decoded_table.table);
        if (!verified_table.ok() || !verified_table.table.has_value()) {
            result.diagnostics = std::move(verified_table.diagnostics);
            if (!result.has_errors()) {
                result.diagnostics.push_back(error("wire-schema table failed authority admission"));
            }
            return result;
        }
        auto payload = std::make_shared<detail::SchemaModulePayload>();
        payload->entry_id = manifest->entry_id;
        payload->nodes = manifest->nodes;
        payload->have_memory_section = framing->have_memory_section;
        payload->memory = framing->memory;
        // Copy the three digests computed once during framing (no re-hash here).
        payload->module_sha256 = framing->module_sha256;
        payload->wire_schema_sha256 = framing->wire_schema_sha256;
        payload->exec_manifest_sha256 = framing->exec_manifest_sha256;

        for (std::size_t n = 0; n < manifest->nodes.size(); ++n) {
            const ManifestNode &node = manifest->nodes[n];
            if (node.cap_call_count == 0) {
                continue;
            }
            // Locate the capability by IDENTITY in the (strictly increasing, unique)
            // table via lower_bound; the ordinal is its position.
            const auto it = std::lower_bound(
                table.capabilities.begin(), table.capabilities.end(), node.capability,
                [](const CoreWireCapabilitySchema &entry, CoreCapabilityId target) noexcept {
                    return entry.capability.value < target.value;
                });
            if (it == table.capabilities.end() || !(it->capability == node.capability)) {
                result.diagnostics.push_back(
                    error("exec-manifest capability is not present in the authority set"));
                return result;
            }
            if (it->source_symbol != node.source_symbol) {
                result.diagnostics.push_back(
                    error("exec-manifest source symbol does not match the capability entry"));
                return result;
            }
            const auto ordinal =
                static_cast<std::uint32_t>(it - table.capabilities.begin());
            referenced.insert(node.capability.value);

            // 6. Emitter contract: exactly one Param today. Eagerly mint Param{0} +
            //    Result through the shared authority; any failure fails the module.
            if (it->params.size() != 1) {
                result.diagnostics.push_back(
                    error("exec-manifest capability does not have exactly one parameter"));
                return result;
            }
            const CoreWireRootSelector param_selector{node.capability, node.source_symbol,
                                                      CoreWireRootKind::Param, 0};
            const CoreWireRootSelector result_selector{node.capability, node.source_symbol,
                                                       CoreWireRootKind::Result, 0};
            std::vector<CoreLowerDiagnostic> mint_diags;
            auto param = ir::core::make_wire_binding_from_verified_table(
                *verified_table.table, param_selector, mint_diags);
            if (!param.has_value()) {
                result.diagnostics = std::move(mint_diags);
                if (!result.has_errors()) {
                    result.diagnostics.push_back(
                        error("exec-manifest Param binding could not be minted"));
                }
                return result;
            }
            auto result_binding = ir::core::make_wire_binding_from_verified_table(
                *verified_table.table, result_selector, mint_diags);
            if (!result_binding.has_value()) {
                result.diagnostics = std::move(mint_diags);
                if (!result.has_errors()) {
                    result.diagnostics.push_back(
                        error("exec-manifest Result binding could not be minted"));
                }
                return result;
            }
            payload->call_sites.push_back(
                detail::CallSiteRecord{n, CapabilityImportOrdinal{ordinal}, std::move(*param),
                                       std::move(*result_binding)});
        }

        // Set equality: every authority capability must be referenced by the manifest.
        for (const CoreWireCapabilitySchema &cap : table.capabilities) {
            if (referenced.find(cap.capability.value) == referenced.end()) {
                result.diagnostics.push_back(
                    error("a wire-schema capability is not referenced by the exec-manifest"));
                return result;
            }
        }

        result.module = VerifiedCoreWasmSchemaModule(std::move(payload));
        return result;
    }
};

VerifiedCoreWasmSchemaModuleResult
make_verified_core_wasm_schema_module(std::span<const std::uint8_t> module_bytes) {
    return SchemaModuleFactory::make(module_bytes);
}

} // namespace ahfl::runtime::core_wasm_schema_module
