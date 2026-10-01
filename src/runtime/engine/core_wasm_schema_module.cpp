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
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/core_wasm_abi_constants.hpp"
#include "ahfl/compiler/ir/core_wire_schema.hpp"
#include "base/support/sha256.hpp"

namespace ahfl::runtime::core_wasm_schema_module {

using ir::core::CoreAgentId;
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
// WH-5b.3: the deterministic transcode import family. A transcode import is
// a pure codec adapter (no capability, no memo, no hooks, no events) with the
// EXACT sealed functype (i32,i32)->(i32,i32,i32) -- the same raw tuple shape
// as the opaque capability lane, but a separate namespace so A2 can exclude
// it from the capability<->schema cross-check and validate it independently.
constexpr std::string_view kTranscodeImportModule = "ahfl_xcode";
constexpr std::string_view kTranscodeImportFieldPrefix = "xcode_";
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
// RFC 0026 KR6.8 WH-4 fix-forward D-C: the agent entry kind. A capability
// agent module carries a flat capability-list manifest (no workflow schedule
// / nodes); the factory turns each capability into one call site.
constexpr std::uint8_t kEntryKindAgent = 1;

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
    // WH-5b.3: true for an ahfl_xcode.xcode_<decimal> transcode import. A
    // transcode import is excluded from the capability<->schema cross-check
    // (it names no capability) and is validated only for its sealed functype.
    bool is_transcode = false;
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
        const bool is_cap = module_view == kCapabilityImportModule;
        const bool is_xcode = module_view == kTranscodeImportModule;
        if (!is_cap && !is_xcode) {
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
        if (is_xcode) {
            // WH-5b.3: validate xcode_<decimal> (no source_symbol; the
            // functype seal is checked in the cross-check below).
            const std::string_view field_view(
                reinterpret_cast<const char *>(field_name->data()),
                field_name->size());
            if (field_view.substr(0, kTranscodeImportFieldPrefix.size()) !=
                kTranscodeImportFieldPrefix) {
                return false;
            }
            const std::string_view digits =
                field_view.substr(kTranscodeImportFieldPrefix.size());
            if (digits.empty()) {
                return false;
            }
            if (digits.size() > 1 && digits.front() == '0') {
                return false;
            }
            for (const char c : digits) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
            framing.imports.push_back(
                ParsedImport{0, *type_index, /*is_transcode=*/true});
        } else {
            const auto source_symbol = parse_capability_field(*field_name);
            if (!source_symbol.has_value()) {
                return false;
            }
            framing.imports.push_back(ParsedImport{*source_symbol, *type_index});
        }
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

// One decoded agent-manifest capability (the agent arm's flat list).
struct ManifestCapability {
    CoreCapabilityId capability{};
    std::uint64_t source_symbol = 0;
};

// One decoded exec-manifest node.
struct ManifestNode {
    CoreWorkflowNodeId workflow_node_id{};
    std::uint32_t schedule_pos = 0;
    std::uint8_t cap_call_count = 0;
    // One entry per capability call site on this node (cap_call_count entries).
    // A P6 bridge node can call multiple capabilities across different branches.
    std::vector<ManifestCapability> capabilities;
};

struct DecodedWorkflowManifest {
    CoreWorkflowId entry_id{};
    std::vector<ManifestNode> nodes;
};

struct DecodedAgentManifest {
    CoreAgentId agent_id{};
    std::vector<ManifestCapability> capabilities;
};

// The decoded exec-manifest, tagged by entry kind. The factory visits the
// variant to build the module payload (workflow: nodes + call sites; agent:
// call sites directly from the capability list).
using DecodedManifest =
    std::variant<DecodedWorkflowManifest, DecodedAgentManifest>;

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
    if (!entry_kind.has_value() ||
        (*entry_kind != kEntryKindWorkflow && *entry_kind != kEntryKindAgent)) {
        diagnostics.push_back(error("exec-manifest has an unsupported entry kind"));
        return std::nullopt;
    }

    if (*entry_kind == kEntryKindAgent) {
        // Agent arm: magic + version + entry_kind=1 + agent_id(4) +
        // capability_count(4) + capabilities[] { capability_id(4) +
        // source_symbol(8) }.
        const auto agent_id = cursor.u32();
        if (!agent_id.has_value() || *agent_id == CoreAgentId::kInvalid) {
            diagnostics.push_back(
                error("exec-manifest agent id is missing or the invalid sentinel"));
            return std::nullopt;
        }
        const auto cap_count = cursor.u32();
        if (!cap_count.has_value()) {
            diagnostics.push_back(error("exec-manifest capability count is malformed"));
            return std::nullopt;
        }
        // Count-before-reserve: min capability entry = capability_id ULEB (>= 1
        // byte) + source_symbol ULEB (>= 1 byte) >= 2 bytes.
        constexpr std::size_t kMinCapEntryBytes = 2;
        if (*cap_count > cursor.remaining() / kMinCapEntryBytes) {
            diagnostics.push_back(
                error("exec-manifest declares more capabilities than remaining bytes"));
            return std::nullopt;
        }
        DecodedAgentManifest manifest;
        manifest.agent_id = CoreAgentId{*agent_id};
        manifest.capabilities.reserve(*cap_count);
        std::unordered_set<std::uint32_t> seen_caps;
        seen_caps.reserve(*cap_count);
        for (std::uint32_t i = 0; i < *cap_count; ++i) {
            const auto capability = cursor.u32();
            if (!capability.has_value() ||
                *capability == CoreCapabilityId::kInvalid) {
                diagnostics.push_back(error(
                    "exec-manifest capability is missing or the invalid sentinel"));
                return std::nullopt;
            }
            if (!seen_caps.insert(*capability).second) {
                diagnostics.push_back(
                    error("exec-manifest capability is not unique"));
                return std::nullopt;
            }
            const auto source_symbol = cursor.u64();
            if (!source_symbol.has_value()) {
                diagnostics.push_back(
                    error("exec-manifest source symbol is malformed"));
                return std::nullopt;
            }
            manifest.capabilities.push_back(
                {CoreCapabilityId{*capability}, *source_symbol});
        }
        if (!cursor.at_end()) {
            diagnostics.push_back(error("exec-manifest has trailing bytes"));
            return std::nullopt;
        }
        return manifest;
    }

    // Workflow arm (entry_kind == 0).
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
    DecodedWorkflowManifest manifest;
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
        if (!cap_call_count.has_value()) {
            diagnostics.push_back(error("exec-manifest cap_call_count is out of range"));
            return std::nullopt;
        }
        node.cap_call_count = *cap_call_count;
        node.capabilities.reserve(*cap_call_count);
        for (std::uint8_t c = 0; c < *cap_call_count; ++c) {
            const auto capability = cursor.u32();
            if (!capability.has_value() || *capability == CoreCapabilityId::kInvalid) {
                diagnostics.push_back(
                    error("exec-manifest capability is missing or the invalid sentinel"));
                return std::nullopt;
            }
            const auto source_symbol = cursor.u64();
            if (!source_symbol.has_value()) {
                diagnostics.push_back(error("exec-manifest source symbol is malformed"));
                return std::nullopt;
            }
            node.capabilities.push_back(
                ManifestCapability{CoreCapabilityId{*capability}, *source_symbol});
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

// One pre-minted capability call site. The capability + source_symbol are
// stored on the record itself (not indirected through nodes) so the record is
// self-contained for BOTH manifest kinds: a workflow call site references its
// cap node via `node_index`; an agent call site has no node (the agent arm
// builds one call site per manifest capability) and leaves `node_index` at 0.
struct CallSiteRecord {
    std::size_t node_index = 0; // index into nodes (workflow only; 0 for agent)
    CapabilityImportOrdinal import_ordinal{};
    VerifiedWireSchemaBinding param;
    VerifiedWireSchemaBinding result;
    CoreCapabilityId capability{};
    std::uint64_t source_symbol = 0;
};

// The single immutable payload shared by the module handle and every node /
// call-site token it resolves. All three handles hold a
// `std::shared_ptr<const SchemaModulePayload>` to THIS one type.
struct SchemaModulePayload {
    // Workflow-only identity (invalid for an agent module; the resume
    // controller / event envelope are workflow-only consumers). An agent
    // module carries `is_agent` + `agent_id` instead and leaves `nodes`
    // empty; its call sites are built directly from the manifest capability
    // list.
    CoreWorkflowId entry_id{};
    std::vector<ManifestNode> nodes;
    bool is_agent = false;
    CoreAgentId agent_id{};
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
    std::visit(
        [&](const auto &arm) {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, DecodedAgentManifest>) {
                out.push_back(kEntryKindAgent);
                put_u32(arm.agent_id.value);
                put_u32(static_cast<std::uint32_t>(arm.capabilities.size()));
                for (const ManifestCapability &cap : arm.capabilities) {
                    put_u32(cap.capability.value);
                    put_u64(cap.source_symbol);
                }
            } else {
                out.push_back(kEntryKindWorkflow);
                put_u32(arm.entry_id.value);
                put_u32(static_cast<std::uint32_t>(arm.nodes.size()));
                for (const ManifestNode &node : arm.nodes) {
                    put_u32(node.workflow_node_id.value);
                    put_u32(node.schedule_pos);
                    out.push_back(node.cap_call_count);
                    for (const ManifestCapability &cap : node.capabilities) {
                        put_u32(cap.capability.value);
                        put_u64(cap.source_symbol);
                    }
                }
            }
        },
        m);
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
    return payload_->call_sites[call_site_index_].capability;
}
std::uint64_t VerifiedCoreWasmCallSite::source_symbol() const noexcept {
    return payload_->call_sites[call_site_index_].source_symbol;
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
bool VerifiedCoreWasmSchemaModule::is_agent() const noexcept {
    return payload_->is_agent;
}
ir::core::CoreAgentId VerifiedCoreWasmSchemaModule::agent_id() const noexcept {
    return payload_->agent_id;
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
        //    WH-5b.3: transcode imports (ahfl_xcode.xcode_<decimal>) are
        //    excluded from the capability cross-check (they name no
        //    capability) and validated only for their sealed tuple functype;
        //    the capability imports alone must match the schema one-to-one.
        std::size_t cap_import_count = 0;
        for (const auto &imp : framing->imports) {
            if (!imp.is_transcode) {
                ++cap_import_count;
            }
        }
        if (cap_import_count == 0) {
            result.diagnostics.push_back(error("module has a wire-schema section but no capability imports"));
            return result;
        }
        if (table.capabilities.empty()) {
            result.diagnostics.push_back(error("wire-schema table carries no capability"));
            return result;
        }
        if (cap_import_count != table.capabilities.size()) {
            result.diagnostics.push_back(error("module capability import count does not match the table"));
            return result;
        }
        // Bridge-signature imports carry multi-argument capabilities (the
        // bridge control block holds N arg ptr/len pairs). The opaque
        // (tuple) lane is always arity-1. Track which source_symbols use
        // the bridge signature so the call-site resolver below accepts
        // their multi-param schema entries.
        std::unordered_set<std::uint64_t> bridge_source_symbols;
        std::size_t cap_i = 0;
        for (const auto &imported : framing->imports) {
            const bool type_in_range =
                imported.type_index < framing->type_is_capability_tuple.size();
            const bool is_tuple =
                type_in_range && framing->type_is_capability_tuple[imported.type_index];
            const bool is_bridge =
                type_in_range && framing->type_is_bridge_signature[imported.type_index];
            if (imported.is_transcode) {
                // WH-5b.3: a transcode import must use the EXACT sealed
                // tuple functype (i32,i32)->(i32,i32,i32) -- never the bridge
                // shape, never anything else.
                if (!is_tuple) {
                    result.diagnostics.push_back(error(
                        "module transcode import does not use the ahfl_xcode tuple signature"));
                    return result;
                }
                continue;
            }
            if (imported.source_symbol != table.capabilities[cap_i].source_symbol) {
                result.diagnostics.push_back(
                    error("module import source symbol does not match the table"));
                return result;
            }
            // Every ordinal's capability-tuple/bridge signature must agree before
            // we honor the import. The schema table carries no mode field, so A2
            // accepts BOTH the opaque (i32,i32)->(i32,i32,i32) tuple and the
            // bridge (i32)->(i32,i32) shape; anything else is rejected (decision
            // doc section 11.3).
            if (!is_tuple && !is_bridge) {
                result.diagnostics.push_back(error(
                    "module capability import does not use the ahfl_cap tuple or bridge signature"));
                return result;
            }
            if (is_bridge) {
                bridge_source_symbols.insert(imported.source_symbol);
            }
            ++cap_i;
        }

        // 5. EXACT set equality: the unique manifest capability identities must equal
        //    the schema/import capability authority set. Build the authority set from
        //    the verified table, then account each manifest capability against it
        //    (sparse ids resolved by lower_bound to the exact entry; the import
        //    ordinal is that entry's position, never cap-id-as-index). Any manifest
        //    cap absent from the authority, or any authority cap unreferenced by the
        //    manifest, fails closed. BOTH manifest kinds share this discipline: a
        //    workflow accounts its cap-bearing nodes (node_index = schedule slot),
        //    an agent accounts its flat capability list (node_index unused, 0).
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
        payload->have_memory_section = framing->have_memory_section;
        payload->memory = framing->memory;
        // Copy the three digests computed once during framing (no re-hash here).
        payload->module_sha256 = framing->module_sha256;
        payload->wire_schema_sha256 = framing->wire_schema_sha256;
        payload->exec_manifest_sha256 = framing->exec_manifest_sha256;

        // Resolve one capability identity against the authority set and mint its
        // Param{0} + Result bindings through the shared authority. On any authority
        // violation the diagnostic is pushed and false returned.
        auto resolve_capability_call_site =
            [&](CoreCapabilityId capability, std::uint64_t source_symbol,
                std::size_t node_index) -> bool {
            // Locate the capability by IDENTITY in the (strictly increasing, unique)
            // table via lower_bound; the ordinal is its position.
            const auto it = std::lower_bound(
                table.capabilities.begin(), table.capabilities.end(), capability,
                [](const CoreWireCapabilitySchema &entry, CoreCapabilityId target) noexcept {
                    return entry.capability.value < target.value;
                });
            if (it == table.capabilities.end() || !(it->capability == capability)) {
                result.diagnostics.push_back(
                    error("exec-manifest capability is not present in the authority set"));
                return false;
            }
            if (it->source_symbol != source_symbol) {
                result.diagnostics.push_back(
                    error("exec-manifest source symbol does not match the capability entry"));
                return false;
            }
            const auto ordinal =
                static_cast<std::uint32_t>(it - table.capabilities.begin());
            referenced.insert(capability.value);

            // Emitter contract: the opaque (tuple) lane is always arity-1, so
            // exactly one Param is minted. The bridge lane carries
            // multi-argument capabilities (the bridge control block holds N
            // arg ptr/len pairs); the bridge handler resolves all params from
            // the wire schema table at runtime, so the single Param{0}
            // binding minted here is sufficient (it is only used to access
            // the shared table, not as the sole arg descriptor).
            const bool is_bridge =
                bridge_source_symbols.count(source_symbol) > 0;
            if (it->params.empty()) {
                result.diagnostics.push_back(
                    error("exec-manifest capability has no parameters"));
                return false;
            }
            if (!is_bridge && it->params.size() != 1) {
                result.diagnostics.push_back(
                    error("exec-manifest capability does not have exactly one parameter"));
                return false;
            }
            const CoreWireRootSelector param_selector{capability, source_symbol,
                                                      CoreWireRootKind::Param, 0};
            const CoreWireRootSelector result_selector{capability, source_symbol,
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
                return false;
            }
            auto result_binding = ir::core::make_wire_binding_from_verified_table(
                *verified_table.table, result_selector, mint_diags);
            if (!result_binding.has_value()) {
                result.diagnostics = std::move(mint_diags);
                if (!result.has_errors()) {
                    result.diagnostics.push_back(
                        error("exec-manifest Result binding could not be minted"));
                }
                return false;
            }
            payload->call_sites.push_back(
                detail::CallSiteRecord{node_index, CapabilityImportOrdinal{ordinal},
                                       std::move(*param), std::move(*result_binding),
                                       capability, source_symbol});
            return true;
        };

        bool call_sites_ok = true;
        std::visit(
            [&](const auto &arm) {
                using T = std::decay_t<decltype(arm)>;
                if constexpr (std::is_same_v<T, DecodedAgentManifest>) {
                    // Agent arm: one call site per manifest capability. The agent
                    // module carries no workflow schedule, so node_index is unused
                    // (left at 0); the workflow-only accessors on the resolved call
                    // site have a workflow-module precondition and are never called
                    // for an agent module (the WH-3 executor consumes only
                    // import_ordinal / param / result / source_symbol).
                    payload->is_agent = true;
                    payload->agent_id = arm.agent_id;
                    for (const ManifestCapability &cap : arm.capabilities) {
                        if (!resolve_capability_call_site(cap.capability,
                                                          cap.source_symbol, 0)) {
                            call_sites_ok = false;
                            return;
                        }
                    }
                } else {
                    // Workflow arm: identity + nodes, then one call site per
                    // capability call on each cap-bearing node. A P6 bridge
                    // node can call multiple capabilities across branches.
                    payload->entry_id = arm.entry_id;
                    payload->nodes = arm.nodes;
                    for (std::size_t n = 0; n < arm.nodes.size(); ++n) {
                        const ManifestNode &node = arm.nodes[n];
                        for (const ManifestCapability &cap : node.capabilities) {
                            if (!resolve_capability_call_site(cap.capability,
                                                              cap.source_symbol, n)) {
                                call_sites_ok = false;
                                return;
                            }
                        }
                    }
                }
            },
            *manifest);
        if (!call_sites_ok) {
            return result;
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
